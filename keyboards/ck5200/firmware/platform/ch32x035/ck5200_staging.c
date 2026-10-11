/*
 * CH32X20x/X035 implementation of the CK-5200 staging layout recovered from
 * stock firmware 1.2.2.
 *
 * Physical flash layout used by the stock updater:
 *   0x08008A00  uint32 little-endian staged image length
 *   0x08008A04  staged image byte 0
 *
 * The image maximum is 0x6A00 bytes, so staged bytes end by 0x0800F404 and
 * page writes end by 0x0800F500. CH32X035 flash constraints (reference
 * manual ch. 20; EVT SDK ch32x035_flash.c): 256-byte pages, fast-mode
 * programming only (page buffer load + page program; the SDK exposes no
 * word-programming API), programming only clears bits, erase sets a page
 * back to all-ones, and the SDK's own driver bounds code flash at
 * FLASH_BASE + 0xF800.
 *
 * Consequence for the length marker: it shares its page with the first 252
 * bytes of the staged payload, so both invalidation (A1) and commit (A3) are
 * page read-modify-write transactions. Power-loss windows, from safest to
 * worst: after the erase the page reads all-ones, which is the factory
 * state of the marker (no pending install); a power loss during the
 * hardware page-program step leaves a partially programmed marker, which is
 * the documented residual risk of the stock update design (stock's own A3
 * commits through the same page-RMW shape, stock 0x31b2/0x3174).
 *
 * Runtime region guard: every page write must land inside
 * [0x08008A00, 0x0800F500). The serial page (0x0800F600), stock config
 * (0x0800F700), and the reserved region from 0x0800F800 are unreachable by
 * construction and by the ck5200_staging_page_in_range check (review
 * 2026-10-12, requirement: no custom-firmware operation may touch the
 * installer, configuration, serial, or reserved regions).
 *
 * CK5200_STAGING_HOST_TEST swaps the page backend for harness hooks so the
 * real page/offset state machine, including the marker RMW logic and
 * full-image restore cycles, can be driven end-to-end on the host,
 * including fault injection at every erase/program boundary.
 */
#include "ck5200_staging.h"

#include <string.h>

#ifdef CK5200_STAGING_HOST_TEST
/* Harness provides the flash backend. */
#include "ck5200_staging_host.h"
#define STAGING_BASE 0x08008A00u
#else
#include "ch32x035.h"
#include "ch32x035_flash.h"
#define STAGING_BASE 0x08008A00u
#endif

#define STAGING_IMAGE_BASE (STAGING_BASE + 4u)
#define FLASH_PAGE_SIZE    256u

/* First page the maximum image can reach: 0x08008A04 + 0x6A00 rounds up to
 * a page write ending exactly at 0x0800F500. */
#define STAGING_WRITE_LIMIT 0x0800F500u

typedef enum {
    STAGING_IDLE,
    STAGING_WRITING,
    STAGING_FAILED,
} staging_state_t;

static uint32_t expected_size;
static uint32_t next_offset;
static uint32_t page_base;
static uint16_t page_fill;
static staging_state_t staging_state;
static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));

#ifdef CK5200_STAGING_HOST_TEST
unsigned staging_host_reboots;
unsigned staging_host_page_writes;
uint32_t staging_host_fail_at = 0xFFFFFFFFu; /* address that fails once */

static void reset_page(uint32_t address);

/* Simulated MCU power cycle for the harness: volatile state cleared, the
 * physical flash array untouched (review 2026-10-12, finding 3). */
void ck5200_staging_test_power_cycle(void) {
    expected_size = 0;
    next_offset = 0;
    page_fill = 0;
    staging_state = STAGING_IDLE;
    reset_page(STAGING_BASE);
}
#endif

/* ---- runtime region guard ------------------------------------------ */

bool ck5200_staging_page_in_range(uint32_t address) {
    /* Upper bound by subtraction only: an addition-based check wraps for
     * addresses near UINT32_MAX (0xFFFFFF00 + 256 == 0x100 wraps to 0 and
     * would pass an add-based check; review 2026-10-12, finding 6). Both
     * bounds are small constants, so this subtraction cannot wrap. */
    return address >= STAGING_BASE &&
           address <= STAGING_WRITE_LIMIT - FLASH_PAGE_SIZE &&
           (address & (FLASH_PAGE_SIZE - 1u)) == 0u;
}

/* ---- page backend --------------------------------------------------- */

static bool staging_read_page(uint32_t address, uint8_t out[FLASH_PAGE_SIZE]) {
    if (!ck5200_staging_page_in_range(address)) return false;
#ifdef CK5200_STAGING_HOST_TEST
    staging_flash_read_page(address, out);
#else
    memcpy(out, (const void *)(uintptr_t)address, FLASH_PAGE_SIZE);
#endif
    return true;
}

static bool staging_write_page(uint32_t address, const uint8_t data[FLASH_PAGE_SIZE]) {
    if (!ck5200_staging_page_in_range(address)) return false;
#ifdef CK5200_STAGING_HOST_TEST
    return staging_flash_write_page(address, data);
#else
    /* CH32X035 fast mode (EVT SDK ch32x035_flash.c): unlock, erase the page,
     * load the 256-byte page buffer word by word, program, lock, and verify
     * by read-back. Programming only clears bits, so the erase is mandatory
     * before every program. */
    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(address);
    FLASH_BufReset();
    for (uint32_t at = 0; at < FLASH_PAGE_SIZE; at += 4u) {
        uint32_t word;
        memcpy(&word, data + at, sizeof(word));
        FLASH_BufLoad(address + at, word);
    }
    FLASH_ProgramPage_Fast(address);
    FLASH_Lock_Fast();
    return memcmp((const void *)(uintptr_t)address, data, FLASH_PAGE_SIZE) == 0;
#endif
}

/* ---- length-marker transactions (page RMW) -------------------------- */

/*
 * The committed length word is the installer's install-pending flag. Both
 * of its writes are page read-modify-write transactions over the first
 * staging page, which the X035 flash model requires (no word programming).
 * The little-endian layout matches the stock updater (recovered from the
 * V122 image) and the existing host model.
 */
static bool marker_write(uint32_t value) {
    uint8_t page_buf[FLASH_PAGE_SIZE];
    if (!staging_read_page(STAGING_BASE, page_buf)) return false;

    memcpy(page_buf, &value, sizeof(value));
    if (!staging_write_page(STAGING_BASE, page_buf)) return false;

    uint8_t verify[FLASH_PAGE_SIZE];
    if (!staging_read_page(STAGING_BASE, verify)) return false;
    uint32_t readback;
    memcpy(&readback, verify, sizeof(readback));
    return readback == value;
}

static void reset_page(uint32_t address) {
    page_base = address;
    page_fill = 0;
    memset(page, 0xFF, sizeof(page));
}

static bool staging_begin(uint32_t size, uint32_t *maximum) {
    if (!maximum || size == 0 || size > CK5200_UPDATE_MAX_IMAGE) return false;
    /* Invalidate any previously committed length before accepting data: an
     * interrupted cycle must never leave a valid length pointing at a
     * partially rewritten staging area (review 2026-10-11, F-3). On the
     * X035 page model this is a page RMW whose power-loss intermediate
     * (an erased marker, all-ones) is the factory state, i.e. no install
     * pending. */
    if (!marker_write(0)) return false;
    expected_size = size;
    next_offset = 0;
    staging_state = STAGING_WRITING;
    *maximum = CK5200_UPDATE_MAX_IMAGE;

    /* First page reserves bytes 0..3 for the length committed by A3. */
    reset_page(STAGING_BASE);
    page_fill = 4;
    return true;
}

static bool flush_if_full(void) {
    if (page_fill != FLASH_PAGE_SIZE) return true;
    if (!staging_write_page(page_base, page)) {
        staging_state = STAGING_FAILED;
        return false;
    }
    reset_page(page_base + FLASH_PAGE_SIZE);
    return true;
}

static bool staging_write(
    uint32_t offset,
    const uint8_t *data,
    uint8_t length,
    uint32_t *accepted_offset
) {
    if (!accepted_offset) return false;
    *accepted_offset = next_offset;
    if (!data || length == 0 || length > CK5200_UPDATE_MAX_CHUNK || staging_state != STAGING_WRITING) {
        return false;
    }
    if (offset != next_offset || offset > expected_size || length > expected_size - offset) {
        *accepted_offset = next_offset;
        return false;
    }

    for (uint8_t i = 0; i < length; ++i) {
        if (page_fill >= FLASH_PAGE_SIZE) {
            staging_state = STAGING_FAILED;
            *accepted_offset = next_offset;
            return false;
        }
        page[page_fill++] = data[i];
        ++next_offset;
        if (!flush_if_full()) {
            *accepted_offset = next_offset;
            return false;
        }
    }
    *accepted_offset = next_offset;
    return true;
}

static bool staging_finish(uint32_t size, uint32_t *committed_size) {
    if (!committed_size) return false;
    *committed_size = 0;
    if (staging_state != STAGING_WRITING || size != expected_size || next_offset != expected_size) return false;

    if (page_fill != 0) {
        if (!staging_write_page(page_base, page)) {
            staging_state = STAGING_FAILED;
            return false;
        }
    }

    /* Commit the length over the first staging page (RMW preserving the
     * 252 staged payload bytes), then verify. */
    if (!marker_write(size)) {
        staging_state = STAGING_FAILED;
        return false;
    }

    *committed_size = size;
    expected_size = 0;
    next_offset = 0;
    page_fill = 0;
    staging_state = STAGING_IDLE;
    return true;
}

static void staging_reboot(void) {
#ifdef CK5200_STAGING_HOST_TEST
    ++staging_host_reboots;
    return;
#else
    NVIC_SystemReset();
#endif
    for (;;) {}
}

const ck5200_update_ops_t ck5200_staging_ops = {
    .begin = staging_begin,
    .write = staging_write,
    .finish = staging_finish,
    .reboot = staging_reboot,
};
