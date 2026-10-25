/* Only hardware hooks are stubbed. Commands, keymap defaults, dynamic_keymap,
 * and the transient EEPROM driver are production C from the pinned checkout. */
#include <assert.h>
#include <string.h>
#include "quantum.h"
#include "eeprom_driver.h"
#include "ck5200_keymap.h"
#include "QMKBridge.h"

void matrix_init_custom(void) {}
bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    memset(current_matrix, 0, sizeof(matrix_row_t) * MATRIX_ROWS);
    return false;
}
static uint8_t brightness;
void ck5200_backlight_set(uint8_t raw) { brightness = raw; }
uint8_t ck5200_backlight_get(void) { return brightness; }
void bootloader_jump(void) { assert(!"No hardware bootloader in host tests"); }
void mcu_reset(void) { assert(!"No hardware reset in host tests"); }
void suspend_power_down_kb(void) {}
void suspend_wakeup_init_kb(void) {}

void qmk_bridge_boot(void) {
    eeprom_driver_erase();
    keyboard_setup();
    keyboard_init();
}

size_t qmk_bridge_send(const uint8_t *request, size_t length, uint8_t *response) {
    return ck5200_keymap_handle(request, length, response);
}
