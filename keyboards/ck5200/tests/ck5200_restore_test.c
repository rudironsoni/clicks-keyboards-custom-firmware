/* Full restore-cycle harness: real update handler + real staging writer.
 *
 * Drives the complete stock update dialect (A1/A2/A3, then A0) against a
 * host flash backend that reproduces the CH32X035 programming constraints
 * from the EVT SDK (ch32x035_flash.c): 256-byte pages, erase before every
 * program, programming only clears bits. Pushes the complete stock V122
 * image and the complete custom image through the real state machine and
 * asserts the staged bytes equal the image exactly.
 *
 * Fault injection (review 2026-10-12 requirements):
 *  - a whole page write fails (before erase: page untouched);
 *  - a power loss after the erase, before the program: the page reads
 *    all-ones, which for the length marker is the factory state (no install
 *    pending);
 *  - both are exercised on the marker transactions (A1 invalidate, A3
 *    commit), which on this flash model are page read-modify-write
 *    operations over the first staging page;
 *  - every failure must leave the driver in a state a fresh A1 can recover
 *    from, and a retry cycle must complete.
 *
 * Region guard: the runtime bounds check is unit-tested over the exact
 * boundaries, so no custom-firmware page write can reach the installer,
 * the serial or configuration pages, or the reserved region from
 * 0x0800F800.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../firmware/platform/ch32x035/ck5200_update_protocol.h"
#include "../firmware/platform/ch32x035/ck5200_staging.h"
#include "ck5200_staging_host.h"

#define STAGING_BASE 0x08008A00u
#define STAGING_IMAGE_BASE (STAGING_BASE + 4u)
#define FLASH_PAGE_SIZE 256u
#define FLASH_SIZE 0x10000u

/* The X035-constrained host model. */
static uint8_t *flash; /* malloc'd fake flash, 0xFF = erased */
unsigned staging_host_reboots;
unsigned staging_host_page_writes;
uint32_t staging_host_fail_at;
staging_fault_phase_t staging_host_fault_phase = STAGING_FAULT_NONE;

bool staging_flash_write_page(uint32_t address, const uint8_t data[FLASH_PAGE_SIZE]) {
    assert(address >= 0x08000000u && address + FLASH_PAGE_SIZE <= 0x08000000u + FLASH_SIZE);
    uint8_t *model = flash + (address - 0x08000000u);

    if (address == staging_host_fail_at) {
        if (staging_host_fault_phase == STAGING_FAULT_BEFORE_ERASE) {
            staging_host_fail_at = 0xFFFFFFFFu; /* fails once, untouched */
            return false;
        }
        if (staging_host_fault_phase == STAGING_FAULT_AFTER_ERASE) {
            /* Power loss after the erase: the page reads all-ones. */
            staging_host_fail_at = 0xFFFFFFFFu;
            memset(model, 0xFF, FLASH_PAGE_SIZE);
            return false;
        }
        staging_host_fail_at = 0xFFFFFFFFu; /* whole-op failure, untouched */
        return false;
    }

    ++staging_host_page_writes;
    memset(model, 0xFF, FLASH_PAGE_SIZE);           /* erase */
    for (uint32_t i = 0; i < FLASH_PAGE_SIZE; ++i) /* program: only clears bits */
        model[i] &= data[i];
    return true;
}

void staging_flash_read_page(uint32_t address, uint8_t out[FLASH_PAGE_SIZE]) {
    assert(address >= 0x08000000u && address + FLASH_PAGE_SIZE <= 0x08000000u + FLASH_SIZE);
    memcpy(out, flash + (address - 0x08000000u), FLASH_PAGE_SIZE);
}

static uint32_t marker_read(void) {
    uint32_t v;
    memcpy(&v, flash + (STAGING_BASE - 0x08000000u), sizeof(v));
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
    staging_host_fault_phase = STAGING_FAULT_NONE;
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
    assert(marker_read() == image_len);
    assert(memcmp(flash + (STAGING_IMAGE_BASE - 0x08000000u), image, image_len) == 0);

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

    /* Region guard boundaries (review 2026-10-12, requirement 4). */
    assert(!ck5200_staging_page_in_range(0x08000000u)); /* installer: never */
    assert(!ck5200_staging_page_in_range(0x08002000u)); /* application: never */
    assert(ck5200_staging_page_in_range(0x08008A00u));  /* staging first page */
    assert(ck5200_staging_page_in_range(0x0800F400u));  /* staging last page */
    assert(!ck5200_staging_page_in_range(0x0800F500u)); /* past the staging limit */
    assert(!ck5200_staging_page_in_range(0x0800F600u)); /* serial page */
    assert(!ck5200_staging_page_in_range(0x0800F700u)); /* stock config page */
    assert(!ck5200_staging_page_in_range(0x0800F800u)); /* reserved: never */
    assert(!ck5200_staging_page_in_range(0x08008A01u)); /* unaligned page */
    puts("region guard ok");

    /* Restore stock onto custom: the exact cycle the phone app and the Mac
     * tool run. Reflash custom: the same handler in the other direction.
     * And back to stock once more, proving repeat cycles work. */
    full_cycle(stock, stock_len, true);
    full_cycle(custom, custom_len, true);
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

    /* Power loss AFTER the erase during the A1 marker invalidation: the
     * marker page reads all-ones, the factory state, so no install can be
     * pending; A1 fails and a fresh cycle recovers. */
    {
        memset(flash, 0xFF, FLASH_SIZE);
        staging_host_fail_at = STAGING_BASE;
        staging_host_fault_phase = STAGING_FAULT_AFTER_ERASE;
        const uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x08, 0x00};
        assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x01 && "A1 must fail when the invalidate page write loses power");
        assert(marker_read() == 0xFFFFFFFFu && "post-erase marker reads all-ones: factory state");
        const unsigned reboots = staging_host_reboots;
        full_cycle(stock, stock_len, true);
        assert(staging_host_reboots == reboots + 1 && "recovery cycle lost an A0");
        puts("A1 invalidate power-loss-after-erase ok");
    }

    /* Power loss BEFORE the erase during the A1 marker invalidation: the
     * previously committed marker survives untouched, the A1 fails, and a
     * fresh cycle rewrites everything. Note the surviving old marker is
     * the pre-existing stock-design window; the fresh A1's invalidate
     * closes it on the retry. */
    {
        memset(flash, 0xFF, FLASH_SIZE);
        full_cycle(custom, custom_len, true); /* leaves marker == custom_len */
        staging_host_fail_at = STAGING_BASE;
        staging_host_fault_phase = STAGING_FAULT_BEFORE_ERASE;
        const uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x08, 0x00};
        assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x01 && "A1 must fail when the invalidate page write fails");
        assert(marker_read() == (uint32_t)custom_len && "page untouched by the failed write");
        staging_host_fault_phase = STAGING_FAULT_NONE;
        full_cycle(stock, stock_len, true);
        puts("A1 invalidate write-failure ok");
    }

    /* Power loss AFTER the erase during the A3 marker commit: the first
     * staging page reads all-ones (the 252 payload bytes of that page are
     * lost, and no length is committed: no install can trigger), the commit
     * fails, and a fresh cycle recovers cleanly. */
    {
        memset(flash, 0xFF, FLASH_SIZE);
        staging_host_fault_phase = STAGING_FAULT_NONE;
        staging_host_fail_at = 0xFFFFFFFFu;
        const uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x00, 0x10};
        assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x00);
        /* 16 staged bytes: the first page does not flush, so the fault
         * stays armed for the A3 marker RMW over that page. */
        uint8_t a2[6 + 16];
        a2[0] = sizeof(a2);
        a2[1] = 0xA2;
        a2[2] = a2[3] = a2[4] = 0;
        a2[5] = 0;
        memset(a2 + 6, 0x55, 16);
        assert(ck5200_update_handle(a2, sizeof(a2), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x00);
        /* Arm the fault on the marker RMW the A3 commit performs. */
        staging_host_fail_at = STAGING_BASE;
        staging_host_fault_phase = STAGING_FAULT_AFTER_ERASE;
        const uint8_t a3[6] = {0x06, 0xA3, 0x00, 0x00, 0x00, 0x10};
        assert(ck5200_update_handle(a3, sizeof(a3), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x01 && "A3 must fail when the commit page write loses power");
        assert(marker_read() == 0xFFFFFFFFu && "no length committed after the power loss");
        staging_host_fault_phase = STAGING_FAULT_NONE;
        full_cycle(stock, stock_len, true);
        puts("A3 commit power-loss-after-erase ok");
    }

    /* Corrupted data (metadata lying about the length) never commits. */
    {
        memset(flash, 0xFF, FLASH_SIZE);
        const uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x08, 0x00};
        assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x00);
        /* A3 claiming a length that was never staged. */
        const uint8_t a3[6] = {0x06, 0xA3, 0x00, 0x00, 0x07, 0xFF};
        assert(ck5200_update_handle(a3, sizeof(a3), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x01 && "commit accepted a length that was never staged");
        /* The marker stays at its A1-invalidated value: no commit happened. */
        assert(marker_read() == 0u);
        puts("invalid metadata ok");
    }

    free(stock);
    free(custom);
    puts("ok 3");
    return 0;
}
