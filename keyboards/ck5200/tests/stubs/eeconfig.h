#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Host-test stubs: ck5200_keymap.c defines eeconfig_init_kb. */
void eeconfig_init_kb(void);
bool eeconfig_is_enabled(void);
uint32_t eeconfig_read_kb(void);
void eeconfig_update_kb(uint32_t setting);
void ck5200_backlight_set(uint8_t raw);
uint8_t ck5200_backlight_get(void);
