#pragma once

/* Host-test stubs for the QMK APIs used by ck5200_keymap.c. */

#define DYNAMIC_KEYMAP_LAYER_COUNT 4

typedef struct {
    uint8_t layer, row, column;
    uint16_t keycode;
    unsigned set_calls;
    unsigned reset_calls;
} dynamic_keymap_stub_state_t;

extern dynamic_keymap_stub_state_t dynamic_keymap_stub;

uint8_t dynamic_keymap_get_layer_count(void);
uint16_t dynamic_keymap_get_keycode(uint8_t layer, uint8_t row, uint8_t column);
void dynamic_keymap_set_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode);
void dynamic_keymap_reset(void);
