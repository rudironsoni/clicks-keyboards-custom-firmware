/*
 * QMK wear-leveling backing store for the CK-5200 CH32V20x internal flash.
 *
 * Four 256-byte pages at 0x0800F800: clear of the stock staging region
 * (<= 0x0800F400) and the stock configuration pages (0x0800F600/0x0800F700),
 * and inside the smallest plausible part size (64 KiB).
 *
 * Upstream contract (quantum/wear_leveling/wear_leveling.c): reads from a
 * freshly erased store must return zero. This flash erases to all-ones, so
 * the store programs the complement and reads it back inverted, exactly
 * like the upstream RP2040 flash backend. The old direct encoding made
 * every boot read 0xFFFFFFFF as a corrupt log head and force a full
 * erase-plus-consolidation of the store (review 2026-10-11, F4).
 */
#include <stdbool.h>
#include <stdint.h>

#include "ch32v20x.h"
#include "ch32v20x_flash.h"
#include "wear_leveling.h"
#include "wear_leveling_internal.h"

#define BACKING_BASE 0x0800F800u
#define FLASH_PAGE_SIZE 256u

#ifdef CK5200_WL_HOST_TEST
/* Host build: the backing region is a plain buffer standing in for the
 * flash at 0x0800F800; the harness flash model maps the absolute flash
 * addresses into that buffer (reviewer harness, 2026-10-11). */
#include "ck5200_wl_host.h"
#define WL_READ_WORD(address) (*(backing_store_int_t *)(ck5200_wl_host_region + (address)))
#else
#define WL_READ_WORD(address) (*(volatile backing_store_int_t *)(BACKING_BASE + (address)))
#endif

bool backing_store_init(void) { return true; }

bool backing_store_unlock(void) {
    FLASH_Unlock();
    FLASH_Unlock_Fast();
    return true;
}

bool backing_store_lock(void) {
    FLASH_Lock();
    FLASH_Lock_Fast();
    return true;
}

bool backing_store_erase(void) {
    for (uint32_t at = 0; at < WEAR_LEVELING_BACKING_SIZE; at += FLASH_PAGE_SIZE) {
        FLASH_ErasePage_Fast(BACKING_BASE + at);
    }
    /* FLASH_ErasePage_Fast returns no status (the vendored SDK returns
     * void), so verify the erase directly: every word must read erased. */
    for (uint32_t at = 0; at < WEAR_LEVELING_BACKING_SIZE; at += sizeof(backing_store_int_t)) {
        if (WL_READ_WORD(at) != ~(backing_store_int_t)0) return false;
    }
    return true;
}

bool backing_store_write(uint32_t address, backing_store_int_t value) {
    if (address > WEAR_LEVELING_BACKING_SIZE - sizeof(value)) return false;
    if (address % sizeof(value) != 0) return false;
    const backing_store_int_t raw = ~value;
    if (FLASH_ProgramWord(BACKING_BASE + address, raw) != FLASH_COMPLETE) return false;
    return WL_READ_WORD(address) == raw;
}

bool backing_store_read(uint32_t address, backing_store_int_t *value) {
    if (!value || address > WEAR_LEVELING_BACKING_SIZE - sizeof(*value)) return false;
    *value = ~WL_READ_WORD(address);
    return true;
}
