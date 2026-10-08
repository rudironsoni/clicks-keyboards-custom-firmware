#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/platform/ch32v20x/ck5200_update_protocol.h"

static uint32_t next_offset;
static unsigned reboot_count;
static bool begin_cb(uint32_t size, uint32_t *maximum) {
    next_offset = 0;
    *maximum = CK5200_UPDATE_MAX_IMAGE;
    return size == 32;
}
static bool write_cb(uint32_t offset, const uint8_t *data, uint8_t length, uint32_t *accepted) {
    (void)data;
    if (offset != next_offset) { *accepted = next_offset; return false; }
    next_offset += length;
    *accepted = next_offset;
    return true;
}
static bool finish_cb(uint32_t size, uint32_t *committed) {
    *committed = size;
    return size == next_offset;
}
static void reboot_cb(void) { ++reboot_count; }

int main(void) {
    ck5200_update_ops_t ops = {begin_cb, write_cb, finish_cb, reboot_cb};
    uint8_t response[8];

    const uint8_t a1[] = {0x06,0xa1,0x00,0x00,0x00,0x20};
    assert(ck5200_update_handle(a1, sizeof(a1), response, &ops) == 8);
    assert(memcmp(response, (uint8_t[]){0x08,0x02,0xa1,0x00,0x00,0x00,0x6a,0x00}, 8) == 0);

    uint8_t a2[38] = {0x26,0xa2,0,0,0,0};
    for (int i=0;i<32;i++) a2[6+i]=(uint8_t)i;
    assert(ck5200_update_handle(a2, sizeof(a2), response, &ops) == 8);
    assert(memcmp(response, (uint8_t[]){0x08,0x02,0xa2,0x00,0x00,0x00,0x00,0x20}, 8) == 0);

    const uint8_t a3[] = {0x06,0xa3,0,0,0,0x20};
    assert(ck5200_update_handle(a3, sizeof(a3), response, &ops) == 8);
    assert(memcmp(response, (uint8_t[]){0x08,0x02,0xa3,0,0,0,0,0x20}, 8) == 0);
    const uint8_t a0[] = {0x02,0xa0};
    assert(ck5200_update_handle(a0, sizeof(a0), response, &ops) == 0);
    assert(reboot_count == 1);

    assert(ck5200_update_handle(a2, sizeof(a2), response, &ops) == 8);
    assert(memcmp(response, (uint8_t[]){0x08,0x02,0xa2,1,0,0,0,0x20}, 8) == 0);
    assert(next_offset == 32 && reboot_count == 1);

    puts("ok");
    return 0;
}
