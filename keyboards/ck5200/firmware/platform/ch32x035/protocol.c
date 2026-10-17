#include <string.h>
#include "tusb.h"
#include "host.h"
#include "keyboard.h"
#include "action_util.h"
#include "timer.h"
#include "iap2.h"
#include "ck5200_backlight.h"
#include "ck5200_consumer.h"
#include "ck5200_update_protocol.h"
#include "ck5200_staging.h"
#include "ck5200_keymap.h"
#include "eeconfig.h"
#include "ck5200_rescue.h"

#define UPDATE_RESPONSE_SIZE 8u
#define CK_HID_REPORT_SIZE 10u /* 8-byte keyboard report + 16-bit consumer */
#define HID_QUEUE_DEPTH 16u
/* Reconcile a lost callback only after TinyUSB reports the endpoint free. */
#define HID_LOST_COMPLETION_GRACE_MS 20u

uint8_t keyboard_protocol = 1, keyboard_idle = 0;
static uint8_t keyboard_led_state;
static report_keyboard_t last_keyboard_report;
static uint8_t hid_queue[HID_QUEUE_DEPTH][CK_HID_REPORT_SIZE];
static uint8_t hid_head, hid_count;
static uint32_t last_keyboard_report_sent_at;
static bool keyboard_report_in_flight, last_keyboard_report_valid;
static uint32_t hid_inflight_started_at;
static uint8_t pending_update_response[UPDATE_RESPONSE_SIZE];
static uint8_t pending_update_response_len;
static uint8_t pending_update_response_itf;
static bool update_response_in_flight;
static uint32_t iap2_last_tick_ms;

static void cancel_rescue_if_disconnected(void) {
    if (ck5200_rescue_active() && !iap2_restore_authorized()) {
        ck5200_staging_abort();
        pending_update_response_len = 0;
        update_response_in_flight = false;
    }
}

static uint8_t keyboard_leds_impl(void) { return keyboard_led_state; }

/* Sends the 8-byte keyboard part plus the 16-bit consumer tail. */
static bool submit_hid_report(void) {
    if (!tud_mounted() || tud_suspended() || keyboard_report_in_flight || !tud_hid_ready()) return false;
    if (!hid_count) return false;

    /* Boot protocol carries the plain 8-byte boot report; the consumer
     * tail exists only in report protocol (review 2026-10-11, F7). */
    const uint16_t length = keyboard_protocol ? CK_HID_REPORT_SIZE : 8u;
    if (!tud_hid_report(0, hid_queue[hid_head], length)) return false;

    keyboard_report_in_flight = true;
    hid_inflight_started_at = timer_read32();
    return true;
}

static void queue_keyboard_report(const report_keyboard_t *report) {
    /* Preserve one-shot/mod-tap edges within a scan. On overload retain the
     * newest state in the last slot so releases cannot leave keys stuck.
     * Unmounted/suspended states coalesce; do not replay offline typing. */
    if (!tud_mounted() || tud_suspended()) hid_head = hid_count = 0;
    if (hid_count < HID_QUEUE_DEPTH) ++hid_count;
    uint8_t tail = (uint8_t)((hid_head + hid_count - 1u) % HID_QUEUE_DEPTH);
    ck5200_consumer_pack(hid_queue[tail], (const uint8_t *)report);
    ck5200_consumer_clear_dirty();
}

static void send_keyboard_impl(report_keyboard_t *report) {
    if (!report) return;

    queue_keyboard_report(report);
    (void)submit_hid_report();
}

void protocol_keyboard_task(void) {
    const bool connected = tud_mounted() && !tud_suspended();

    /* A missing TinyUSB callback cannot freeze scanning.  A ready endpoint
     * (unclaimed and not busy in pinned TinyUSB) after the grace interval
     * establishes that no USB transfer is currently owned by the driver.
     * It does NOT establish that the host received the old report.
     * Therefore keep the queue head and retry in order, including releases.
     * Never re-submit while TinyUSB still reports the endpoint busy. */
    if (connected && keyboard_report_in_flight && tud_hid_ready() &&
        timer_elapsed32(hid_inflight_started_at) >= HID_LOST_COMPLETION_GRACE_MS) {
        keyboard_report_in_flight = false;
    }

    /* Advance QMK independently of the HID endpoint state so that a lost
     * callback, backpressure, and suspend cannot starve the matrix. */
    keyboard_task();

    if (!connected || keyboard_report_in_flight || !tud_hid_ready()) return;
    if (hid_count) { (void)submit_hid_report(); return; }

    /* A consumer-only change still needs a fresh report on the wire. */
    if (ck5200_consumer_dirty()) {
        if (keyboard_report) queue_keyboard_report(keyboard_report);
        else if (last_keyboard_report_valid) queue_keyboard_report(&last_keyboard_report);
        if (submit_hid_report()) return;
    }

    if (keyboard_idle != 0 && last_keyboard_report_valid &&
        timer_elapsed32(last_keyboard_report_sent_at) >= (uint32_t)keyboard_idle * 4u) {
        queue_keyboard_report(&last_keyboard_report);
        (void)submit_hid_report();
    }
}

static void send_nkro_impl(report_nkro_t *report) { (void)report; }
static void send_mouse_impl(report_mouse_t *report) { (void)report; }
static void send_extra_impl(report_extra_t *report) { (void)report; }

static host_driver_t ck5200_host_driver = {
    .keyboard_leds = keyboard_leds_impl,
    .send_keyboard = send_keyboard_impl,
    .send_nkro = send_nkro_impl,
    .send_mouse = send_mouse_impl,
    .send_extra = send_extra_impl,
};

void protocol_setup(void) {
    host_set_driver(&ck5200_host_driver);
    ck5200_transport_setup();
    ck5200_backlight_init();
}

void ck5200_transport_setup(void) {
    /* CH32X035: TinyUSB's port drives root port 0 (the V203 build used
     * port 1, which does not exist on this family; review 2026-10-12,
     * finding 1). */
    tud_init(0);
    iap2_init();
    iap2_last_tick_ms = timer_read32();
}

static void apply_persisted_brightness_once(void) {
    static bool done;
    if (done || !eeconfig_is_enabled()) return;
    done = true;
    ck5200_backlight_set((uint8_t)(eeconfig_read_kb() & 0xffu));
}

void protocol_pre_init(void) {} void protocol_post_init(void) {}
void protocol_pre_task(void) { tud_task(); }

static void send_pending_update_response(void) {
    if (pending_update_response_len == 0 || update_response_in_flight || !tud_mounted() || tud_suspended()) return;
    /* Stock 0x62cc: the EP3 companion channel answers only while the
     * Apple session is active. */
    if (pending_update_response_itf == 1 && !iap2_session_active()) {
        pending_update_response_len = 0;
        return;
    }
    if (tud_vendor_n_write(pending_update_response_itf, pending_update_response,
                           pending_update_response_len) != pending_update_response_len) {
        return;
    }

    update_response_in_flight = true;
}

static void account_iap2_elapsed_time(void) {
    const uint32_t now = timer_read32();
    const uint32_t elapsed = now - iap2_last_tick_ms; /* Also handles timer wrap. */
    iap2_last_tick_ms = now;
    iap2_elapsed_ms(elapsed);
    cancel_rescue_if_disconnected();
}

static void service_iap2(void) {
    account_iap2_elapsed_time();
    iap2_task();

    cancel_rescue_if_disconnected();

    send_pending_update_response();
}

void protocol_post_task(void) {
    tud_task();
    apply_persisted_brightness_once();
    service_iap2();
}

void ck5200_transport_task(void) {
    tud_task();
    service_iap2();
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    if (instance != 0 || report_id != 0 || !buffer || reqlen == 0) return 0;
    if (ck5200_rescue_active()) return 0;

    if (report_type == HID_REPORT_TYPE_INPUT && keyboard_report) {
        uint8_t buf[CK_HID_REPORT_SIZE];
        ck5200_consumer_pack(buf, (const uint8_t *)keyboard_report);
        const uint16_t report_size = keyboard_protocol ? CK_HID_REPORT_SIZE : 8u;
        const uint16_t length = report_size < reqlen ? report_size : reqlen;
        memcpy(buffer, buf, length);
        return length;
    }

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        buffer[0] = keyboard_led_state;
        return 1;
    }

    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    if (instance == 0 && report_id == 0 && report_type == HID_REPORT_TYPE_OUTPUT && buffer && bufsize > 0) {
        keyboard_led_state = (uint8_t)(buffer[0] & 0x1Fu);
    }
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
    if (instance == 0 && protocol <= 1) keyboard_protocol = protocol;
}

bool tud_hid_set_idle_cb(uint8_t instance, uint8_t idle_rate) {
    if (instance != 0) return false;
    keyboard_idle = idle_rate; last_keyboard_report_sent_at = timer_read32();
    return true;
}

static void retain_interrupted_keyboard_report(void) {
    if (ck5200_rescue_active()) return;
    hid_head = hid_count = 0;
    if (keyboard_report) queue_keyboard_report(keyboard_report);
    keyboard_report_in_flight = false;
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
    if (instance != 0 || !keyboard_report_in_flight) return;
    keyboard_report_in_flight = false;
    /* Transfers complete at the wire size (10 with the consumer tail, 8
     * in boot protocol); the keyboard part is always the first 8 bytes.
     * The old exact-8 check never matched, the pending report was never
     * cleared, and one completed tap froze the scan forever (review
     * 2026-10-11, F1/F-1). */
    if (!report || len < sizeof(last_keyboard_report)) return;

    memcpy(&last_keyboard_report, report, sizeof(last_keyboard_report));
    last_keyboard_report_valid = true;
    last_keyboard_report_sent_at = timer_read32();
    if (hid_count) { hid_head = (uint8_t)((hid_head + 1u) % HID_QUEUE_DEPTH); --hid_count; }
}

void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t report_type,
                              uint8_t const *report, uint16_t xferred_bytes) {
    (void)report;
    (void)xferred_bytes;
    if (instance == 0 && report_type == HID_REPORT_TYPE_INPUT && keyboard_report_in_flight) {
        keyboard_report_in_flight = false; /* Keep the queue head for retry. */
    }
}

void tud_mount_cb(void) {
    keyboard_protocol = 1;
    keyboard_idle = keyboard_led_state = 0;
    last_keyboard_report_valid = false;
    retain_interrupted_keyboard_report();
    iap2_set_connected(true);
}
void tud_umount_cb(void) {
    retain_interrupted_keyboard_report();
    iap2_set_connected(false);
    pending_update_response_len = 0;
    update_response_in_flight = false;
    if (ck5200_rescue_active()) ck5200_staging_abort();
}
void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; retain_interrupted_keyboard_report(); }
void tud_resume_cb(void) { retain_interrupted_keyboard_report(); }

/* Shared dispatcher entry for both bulk channels. itf selects the reply
 * endpoint pair: 0 = EP2 (Mac path, outside a session), 1 = EP3 (the raw
 * companion channel while the Apple session is active). */
static void handle_dispatcher_packet(uint8_t itf, const uint8_t *buffer, uint32_t bufsize) {
    if (!buffer || bufsize < 2 || bufsize > 64 ||
        pending_update_response_len != 0 || update_response_in_flight) return;
    if (buffer[0] != (uint8_t)bufsize) return;

    /* Explicit software entry: authenticated companion only. No image write.
     * The default iOS controller does not authorize this command. */
    if (!ck5200_rescue_active() && itf == 1 && bufsize == 2 && buffer[1] == 0x94 && iap2_restore_authorized()) {
        if (ck5200_staging_rescue_entry_allowed()) ck5200_request_rescue();
        return;
    }

    uint8_t response[UPDATE_RESPONSE_SIZE];
    size_t response_len = 0;
    if (ck5200_rescue_active()) {
        if (itf != 1 || !iap2_restore_authorized()) return;
        if (buffer[1] == 0x03 && bufsize == 2) {
            /* Distinct rescue identity, never impersonate stock or normal QM. */
            const uint8_t identity[8] = {8, 2, 3, 0, 'R', 'S', 0, 1};
            memcpy(response, identity, sizeof(identity));
            response_len = sizeof(identity);
        }
    } else {
        response_len = ck5200_keymap_handle(buffer, (size_t)bufsize, response);
    }
    if (response_len == 0) {
        response_len = ck5200_update_handle(buffer, (size_t)bufsize, response, &ck5200_staging_ops);
    }

    if (response_len >= 4 && response_len <= UPDATE_RESPONSE_SIZE) {
        memcpy(pending_update_response, response, response_len);
        pending_update_response_len = (uint8_t)response_len;
        pending_update_response_itf = itf;
    }
}

void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint32_t bufsize) {
    if (!buffer || bufsize == 0) return;
    /* Account for silence before this packet can refresh the watchdog. */
    account_iap2_elapsed_time();

    if (idx == 0) {
        /* EP2 is the iAP2 link. Outside a session, dispatcher-framed packets
         * keep the Mac recovery path alive; the phone only ever sends the
         * FF 55 attach or FF 5A link framing, which never matches that shape
         * (its length byte would exceed the 64-byte endpoint). */
        if (!iap2_session_active() && bufsize >= 2 && bufsize <= 64 &&
            buffer[0] == (uint8_t)bufsize) {
            handle_dispatcher_packet(0, buffer, bufsize);
            return;
        }
        iap2_ep2_rx(buffer, bufsize);
        cancel_rescue_if_disconnected();
        return;
    }

    if (idx == 1) {
        /* EP3 carries raw companion data (stock 0x629e gate). */
        if (!iap2_session_active()) return;
        iap2_activity();
        handle_dispatcher_packet(1, buffer, bufsize);
    }
}

void tud_vendor_tx_cb(uint8_t idx, uint32_t sent_bytes) {
    if (idx != pending_update_response_itf || !update_response_in_flight) return;
    account_iap2_elapsed_time();
    if (!update_response_in_flight) return; /* Rescue authority expired. */
    update_response_in_flight = false;
    if (pending_update_response_len == 4 && pending_update_response[2] == 0xA0) {
        pending_update_response_len = 0; /* Never retry a failed/short A0 ACK. */
        if (sent_bytes == 4 && tud_mounted() && !tud_suspended() &&
            (!ck5200_rescue_active() || iap2_restore_authorized())) ck5200_staging_ops.reboot();
        return;
    }
    if (sent_bytes == pending_update_response_len) pending_update_response_len = 0;
}

/* The pinned vendor driver otherwise reports bytes without transfer status. */
void ck5200_vendor_tx_failed(uint8_t idx) {
    if (idx != pending_update_response_itf) return;
    pending_update_response_len = 0;
    update_response_in_flight = false;
}
