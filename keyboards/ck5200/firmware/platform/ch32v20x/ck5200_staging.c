/*
 * CH32V20x implementation of the CK-5200 staging layout recovered from
 * stock firmware 1.2.2.
 *
 * Physical flash layout used by the stock updater:
 *   0x08008A00  uint32 little-endian staged image length
 *   0x08008A04  staged image byte 0
 *
 * The image maximum is 0x6A00 bytes and stock code programs 256-byte pages.
 */
#include "ck5200_staging.h"

#include <string.h>
#include "ch32v20x.h"
#include "ch32v20x_flash.h"

#define STAGING_BASE       0x08008A00u
#define STAGING_IMAGE_BASE (STAGING_BASE + 4u)
#define FLASH_PAGE_SIZE    256u

static uint32_t expected_size;
static uint32_t next_offset;
static uint32_t page_base;
static uint16_t page_fill;
static uint8_t page[FLASH_PAGE_SIZE] __attribute__((aligned(4)));

static bool flash_page(uint32_t address, const uint8_t data[FLASH_PAGE_SIZE]) {
    FLASH_Unlock_Fast();
    FLASH_ErasePage_Fast(address);
    FLASH_ProgramPage_Fast(address, (uint32_t *)(uintptr_t)data);
    FLASH_Lock_Fast();
    return memcmp((const void *)(uintptr_t)address, data, FLASH_PAGE_SIZE) == 0;
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
    *maximum = CK5200_UPDATE_MAX_IMAGE;

    /* First page reserves bytes 0..3 for the length committed by A3. */
    reset_page(STAGING_BASE);
    page_fill = 4;
    return true;
}

static bool flush_if_full(void) {
    if (page_fill != FLASH_PAGE_SIZE) return true;
    if (!flash_page(page_base, page)) return false;
    reset_page(page_base + FLASH_PAGE_SIZE);
    return true;
}

static bool staging_write(
    uint32_t offset,
    const uint8_t *data,
    uint8_t length,
    uint32_t *accepted_offset
) {
    if (!data || !accepted_offset || length == 0 || length > CK5200_UPDATE_MAX_CHUNK) return false;
    if (expected_size == 0 || offset != next_offset || offset + length > expected_size) {
        *accepted_offset = next_offset;
        return false;
    }

    for (uint8_t i = 0; i < length; ++i) {
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
    if (!committed_size || size != expected_size || next_offset != expected_size) return false;

    if (page_fill != 0) {
        if (!flash_page(page_base, page)) return false;
    }

    /* Word was left erased (0xFFFFFFFF) when the first page was programmed. */
    FLASH_Unlock();
    const FLASH_Status status = FLASH_ProgramWord(STAGING_BASE, size);
    FLASH_Lock();
    if (status != FLASH_COMPLETE || *(volatile const uint32_t *)STAGING_BASE != size) return false;

    *committed_size = size;
    expected_size = 0;
    return true;
}

static void staging_reboot(void) {
    NVIC_SystemReset();
    for (;;) {}
}

const ck5200_update_ops_t ck5200_staging_ops = {
    .begin = staging_begin,
    .write = staging_write,
    .finish = staging_finish,
    .reboot = staging_reboot,
};
