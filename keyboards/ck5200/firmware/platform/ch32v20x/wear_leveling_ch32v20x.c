/*
 * QMK wear-leveling backing store for the CK-5200 CH32V20x internal flash.
 *
 * Four 256-byte pages at 0x0800F800: clear of the stock staging region
 * (<= 0x0800F400) and the stock configuration pages (0x0800F600/0x0800F700),
 * and inside the smallest plausible part size (64 KiB).
 */
#include <stdbool.h>
#include <stdint.h>

#include "ch32v20x.h"
#include "ch32v20x_flash.h"
#include "wear_leveling.h"
#include "wear_leveling_internal.h"

#define BACKING_BASE 0x0800F800u
#define FLASH_PAGE_SIZE 256u

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
    return true;
}

bool backing_store_write(uint32_t address, backing_store_int_t value) {
    if (address > WEAR_LEVELING_BACKING_SIZE - sizeof(value)) return false;
    if (address % sizeof(value) != 0) return false;
    if (FLASH_ProgramWord(BACKING_BASE + address, value) != FLASH_COMPLETE) return false;
    return *(volatile backing_store_int_t *)(BACKING_BASE + address) == value;
}

bool backing_store_read(uint32_t address, backing_store_int_t *value) {
    if (!value || address > WEAR_LEVELING_BACKING_SIZE - sizeof(*value)) return false;
    *value = *(volatile backing_store_int_t *)(BACKING_BASE + address);
    return true;
}
