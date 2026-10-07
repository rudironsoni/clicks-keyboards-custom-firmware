#include <string.h>
#include "tusb.h"
#include "host.h"
#include "timer.h"
#include "ck5200_update_protocol.h"
#include "ck5200_staging.h"

uint8_t keyboard_protocol = 1;
uint8_t keyboard_idle = 0;
static uint8_t keyboard_led_state = 0;

static uint8_t keyboard_leds_impl(void) {
    return keyboard_led_state;
}

static void send_keyboard_impl(report_keyboard_t *report) {
    if (!tud_mounted()) return;

    uint32_t started = timer_read32();
    while (!tud_hid_ready()) {
        tud_task();
        if (timer_elapsed32(started) >= 5u) return;
    }

    (void)tud_hid_report(0, report, sizeof(*report));
}

static void send_nkro_impl(report_nkro_t *report) {
    (void)report;
}

static void send_mouse_impl(report_mouse_t *report) {
    (void)report;
}

static void send_extra_impl(report_extra_t *report) {
    (void)report;
}

static host_driver_t ck5200_host_driver = {
    .keyboard_leds = keyboard_leds_impl,
    .send_keyboard = send_keyboard_impl,
    .send_nkro = send_nkro_impl,
    .send_mouse = send_mouse_impl,
    .send_extra = send_extra_impl,
};

void protocol_setup(void) {
    host_set_driver(&ck5200_host_driver);
    (void)tud_init(1);
}

void protocol_pre_init(void) {}
void protocol_post_init(void) {}
void protocol_pre_task(void) { tud_task(); }
void protocol_post_task(void) { tud_task(); }

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    if (reqlen) buffer[0] = 0;
    return reqlen ? 1u : 0u;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize > 0) {
        keyboard_led_state = buffer[0];
    }
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
    (void)instance;
    keyboard_protocol = protocol;
}

void tud_hid_set_idle_cb(uint8_t instance, uint8_t idle_rate) {
    (void)instance;
    keyboard_idle = idle_rate;
}

void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint32_t bufsize) {
    if (idx != 0 || !buffer || bufsize < 2 || bufsize > 64) return;

    uint8_t response[8];
    size_t response_len = ck5200_update_handle(buffer, (size_t)bufsize, response, &ck5200_staging_ops);
    if (response_len != 0) {
        (void)tud_vendor_n_write(0, response, (uint32_t)response_len);
    }
}
