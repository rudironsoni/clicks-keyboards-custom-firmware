/*
 * CH32V20x implementation of the CK-5200 staging layout recovered from
 * stock firmware 1.2.2.
 *
 * Physical flash layout used by the stock updater:
 *   0x08008A00  uint32 little-endian staged image length
 *   0x08008A04  staged image byte 0
 *
 * The image maximum is 0x6A00 bytes and stock code programs 256-byte pages.
 *
 * CK5200_STAGING_HOST_TEST swaps the flash backend for harness hooks so
 * the real page/offset state machine can be driven end-to-end on the
 * host, including full-image restore cycles.
 */
#include "ck5200_staging.h"

#include <string.h>

#ifdef CK5200_STAGING_HOST_TEST
/* Harness provides the flash backend. */
#include "ck5200_staging_host.h"
#define STAGING_BASE 0x08008A00u
#else
#include "ch32v20x.h"
#include "ch32v20x_flash.h"
#define STAGING_BASE 0x08008A00u
#endif

#define STAGING_IMAGE_BASE (STAGING_BASE + 4u)
#define FLASH_PAGE_SIZE    256u

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
#endif

static bool flash_page(uint32_t address, const uint8_t data[FLASH_PAGE_SIZE]) {
#ifdef CK5200_STAGING_HOST_TEST
    return staging_flash_page(address, data);
#else
    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(address);
    FLASH_ProgramPage_Fast(address, (uint32_t *)(uintptr_t)data);
    FLASH_Lock_Fast();
    return memcmp((const void *)(uintptr_t)address, data, FLASH_PAGE_SIZE) == 0;
#endif
}

static void reset_page(uint32_t address) {
    page_base = address;
    page_fill = 0;
    memset(page, 0xFF, sizeof(page));
}

static bool staging_begin(uint32_t size, uint32_t *maximum) {
    if (!maximum || size == 0 || size > CK5200_UPDATE_MAX_IMAGE) return false;
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
    if (!flash_page(page_base, page)) {
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
        if (!flash_page(page_base, page)) {
            staging_state = STAGING_FAILED;
            return false;
        }
    }

    /* Word was left erased (0xFFFFFFFF) when the first page was programmed. */
#ifdef CK5200_STAGING_HOST_TEST
    if (!staging_flash_word(STAGING_BASE, size)) {
        staging_state = STAGING_FAILED;
        return false;
    }
    if (staging_flash_read_word(STAGING_BASE) != size) {
        staging_state = STAGING_FAILED;
        return false;
    }
#else
    FLASH_Unlock();
    const FLASH_Status status = FLASH_ProgramWord(STAGING_BASE, size);
    FLASH_Lock();
    if (status != FLASH_COMPLETE || *(volatile const uint32_t *)STAGING_BASE != size) {
        staging_state = STAGING_FAILED;
        return false;
    }
#endif

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
