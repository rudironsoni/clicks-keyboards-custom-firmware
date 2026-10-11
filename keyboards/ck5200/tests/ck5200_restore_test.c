/* Full restore-cycle harness: real update handler + real staging writer.
 *
 * Drives the complete stock update dialect (A1/A2/A3, then A0) against a
 * host flash backend that reproduces the CH32X035 programming constraints
 * from the EVT SDK (ch32x035_flash.c): 256-byte pages, erase before every
 * program, programming only clears bits. Pushes the complete stock V122
 * image and the complete custom image through the real state machine and
 * asserts the staged bytes equal the image exactly.
 *
 * Fault model (rebuilt after review 2026-10-12, findings 3 and 4). Device
 * state, MCU reset, and transaction execution are SEPARATED:
 *  - blank_flash(): simulated device fresh from the factory (all 0xFF).
 *  - power_cycle(): simulated MCU reset: the driver's volatile state is
 *    cleared while the flash array is preserved. Recovery tests run
 *    against the damage left by the previous transaction, never over a
 *    re-blanked device.
 *  - run_cycle(): one A1/A2/A3/(A0) transaction over whatever flash
 *    contents exist.
 * Injected power-loss phases, at the page write chosen by
 * staging_host_fail_at:
 *  - BEFORE_ERASE: the write never starts; the page is untouched.
 *  - AFTER_ERASE: the page is erased to all-ones and the write stops; for
 *    the length marker this is the factory state (no install pending).
 *  - AFTER_PROGRAM_MARKER: the page is programmed through the marker word
 *    only (bytes 0..3), then the write stops: a plausible-looking marker
 *    paired with erased payload.
 *  - HALF_PROGRAMMED: half the page is programmed, half stays erased.
 * The last two model the worst case the SDK's busy-wait page program
 * cannot exclude (ch32x035_flash.c exposes no status checking); the
 * analysis and its limits are asserted explicitly below.
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

/* The X035-constrained host model. The staging_host_* counters and the
 * fault address are DEFINED by the driver under test (ck5200_staging.c,
 * CK5200_STAGING_HOST_TEST); the harness only declares them via
 * ck5200_staging_host.h. Defining them here too broke Linux builds with
 * -fno-common (review 2026-10-12, finding 2: multiple-definition link
 * errors in CI run 38147753811). The fault phase lives in the harness
 * because the driver never reads it. */
static uint8_t *flash; /* malloc'd fake flash, 0xFF = erased */
staging_fault_phase_t staging_host_fault_phase = STAGING_FAULT_NONE;

bool staging_flash_write_page(uint32_t address, const uint8_t data[FLASH_PAGE_SIZE]) {
    assert(address >= 0x08000000u && address + FLASH_PAGE_SIZE <= 0x08000000u + FLASH_SIZE);
    uint8_t *model = flash + (address - 0x08000000u);

    if (address == staging_host_fail_at) {
        const staging_fault_phase_t phase = staging_host_fault_phase;
        staging_host_fail_at = 0xFFFFFFFFu; /* every fault fires once */
        if (phase == STAGING_FAULT_BEFORE_ERASE) {
            return false; /* untouched */
        }
        if (phase == STAGING_FAULT_AFTER_ERASE) {
            memset(model, 0xFF, FLASH_PAGE_SIZE); /* factory state */
            return false;
        }
        if (phase == STAGING_FAULT_AFTER_PROGRAM_MARKER) {
            /* Programmed through the marker word only: a plausible marker
             * over erased payload. Cannot be excluded by the SDK's API
             * (no status checking on the page-program command). */
            memset(model, 0xFF, FLASH_PAGE_SIZE);
            for (uint32_t i = 0; i < 4; ++i) model[i] &= data[i];
            return false;
        }
        if (phase == STAGING_FAULT_HALF_PROGRAMMED) {
            memset(model, 0xFF, FLASH_PAGE_SIZE);
            for (uint32_t i = 0; i < FLASH_PAGE_SIZE / 2; ++i) model[i] &= data[i];
            return false;
        }
        return false; /* whole-op failure, untouched */
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

/* Simulated MCU reset: volatile driver state cleared, flash preserved. */
static void power_cycle(void) {
    staging_host_fail_at = 0xFFFFFFFFu;
    staging_host_fault_phase = STAGING_FAULT_NONE;
    ck5200_staging_test_power_cycle();
}

/* Simulated fresh device. Only for factory-state tests; recovery tests
 * must NOT call this (review 2026-10-12, finding 3). */
static void blank_flash(void) {
    memset(flash, 0xFF, FLASH_SIZE);
    power_cycle();
}

/* One complete update cycle through the stock dialect, executed over
 * whatever flash contents currently exist. */
static void run_cycle(const uint8_t *image, size_t image_len, bool expect_ok) {
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
    uint8_t response[8];

    /* ---- Region guard boundaries, including the overflow case ------- */
    assert(!ck5200_staging_page_in_range(0x08000000u)); /* installer: never */
    assert(!ck5200_staging_page_in_range(0x08002000u)); /* application: never */
    assert(ck5200_staging_page_in_range(0x08008A00u));  /* staging first page */
    assert(ck5200_staging_page_in_range(0x0800F400u));  /* staging last page */
    assert(!ck5200_staging_page_in_range(0x0800F500u)); /* past the staging limit */
    assert(!ck5200_staging_page_in_range(0x0800F600u)); /* serial page */
    assert(!ck5200_staging_page_in_range(0x0800F700u)); /* stock config page */
    assert(!ck5200_staging_page_in_range(0x0800F800u)); /* reserved: never */
    assert(!ck5200_staging_page_in_range(0x08008A01u)); /* unaligned page */
    /* The add-based bound wrapped for addresses near UINT32_MAX
     * (0xFFFFFF00 + 256 == 0): must be rejected by the subtraction-based
     * guard (review 2026-10-12, finding 6). */
    assert(!ck5200_staging_page_in_range(0xFFFFFF00u));
    assert(!ck5200_staging_page_in_range(0xFFFFFFFFu));
    assert(!ck5200_staging_page_in_range(0xFFFF0000u));
    for (uint32_t probe = 0; probe < 4096; ++probe) {
        const uint32_t addr = 0x08010000u + (probe * 0x1000u);
        assert(!ck5200_staging_page_in_range(addr));
    }
    puts("region guard ok");

    /* ---- Factory-state cycles (blank device) ------------------------- */
    blank_flash();
    run_cycle(stock, stock_len, true);
    run_cycle(custom, custom_len, true);
    run_cycle(stock, stock_len, true);
    printf("chained cycles without re-blanking ok (stock %zu, custom %zu)\n",
           stock_len, custom_len);

    /* ---- Bounds rejections over existing staging contents ------------ */
    const uint8_t zero[] = {0x06, 0xA1, 0, 0, 0, 0};
    assert(ck5200_update_handle(zero, sizeof(zero), response, &ck5200_staging_ops) == 8);
    assert(response[3] == 0x01);
    const uint8_t over[] = {0x06, 0xA1, 0x00, 0x00, 0x6A, 0x01};
    assert(ck5200_update_handle(over, sizeof(over), response, &ck5200_staging_ops) == 8);
    assert(response[3] == 0x01);
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

    /* ---- Power loss during A1 invalidation (recovery over damage) ----- */

    /* The bounds-rejection block above left an open transaction with the
     * marker invalidated; complete a cycle first so a previously valid
     * marker really exists for the recovery tests to recover over. */
    power_cycle();
    run_cycle(stock, stock_len, true);

    /* AFTER the erase, with a PREVIOUS VALID MARKER still committed from
     * the last successful cycle: the erase destroys the old marker and
     * payload bytes of that page, A1 fails, and after a power cycle the
     * next cycle must succeed over the retained damaged flash (finding 3:
     * no re-blanking between damage and recovery). */
    {
        assert(marker_read() == (uint32_t)stock_len); /* previous cycle committed */
        staging_host_fail_at = STAGING_BASE;
        staging_host_fault_phase = STAGING_FAULT_AFTER_ERASE;
        const uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x08, 0x00};
        assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x01 && "A1 must fail when the invalidate page write loses power");
        assert(marker_read() == 0xFFFFFFFFu && "post-erase marker reads all-ones: factory state");
        power_cycle();
        const unsigned reboots = staging_host_reboots;
        run_cycle(custom, custom_len, true); /* recovery over retained damage */
        assert(staging_host_reboots == reboots + 1 && "recovery cycle lost an A0");
        puts("A1 invalidate power-loss-after-erase recovery over damage ok");
    }

    /* BEFORE the erase: the previously committed marker survives
     * untouched, A1 fails, and the power-cycled recovery must overwrite
     * it (the surviving valid marker is the pre-existing stock-design
     * window; the fresh A1's invalidate closes it on the retry). */
    {
        assert(marker_read() == (uint32_t)custom_len);
        staging_host_fail_at = STAGING_BASE;
        staging_host_fault_phase = STAGING_FAULT_BEFORE_ERASE;
        const uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x08, 0x00};
        assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x01 && "A1 must fail when the invalidate page write fails");
        assert(marker_read() == (uint32_t)custom_len && "page untouched by the failed write");
        power_cycle();
        run_cycle(stock, stock_len, true); /* recovery with the old marker intact */
        puts("A1 invalidate write-failure recovery with retained marker ok");
    }

    /* ---- Power loss during the A3 commit (recovery over damage) ------ */

    /* AFTER the erase: the whole first staging page (marker + 252 payload
     * bytes) reads all-ones, no install can trigger, the commit fails,
     * and the power-cycled recovery cycle succeeds over the damaged flash. */
    {
        blank_flash();
        const uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x00, 0x10};
        assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x00);
        uint8_t a2[6 + 16];
        a2[0] = sizeof(a2);
        a2[1] = 0xA2;
        a2[2] = a2[3] = a2[4] = 0;
        a2[5] = 0;
        memset(a2 + 6, 0x55, 16);
        assert(ck5200_update_handle(a2, sizeof(a2), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x00);
        staging_host_fail_at = STAGING_BASE;
        staging_host_fault_phase = STAGING_FAULT_AFTER_ERASE;
        const uint8_t a3[6] = {0x06, 0xA3, 0x00, 0x00, 0x00, 0x10};
        assert(ck5200_update_handle(a3, sizeof(a3), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x01 && "A3 must fail when the commit page write loses power");
        assert(marker_read() == 0xFFFFFFFFu && "no length committed after the power loss");
        power_cycle();
        run_cycle(stock, stock_len, true);
        puts("A3 commit power-loss-after-erase recovery over damage ok");
    }

    /* ---- Partial-page-program analysis (finding 4) ------------------- *
     *
     * The SDK's fast page program (FLASH_ErasePage_Fast / BufLoad /
     * ProgramPage_Fast) is a busy-wait sequence with no status checking
     * (ch32x035_flash.c: void returns, while(FLASH->STATR & SR_BSY) loops).
     * Whether the controller can write part of a page and stop is a
     * hardware property no host model can prove or exclude. The two
     * cases below model the worst case and assert what our firmware can
     * and cannot do about it; the residual risk is stated explicitly. */

    /* Case 1: the marker word programs but the payload does not. The page
     * now carries a PLAUSIBLE marker over erased payload. Our firmware's
     * A1 invalidation on the next boot clears it, BUT the boot-region
     * installer runs before our firmware: if the installer treats the
     * marker as install-pending, it copies an erased payload. This is
     * the documented residual risk: it cannot be excluded without
     * decoding the installer, and the stock updater shares the same
     * window (stock commits through the same page-RMW shape at 0x31b2). */
    {
        blank_flash();
        /* Stage 320 bytes so the first staging page flushes during A2 and
         * the final partial page lands on the SECOND page: the armed
         * fault then fires exactly on the marker RMW at the first page. */
        const uint32_t total = 320;
        const uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x01, 0x40};
        assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x00);
        for (uint32_t off = 0; off < total; off += 32) {
            uint8_t a2[6 + 32];
            const uint8_t chunk = (uint8_t)((total - off > 32) ? 32 : total - off);
            a2[0] = (uint8_t)(chunk + 6);
            a2[1] = 0xA2;
            a2[2] = a2[3] = 0;
            a2[4] = (uint8_t)(off >> 8);
            a2[5] = (uint8_t)off;
            memset(a2 + 6, 0x55, chunk);
            assert(ck5200_update_handle(a2, (size_t)a2[0], response, &ck5200_staging_ops) == 8);
            assert(response[3] == 0x00);
        }
        staging_host_fail_at = STAGING_BASE;
        staging_host_fault_phase = STAGING_FAULT_AFTER_PROGRAM_MARKER;
        const uint8_t a3[6] = {0x06, 0xA3, 0x00, 0x00, 0x01, 0x40};
        assert(ck5200_update_handle(a3, sizeof(a3), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x01 && "A3 reports failure");
        assert(marker_read() == 0x00000140u && "worst case confirmed: plausible marker survived");
        assert(flash[(STAGING_IMAGE_BASE - 0x08000000u)] == 0xFF && "payload erased under the marker");
        /* What our firmware can do: a fresh A1 (after the power cycle,
         * IF our firmware is the code that runs) invalidates the marker
         * and rewrites the page. */
        power_cycle();
        const uint8_t a1b[6] = {0x06, 0xA1, 0x00, 0x00, 0x08, 0x00};
        assert(ck5200_update_handle(a1b, sizeof(a1b), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x00);
        assert(marker_read() == 0u && "A1 invalidation clears the plausible marker");
        run_cycle(stock, stock_len, true);
        puts("A3 partial-program worst case: modeled, cleared by A1, residual risk stated");
    }

    /* Case 2: half the page programs. Same residual: the marker (word 0)
     * is inside the programmed half, so the state is indistinguishable
     * from case 1 from the installer's point of view. */
    {
        blank_flash();
        const uint32_t total = 320;
        const uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x01, 0x40};
        assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
        for (uint32_t off = 0; off < total; off += 32) {
            uint8_t a2[6 + 32];
            const uint8_t chunk = (uint8_t)((total - off > 32) ? 32 : total - off);
            a2[0] = (uint8_t)(chunk + 6);
            a2[1] = 0xA2;
            a2[2] = a2[3] = 0;
            a2[4] = (uint8_t)(off >> 8);
            a2[5] = (uint8_t)off;
            memset(a2 + 6, 0x55, chunk);
            assert(ck5200_update_handle(a2, (size_t)a2[0], response, &ck5200_staging_ops) == 8);
            assert(response[3] == 0x00);
        }
        staging_host_fail_at = STAGING_BASE;
        staging_host_fault_phase = STAGING_FAULT_HALF_PROGRAMMED;
        const uint8_t a3[6] = {0x06, 0xA3, 0x00, 0x00, 0x01, 0x40};
        assert(ck5200_update_handle(a3, sizeof(a3), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x01);
        assert(marker_read() == 0x00000140u && "marker in the programmed half: plausible");
        power_cycle();
        const uint8_t a1b[6] = {0x06, 0xA1, 0x00, 0x00, 0x08, 0x00};
        assert(ck5200_update_handle(a1b, sizeof(a1b), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x00 && "recovery A1 succeeds over the half-programmed page");
        assert(marker_read() == 0u);
        puts("A3 half-programmed worst case: modeled and recoverable by A1");
    }

    /* ---- Invalid metadata over existing contents ---------------------- */
    {
        const uint8_t a1[6] = {0x06, 0xA1, 0x00, 0x00, 0x08, 0x00};
        assert(ck5200_update_handle(a1, sizeof(a1), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x00);
        const uint8_t a3[6] = {0x06, 0xA3, 0x00, 0x00, 0x07, 0xFF};
        assert(ck5200_update_handle(a3, sizeof(a3), response, &ck5200_staging_ops) == 8);
        assert(response[3] == 0x01 && "commit accepted a length that was never staged");
        assert(marker_read() == 0u);
        power_cycle();
        run_cycle(stock, stock_len, true);
        puts("invalid metadata recovery ok");
    }

    /* ---- Final chained cycle without re-blanking ---------------------- */
    run_cycle(custom, custom_len, true);
    assert(marker_read() == (uint32_t)custom_len);
    run_cycle(stock, stock_len, true);
    assert(marker_read() == (uint32_t)stock_len);
    puts("final chained stock/custom/stock over retained flash ok");

    free(stock);
    free(custom);
    puts("ok 3");
    return 0;
}
