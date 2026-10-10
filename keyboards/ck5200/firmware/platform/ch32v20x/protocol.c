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

#define UPDATE_RESPONSE_SIZE 8u
#define HID_REPORT_SIZE 10u /* 8-byte keyboard report + 16-bit consumer */

uint8_t keyboard_protocol = 1, keyboard_idle = 0;
static uint8_t keyboard_led_state;
static report_keyboard_t pending_keyboard_report, last_keyboard_report;
static uint32_t last_keyboard_report_sent_at;
static bool pending_keyboard_report_valid, keyboard_report_in_flight, last_keyboard_report_valid;
static uint8_t pending_update_response[UPDATE_RESPONSE_SIZE];
static uint8_t pending_update_response_len;
static uint8_t pending_update_response_itf;
static bool update_response_in_flight;
static uint32_t iap2_last_tick_ms;

static uint8_t keyboard_leds_impl(void) { return keyboard_led_state; }

/* Sends the 8-byte keyboard part plus the 16-bit consumer tail. */
static bool submit_hid_report(const report_keyboard_t *report) {
    if (!tud_mounted() || tud_suspended() || keyboard_report_in_flight || !tud_hid_ready()) return false;

    uint8_t buf[HID_REPORT_SIZE];
    ck5200_consumer_pack(buf, (const uint8_t *)report);
    if (!tud_hid_report(0, buf, sizeof(buf))) return false;

    keyboard_report_in_flight = true;
    return true;
}

static bool submit_keyboard_report(const report_keyboard_t *report) {
    return submit_hid_report(report);
}

static void send_keyboard_impl(report_keyboard_t *report) {
    if (!report) return;

    /* One pending state coalesces a task pass; synchronous macros need a report queue. */
    if (!tud_mounted() || tud_suspended() || pending_keyboard_report_valid || keyboard_report_in_flight ||
        !submit_keyboard_report(report)) {
        pending_keyboard_report = *report;
        pending_keyboard_report_valid = true;
    }
}

void protocol_keyboard_task(void) {
    if (!tud_mounted() || tud_suspended()) { keyboard_task(); return; }
    if (keyboard_report_in_flight || !tud_hid_ready()) return;
    if (pending_keyboard_report_valid) { (void)submit_keyboard_report(&pending_keyboard_report); return; }

    /* A consumer-only change still needs a fresh report on the wire. */
    if (ck5200_consumer_dirty()) {
        ck5200_consumer_clear_dirty();
        if (keyboard_report && submit_hid_report(keyboard_report)) return;
        if (last_keyboard_report_valid && submit_hid_report(&last_keyboard_report)) return;
    }

    if (keyboard_idle != 0 && last_keyboard_report_valid &&
        timer_elapsed32(last_keyboard_report_sent_at) >= (uint32_t)keyboard_idle * 4u) {
        if (!submit_keyboard_report(&last_keyboard_report)) {
            pending_keyboard_report = last_keyboard_report;
            pending_keyboard_report_valid = true;
        }
        return;
    }

    keyboard_task();
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
    tud_init(1);
    iap2_init();
    ck5200_backlight_init();
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

void protocol_post_task(void) {
    tud_task();

    apply_persisted_brightness_once();

    const uint32_t now = timer_read32();
    if (now != iap2_last_tick_ms) {
        iap2_last_tick_ms = now;
        iap2_tick_1ms();
    }
    iap2_task();

    send_pending_update_response();
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    if (instance != 0 || report_id != 0 || !buffer || reqlen == 0) return 0;

    if (report_type == HID_REPORT_TYPE_INPUT && keyboard_report) {
        uint8_t buf[HID_REPORT_SIZE];
        ck5200_consumer_pack(buf, (const uint8_t *)keyboard_report);
        const uint16_t length = (uint16_t)((HID_REPORT_SIZE < reqlen) ? HID_REPORT_SIZE : reqlen);
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
    if (keyboard_report) {
        pending_keyboard_report = *keyboard_report;
        pending_keyboard_report_valid = true;
    }
    keyboard_report_in_flight = false;
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
    if (instance != 0 || !keyboard_report_in_flight) return;
    keyboard_report_in_flight = false;
    if (!report || len != sizeof(last_keyboard_report)) return;

    memcpy(&last_keyboard_report, report, sizeof(last_keyboard_report));
    last_keyboard_report_valid = true;
    last_keyboard_report_sent_at = timer_read32();
    if (memcmp(&pending_keyboard_report, report, sizeof(last_keyboard_report)) == 0) {
        pending_keyboard_report_valid = false;
    }
}

void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t report_type,
                              uint8_t const *report, uint16_t xferred_bytes) {
    (void)report;
    (void)xferred_bytes;
    if (instance == 0 && report_type == HID_REPORT_TYPE_INPUT && keyboard_report_in_flight) {
        keyboard_report_in_flight = false;
    }
}

void tud_mount_cb(void) { retain_interrupted_keyboard_report(); iap2_set_connected(true); }
void tud_umount_cb(void) {
    retain_interrupted_keyboard_report();
    iap2_set_connected(false);
    pending_update_response_len = 0;
    update_response_in_flight = false;
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

    uint8_t response[UPDATE_RESPONSE_SIZE];
    size_t response_len = ck5200_keymap_handle(buffer, (size_t)bufsize, response);
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
    update_response_in_flight = false;
    if (sent_bytes == pending_update_response_len) pending_update_response_len = 0;
}
