#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dynamic_keymap.h"
#include "eeconfig.h"
#include "../firmware/platform/ch32v20x/ck5200_keymap.h"

#define MATRIX_ROWS 6
#define MATRIX_COLS 6

dynamic_keymap_stub_state_t dynamic_keymap_stub;

uint8_t dynamic_keymap_get_layer_count(void) { return DYNAMIC_KEYMAP_LAYER_COUNT; }

uint16_t dynamic_keymap_get_keycode(uint8_t layer, uint8_t row, uint8_t column) {
    if (layer != 1 || row != 2 || column != 3) return 0;
    return 0x1234; /* arbitrary recognisable value */
}

void dynamic_keymap_set_keycode(uint8_t layer, uint8_t row, uint8_t column, uint16_t keycode) {
    dynamic_keymap_stub.layer = layer;
    dynamic_keymap_stub.row = row;
    dynamic_keymap_stub.column = column;
    dynamic_keymap_stub.keycode = keycode;
    dynamic_keymap_stub.set_calls++;
}

void dynamic_keymap_reset(void) { dynamic_keymap_stub.reset_calls++; }

int main(void) {
    uint8_t response[8];

    /* Framing: declared length must match the buffer and be >= 2. */
    const uint8_t bad_len[] = {0x05, 0x03};
    assert(ck5200_keymap_handle(bad_len, sizeof(bad_len), response) == 0);
    const uint8_t short_req[] = {0x01, 0x03};
    assert(ck5200_keymap_handle(short_req, sizeof(short_req), response) == 0);
    const uint8_t unknown[] = {0x02, 0x8f};
    assert(ck5200_keymap_handle(unknown, sizeof(unknown), response) == 0);

    /* Version: identifies this firmware unambiguously versus stock 01 20. */
    const uint8_t version[] = {0x02, 0x03};
    assert(ck5200_keymap_handle(version, sizeof(version), response) == 8);
    assert(memcmp(response, (uint8_t[]){0x08, 0x02, 0x03, 0x00, 'Q', 'M', 0x00, 0x01}, 8) == 0);

    /* Info: layers, rows, cols. */
    const uint8_t info[] = {0x02, 0x90};
    assert(ck5200_keymap_handle(info, sizeof(info), response) == 8);
    assert(memcmp(response, (uint8_t[]){0x08, 0x02, 0x90, 0x00, 0x04, 0x06, 0x06, 0x00}, 8) == 0);

    /* Get key: big-endian keycode payload. */
    const uint8_t get[] = {0x05, 0x91, 0x01, 0x02, 0x03};
    assert(ck5200_keymap_handle(get, sizeof(get), response) == 6);
    assert(memcmp(response, (uint8_t[]){0x06, 0x02, 0x91, 0x00, 0x12, 0x34}, 6) == 0);

    /* Out-of-range layer/row/col rejected with status 01. */
    const uint8_t bad_layer[] = {0x05, 0x91, 0x04, 0x02, 0x03};
    assert(ck5200_keymap_handle(bad_layer, sizeof(bad_layer), response) == 4);
    assert(memcmp(response, (uint8_t[]){0x04, 0x02, 0x91, 0x01}, 4) == 0);
    const uint8_t bad_row[] = {0x07, 0x92, 0x00, 0x06, 0x00, 0x12, 0x34};
    assert(ck5200_keymap_handle(bad_row, sizeof(bad_row), response) == 4);
    assert(memcmp(response, (uint8_t[]){0x04, 0x02, 0x92, 0x01}, 4) == 0);
    assert(dynamic_keymap_stub.set_calls == 0);

    /* Set key: decodes big-endian keycode, passes position through. */
    const uint8_t set[] = {0x07, 0x92, 0x01, 0x02, 0x03, 0x12, 0x34};
    assert(ck5200_keymap_handle(set, sizeof(set), response) == 4);
    assert(memcmp(response, (uint8_t[]){0x04, 0x02, 0x92, 0x00}, 4) == 0);
    assert(dynamic_keymap_stub.set_calls == 1);
    assert(dynamic_keymap_stub.layer == 1 && dynamic_keymap_stub.row == 2 && dynamic_keymap_stub.column == 3);
    assert(dynamic_keymap_stub.keycode == 0x1234);

    /* Reset clears to the factory map. */
    const uint8_t reset[] = {0x02, 0x93};
    assert(ck5200_keymap_handle(reset, sizeof(reset), response) == 4);
    assert(memcmp(response, (uint8_t[]){0x04, 0x02, 0x93, 0x00}, 4) == 0);
    assert(dynamic_keymap_stub.reset_calls == 1);

    /* eeconfig_init_kb must seed the map from the factory layout. */
    eeconfig_init_kb();
    assert(dynamic_keymap_stub.reset_calls == 2);

    puts("ok");
    return 0;
}
