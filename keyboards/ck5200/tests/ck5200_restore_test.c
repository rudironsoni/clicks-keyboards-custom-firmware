/* Full restore-cycle harness: real update handler + real staging writer.
 *
 * Pushes the complete stock V122 image and the complete custom image
 * through the stock update dialect (A1/A2/A3, then A0), against a mocked
 * flash backend, and asserts the staged bytes equal the image exactly.
 * This is the restore path that runs when the phone app or the Mac tool
 * flashes the keyboard, so the test uses the same bytes a real restore
 * would: every chunk at the size the stock updater sends.
 *
 * Also covers the failure paths that must not corrupt or wedge staging:
 * wrong sizes, offset jumps, chunk overruns, and an injected flash page
 * failure mid-restore.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../firmware/platform/ch32v20x/ck5200_update_protocol.h"
#include "../firmware/platform/ch32v20x/ck5200_staging.h"
#include "ck5200_staging_host.h"

#define STAGING_BASE 0x08008A00u
#define STAGING_IMAGE_BASE (STAGING_BASE + 4u)
#define FLASH_PAGE_SIZE 256u
#define FLASH_SIZE 0x10000u

static uint8_t *flash; /* malloc'd fake flash, 0xFF = erased */
unsigned staging_host_reboots;
unsigned staging_host_page_writes;
uint32_t staging_host_fail_at;

bool staging_flash_page(uint32_t address, const uint8_t data[FLASH_PAGE_SIZE]) {
    assert(address >= STAGING_BASE && address + FLASH_PAGE_SIZE <= STAGING_BASE + FLASH_SIZE);
    ++staging_host_page_writes;
    if (address == staging_host_fail_at) {
        staging_host_fail_at = 0xFFFFFFFFu; /* fails once */
        return false;
    }
    memset(flash + (address - STAGING_BASE), 0xFF, FLASH_PAGE_SIZE);
    memcpy(flash + (address - STAGING_BASE), data, FLASH_PAGE_SIZE);
    return true;
}

bool staging_flash_word(uint32_t address, uint32_t value) {
    assert(address == STAGING_BASE);
    memcpy(flash + (address - STAGING_BASE), &value, sizeof(value));
    return true;
}

uint32_t staging_flash_read_word(uint32_t address) {
    assert(address == STAGING_BASE);
    uint32_t v;
    memcpy(&v, flash + (address - STAGING_BASE), sizeof(v));
    return v;
}

static uint8_t *load(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    assert(f && "image file missing");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    assert(n > 0);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    assert(buf && fread(buf, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *len = (size_t)n;
    return buf;
}

/* One complete flash cycle through the stock dialect. */
static void full_cycle(const uint8_t *image, size_t image_len, bool expect_ok) {
    memset(flash, 0xFF, FLASH_SIZE);
    staging_host_fail_at = 0xFFFFFFFFu;
    const unsigned reboots_before = staging_host_reboots;
    uint8_t response[8];

    /* A1: announce the size. */
    uint8_t a1[6] = {0x06, 0xA1,
                     (uint8_t)(image_len >> 24), (uint8_t)(image_len >> 16),
                     (uint8_t)(image_len >> 8), (uint8_t)image_len};
    assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
    assert(response[1] == 0x02 && response[2] == 0xA1);
    assert((response[3] == 0x00) == expect_ok);
    if (!expect_ok) return;

    /* A2: stream the image in stock-sized chunks. */
    uint8_t a2[CK5200_UPDATE_MAX_CHUNK + 6];
    uint32_t off = 0;
    while (off < image_len) {
        const uint8_t chunk = (uint8_t)((image_len - off > CK5200_UPDATE_MAX_CHUNK)
                                            ? CK5200_UPDATE_MAX_CHUNK
                                            : image_len - off);
        a2[0] = (uint8_t)(chunk + 6);
        a2[1] = 0xA2;
        a2[2] = (uint8_t)(off >> 24);
        a2[3] = (uint8_t)(off >> 16);
        a2[4] = (uint8_t)(off >> 8);
        a2[5] = (uint8_t)off;
        memcpy(a2 + 6, image + off, chunk);
        assert(ck5200_update_handle(a2, (size_t)a2[0], response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x00 && "A2 chunk rejected");
        const uint32_t accepted = ((uint32_t)response[4] << 24) | ((uint32_t)response[5] << 16) |
                                   ((uint32_t)response[6] << 8) | (uint32_t)response[7];
        assert(accepted == off + chunk && "accepted offset mismatch");
        off += chunk;
    }

    /* A3: commit. */
    uint8_t a3[6] = {0x06, 0xA3,
                     (uint8_t)(image_len >> 24), (uint8_t)(image_len >> 16),
                     (uint8_t)(image_len >> 8), (uint8_t)image_len};
    assert(ck5200_update_handle(a3, sizeof(a3), response, &ck5200_staging_ops) == 8);
    assert(response[2] == 0xA3 && response[3] == 0x00 && "A3 commit rejected");
    const uint32_t committed = ((uint32_t)response[4] << 24) | ((uint32_t)response[5] << 16) |
                               ((uint32_t)response[6] << 8) | (uint32_t)response[7];
    assert(committed == image_len);

    /* The length word is committed and the staged bytes match exactly. */
    assert(staging_flash_read_word(STAGING_BASE) == image_len);
    assert(memcmp(flash + (STAGING_IMAGE_BASE - STAGING_BASE), image, image_len) == 0);

    /* A0: reboot command reaches the driver exactly once per cycle. */
    const uint8_t a0[2] = {0x02, 0xA0};
    assert(ck5200_update_handle(a0, sizeof(a0), response, &ck5200_staging_ops) == 0);
    assert(staging_host_reboots == reboots_before + 1 && "A0 did not trigger the reset path");
}

int main(int argc, char **argv) {
    assert(argc == 3 && "usage: restore_test <stock.bin> <custom.bin>");
    flash = malloc(FLASH_SIZE);
    assert(flash);

    size_t stock_len, custom_len;
    uint8_t *stock = load(argv[1], &stock_len);
    uint8_t *custom = load(argv[2], &custom_len);
    assert(stock_len <= CK5200_UPDATE_MAX_IMAGE);
    assert(custom_len <= CK5200_UPDATE_MAX_IMAGE);

    /* Restore stock onto custom: the exact cycle the phone app and the
     * Mac tool run. */
    full_cycle(stock, stock_len, true);
    /* Reflash custom: the same handler in the other direction. */
    full_cycle(custom, custom_len, true);
    /* And back to stock once more, proving repeat cycles work. */
    full_cycle(stock, stock_len, true);
    printf("full cycles ok (stock %zu, custom %zu)\n", stock_len, custom_len);

    /* Refuse an empty image and one byte over the region limit. */
    uint8_t response[8];
    const uint8_t zero[] = {0x06, 0xA1, 0, 0, 0, 0};
    assert(ck5200_update_handle(zero, sizeof(zero), response, &ck5200_staging_ops) == 8);
    assert(response[3] == 0x01);
    const uint8_t over[] = {0x06, 0xA1, 0x00, 0x00, 0x6A, 0x01};
    assert(ck5200_update_handle(over, sizeof(over), response, &ck5200_staging_ops) == 8);
    assert(response[3] == 0x01);

    /* Offset jumps and overruns are rejected without corrupting state. */
    const uint8_t ok1[] = {0x06, 0xA1, 0x00, 0x00, 0x00, 0x40};
    assert(ck5200_update_handle(ok1, sizeof(ok1), response, &ck5200_staging_ops) == 8);
    assert(response[3] == 0x00);
    uint8_t jump[10] = {0x0A, 0xA2, 0, 0, 1, 0, 1, 2, 3, 4};
    assert(ck5200_update_handle(jump, sizeof(jump), response, &ck5200_staging_ops) == 8);
    assert(response[3] == 0x01 && "offset jump accepted");
    uint8_t overrun[38] = {0x26, 0xA2, 0, 0, 0x00, 0x38};
    memset(overrun + 6, 0xAA, 32);
    assert(ck5200_update_handle(overrun, sizeof(overrun), response, &ck5200_staging_ops) == 8);
    assert(response[3] == 0x01 && "chunk overrun accepted");
    puts("bounds and jumps ok");

    /* Injected page failure mid-restore: the cycle refuses to continue,
     * a fresh A1 restarts cleanly, and the retry succeeds. */
    memset(flash, 0xFF, FLASH_SIZE);
    staging_host_fail_at = STAGING_BASE + 4 * FLASH_PAGE_SIZE;
    uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x08, 0x00};
    assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
    assert(response[3] == 0x00);
    uint32_t off = 0;
    bool failed_seen = false;
    uint8_t a2[CK5200_UPDATE_MAX_CHUNK + 6];
    while (off < 0x800) {
        const uint8_t chunk = (uint8_t)((0x800 - off > CK5200_UPDATE_MAX_CHUNK)
                                            ? CK5200_UPDATE_MAX_CHUNK : 0x800 - off);
        a2[0] = (uint8_t)(chunk + 6);
        a2[1] = 0xA2;
        a2[2] = (uint8_t)(off >> 24); a2[3] = (uint8_t)(off >> 16);
        a2[4] = (uint8_t)(off >> 8);  a2[5] = (uint8_t)off;
        memset(a2 + 6, (int)(off ^ 0x5A), chunk);
        assert(ck5200_update_handle(a2, (size_t)a2[0], response, &ck5200_staging_ops) == 8);
        if (response[3] != 0x00) { failed_seen = true; break; }
        off += chunk;
    }
    assert(failed_seen && "injected page failure never surfaced");
    const unsigned reboots_after_failure = staging_host_reboots;
    full_cycle(stock, stock_len, true);
    assert(staging_host_reboots == reboots_after_failure + 1 && "retry cycle lost an A0");
    puts("failure injection ok");

    free(stock);
    free(custom);
    puts("ok 3");
    return 0;
}
