#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CK5200_UPDATE_MAX_IMAGE 0x6A00u
#define CK5200_UPDATE_MAX_CHUNK 32u

typedef struct {
    bool (*begin)(uint32_t size, uint32_t *maximum);
    bool (*write)(uint32_t offset, const uint8_t *data, uint8_t length, uint32_t *accepted_offset);
    bool (*finish)(uint32_t size, uint32_t *committed_size);
    void (*reboot)(void);
} ck5200_update_ops_t;

/*
 * Handles one complete request from the stock CK-5200 updater protocol.
 * A1/A2/A3 return an 8-byte response in response[]. A0 returns zero bytes.
 */
size_t ck5200_update_handle(
    const uint8_t *request,
    size_t request_len,
    uint8_t response[8],
    const ck5200_update_ops_t *ops
);
