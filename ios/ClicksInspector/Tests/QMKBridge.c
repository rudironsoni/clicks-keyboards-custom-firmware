/* Only hardware hooks are stubbed. Commands, keymap defaults, dynamic_keymap,
 * and the transient EEPROM driver are production C from the pinned checkout. */
#include <assert.h>
#include <string.h>
#include "quantum.h"
#include "eeprom_driver.h"
#include "ck5200_keymap.h"
#include "QMKBridge.h"
#include "host.h"
#include "host_driver.h"
#include "ck5200_consumer.h"

void advance_time(uint32_t ms);
static matrix_row_t raw[MATRIX_ROWS], previous[MATRIX_ROWS];
static uint8_t reports[512][10];
static size_t report_count;
static uint8_t latest[10];
void matrix_init_custom(void) {
    memset(raw, 0, sizeof(raw));
    memset(previous, 0, sizeof(previous));
}
bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    bool changed = memcmp(raw, previous, sizeof(raw)) != 0;
    memcpy(current_matrix, raw, sizeof(raw));
    memcpy(previous, raw, sizeof(raw));
    return changed;
}
static uint8_t leds(void) { return 0; }
static void capture(report_keyboard_t *report) {
    ck5200_consumer_pack(latest, (const uint8_t *)report);
    assert(report_count < 512);
    memcpy(reports[report_count++], latest, 10);
}
static void nkro(report_nkro_t *report) { (void)report; }
static void mouse(report_mouse_t *report) { (void)report; }
static void extra(report_extra_t *report) { (void)report; }
static host_driver_t driver = {
    .keyboard_leds = leds, .send_keyboard = capture, .send_nkro = nkro,
    .send_mouse = mouse, .send_extra = extra,
};
static uint8_t brightness;
void ck5200_backlight_set(uint8_t raw) { brightness = raw; }
uint8_t ck5200_backlight_get(void) { return brightness; }
void bootloader_jump(void) { assert(!"No hardware bootloader in host tests"); }
void mcu_reset(void) { assert(!"No hardware reset in host tests"); }
void suspend_power_down_kb(void) {}
void suspend_wakeup_init_kb(void) {}

void qmk_bridge_boot(void) {
    host_set_driver(&driver);
    eeprom_driver_erase();
    keyboard_setup();
    keyboard_init();
    qmk_bridge_ticks(300);
    qmk_bridge_clear_reports();
    memset(latest, 0, sizeof(latest));
}

void qmk_bridge_key(uint8_t row, uint8_t column, uint8_t pressed) {
    assert(row < MATRIX_ROWS && column < MATRIX_COLS);
    if (pressed) raw[row] |= (matrix_row_t)1 << column;
    else raw[row] &= ~((matrix_row_t)1 << column);
}
void qmk_bridge_ticks(uint32_t milliseconds) {
    for (uint32_t i = 0; i < milliseconds; ++i) {
        advance_time(1);
        keyboard_task();
    }
}
void qmk_bridge_report(uint8_t report[10]) { memcpy(report, latest, 10); }
void qmk_bridge_clear_reports(void) { report_count = 0; }
size_t qmk_bridge_report_count(void) { return report_count; }
void qmk_bridge_report_at(size_t index, uint8_t report[10]) {
    assert(index < report_count);
    memcpy(report, reports[index], 10);
}
void qmk_bridge_layer(uint8_t layer) {
    assert(layer < 4);
    reset_oneshot_layer();
    layer_move(layer);
}

size_t qmk_bridge_send(const uint8_t *request, size_t length, uint8_t *response) {
    return ck5200_keymap_handle(request, length, response);
}
