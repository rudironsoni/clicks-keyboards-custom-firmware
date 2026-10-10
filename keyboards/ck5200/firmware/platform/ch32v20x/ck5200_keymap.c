/*
 * CK-5200 custom firmware commands over the vendor bulk endpoint.
 *
 * Thin dispatch only: all keymap behavior comes from QMK's dynamic_keymap
 * module. Writes are durable immediately through QMK's wear-leveled EEPROM,
 * so there is no separate save command. 0xA0..0xA3 remain in
 * ck5200_update_protocol.c.
 */
#include "ck5200_keymap.h"

#include "dynamic_keymap.h"
#include "eeconfig.h"
#include "ck5200_backlight.h"

#define CMD_VERSION 0x03u
#define CMD_SET_BRIGHTNESS 0x82u
#define CMD_GET_BRIGHTNESS 0x84u
#define CMD_GET_INFO 0x90u
#define CMD_GET_KEY 0x91u
#define CMD_SET_KEY 0x92u
#define CMD_RESET 0x93u

#define CUSTOM_FIRMWARE_ID 0x514Du /* "QM" */

/* First boot on fresh EEPROM: seed the dynamic keymap from the factory map. */
void eeconfig_init_kb(void) { dynamic_keymap_reset(); }

static size_t reply_status(uint8_t command, uint8_t status, uint8_t response[8]) {
    response[0] = 4;
    response[1] = 0x02;
    response[2] = command;
    response[3] = status;
    return 4;
}

static size_t reply_word(uint8_t command, uint16_t value, uint8_t response[8]) {
    response[0] = 6;
    response[1] = 0x02;
    response[2] = command;
    response[3] = 0x00;
    response[4] = (uint8_t)(value >> 8);
    response[5] = (uint8_t)value;
    return 6;
}

size_t ck5200_keymap_handle(const uint8_t *request, size_t request_len, uint8_t response[8]) {
    if (!request || request_len < 2 || request[0] != request_len) return 0;
    const uint8_t command = request[1];

    switch (command) {
        case CMD_VERSION:
            /* 08 02 03 00 "QM" 00 01: unambiguous versus the stock 01 20 reply. */
            if (request_len != 2) return 0;
            response[0] = 8;
            response[1] = 0x02;
            response[2] = CMD_VERSION;
            response[3] = 0x00;
            response[4] = (uint8_t)(CUSTOM_FIRMWARE_ID >> 8);
            response[5] = (uint8_t)CUSTOM_FIRMWARE_ID;
            response[6] = 0x00;
            response[7] = 0x01;
            return 8;

        case CMD_SET_BRIGHTNESS:
            /* 03 82 raw: apply the stock formula and persist. */
            if (request_len != 3) return 0;
            ck5200_backlight_set(request[2]);
            eeconfig_update_kb(request[2]);
            return reply_status(command, 0x00, response);

        case CMD_GET_BRIGHTNESS:
            /* 02 84 -> 05 02 84 00 raw, matching the stock reply shape. */
            if (request_len != 2) return 0;
            response[0] = 5;
            response[1] = 0x02;
            response[2] = command;
            response[3] = 0x00;
            response[4] = ck5200_backlight_get();
            return 5;

        case CMD_GET_INFO:
            if (request_len != 2) return 0;
            response[0] = 8;
            response[1] = 0x02;
            response[2] = CMD_GET_INFO;
            response[3] = 0x00;
            response[4] = dynamic_keymap_get_layer_count();
            response[5] = MATRIX_ROWS;
            response[6] = MATRIX_COLS;
            response[7] = 0x00;
            return 8;

        case CMD_GET_KEY:
            if (request_len != 5) return 0;
            if (request[2] >= dynamic_keymap_get_layer_count() || request[3] >= MATRIX_ROWS || request[4] >= MATRIX_COLS) {
                return reply_status(CMD_GET_KEY, 0x01, response);
            }
            return reply_word(CMD_GET_KEY, dynamic_keymap_get_keycode(request[2], request[3], request[4]), response);

        case CMD_SET_KEY:
            if (request_len != 7) return 0;
            if (request[2] >= dynamic_keymap_get_layer_count() || request[3] >= MATRIX_ROWS || request[4] >= MATRIX_COLS) {
                return reply_status(CMD_SET_KEY, 0x01, response);
            }
            dynamic_keymap_set_keycode(request[2], request[3], request[4], (uint16_t)(((uint16_t)request[5] << 8) | request[6]));
            return reply_status(CMD_SET_KEY, 0x00, response);

        case CMD_RESET:
            if (request_len != 2) return 0;
            dynamic_keymap_reset();
            return reply_status(CMD_RESET, 0x00, response);

        default:
            return 0;
    }
}
