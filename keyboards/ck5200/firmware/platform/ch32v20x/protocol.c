#include <string.h>
#include "tusb.h"
#include "host.h"
#include "keyboard.h"
#include "action_util.h"
#include "timer.h"
#include "ck5200_update_protocol.h"
#include "ck5200_staging.h"

#define UPDATE_RESPONSE_SIZE 8u

uint8_t keyboard_protocol = 1, keyboard_idle = 0;
static uint8_t keyboard_led_state;
static report_keyboard_t pending_keyboard_report, last_keyboard_report;
static uint32_t last_keyboard_report_sent_at;
static bool pending_keyboard_report_valid, keyboard_report_in_flight, last_keyboard_report_valid;
static uint8_t pending_update_response[UPDATE_RESPONSE_SIZE];
static bool pending_update_response_valid, update_response_in_flight;

static uint8_t keyboard_leds_impl(void) { return keyboard_led_state; }

static bool submit_keyboard_report(const report_keyboard_t *report) {
    if (!tud_mounted() || tud_suspended() || keyboard_report_in_flight || !tud_hid_ready()) return false;

    pending_keyboard_report = *report;
    pending_keyboard_report_valid = true;
    if (!tud_hid_report(0, report, sizeof(*report))) return false;
    keyboard_report_in_flight = true;
    return true;
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

void protocol_setup(void) { host_set_driver(&ck5200_host_driver); (void)tud_init(1); }

void protocol_pre_init(void) {} void protocol_post_init(void) {}
void protocol_pre_task(void) { tud_task(); }

static void send_pending_update_response(void) {
    if (!pending_update_response_valid || update_response_in_flight || !tud_mounted() || tud_suspended()) return;
    if (tud_vendor_n_write(0, pending_update_response, UPDATE_RESPONSE_SIZE) != UPDATE_RESPONSE_SIZE) return;

    update_response_in_flight = true;
}

void protocol_post_task(void) { tud_task(); send_pending_update_response(); }

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    if (instance != 0 || report_id != 0 || !buffer || reqlen == 0) return 0;

    if (report_type == HID_REPORT_TYPE_INPUT && keyboard_report) {
        const uint16_t length = (uint16_t)(sizeof(*keyboard_report) < reqlen ? sizeof(*keyboard_report) : reqlen);
        memcpy(buffer, keyboard_report, length);
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

void tud_mount_cb(void) { retain_interrupted_keyboard_report(); }
void tud_umount_cb(void) {
    retain_interrupted_keyboard_report();
    pending_update_response_valid = false;
    update_response_in_flight = false;
}
void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; retain_interrupted_keyboard_report(); }
void tud_resume_cb(void) { retain_interrupted_keyboard_report(); }

void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint32_t bufsize) {
    if (idx != 0 || !buffer || bufsize < 2 || bufsize > 64 ||
        pending_update_response_valid || update_response_in_flight) return;

    uint8_t response[UPDATE_RESPONSE_SIZE];
    const size_t response_len = ck5200_update_handle(buffer, (size_t)bufsize, response, &ck5200_staging_ops);
    if (response_len == UPDATE_RESPONSE_SIZE) {
        memcpy(pending_update_response, response, UPDATE_RESPONSE_SIZE);
        pending_update_response_valid = true;
    }
}

void tud_vendor_tx_cb(uint8_t idx, uint32_t sent_bytes) {
    if (idx != 0 || !update_response_in_flight) return;
    update_response_in_flight = false;
    if (sent_bytes == UPDATE_RESPONSE_SIZE) pending_update_response_valid = false;
}
