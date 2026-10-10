#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Handles one custom-firmware request in the stock framing
 * ([len, cmd, ...]) and fills response[] with
 * [len, 0x02, cmd, status, payload...]. Returns the response length,
 * or 0 when the request is not a keymap command.
 *
 * Keycodes and multi-byte payloads are big-endian, matching the stock
 * protocol and QMK's dynamic keymap EEPROM layout.
 */
size_t ck5200_keymap_handle(const uint8_t *request, size_t request_len, uint8_t response[8]);
