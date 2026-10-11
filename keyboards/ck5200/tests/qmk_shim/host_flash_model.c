/* Host model of the CH32 flash semantics for the wear-leveling store:
 * erased = 0xFF, programming ANDs bits (can only clear), 256-byte fast
 * page erase restores 0xFF. The production backing store addresses the
 * region absolutely (0x0800F800..0x0800FC00); this model maps those
 * addresses into the host buffer.
 */
#include "ch32v20x.h"
#include "ck5200_wl_host.h"
#include <stddef.h>
#include <string.h>

#define WEAR_REGION_BASE 0x0800F800u
#define WEAR_REGION_SIZE 1024u

uint8_t ck5200_wl_host_region[WEAR_REGION_SIZE];

static int page_erase_calls;
static int word_writes;

void host_flash_reset(void) {
    page_erase_calls = 0;
    word_writes = 0;
    memset(ck5200_wl_host_region, 0xFF, sizeof(ck5200_wl_host_region));
}
int host_flash_page_erase_calls(void) { return page_erase_calls; }
int host_flash_word_writes(void) { return word_writes; }

void FLASH_Unlock(void) {}
void FLASH_Lock(void) {}
void FLASH_Unlock_Fast(void) {}
void FLASH_Lock_Fast(void) {}

static int region_offset(uint32_t address, size_t *out) {
    if (address < WEAR_REGION_BASE || address >= WEAR_REGION_BASE + WEAR_REGION_SIZE) return 0;
    *out = (size_t)(address - WEAR_REGION_BASE);
    return 1;
}

FLASH_Status FLASH_ErasePage_Fast(uint32_t address) {
    size_t off;
    if (!region_offset(address, &off) || off % 256u != 0 || off + 256u > sizeof(ck5200_wl_host_region)) return FLASH_ERROR;
    page_erase_calls++;
    memset(ck5200_wl_host_region + off, 0xFF, 256);
    return FLASH_COMPLETE;
}

FLASH_Status FLASH_ProgramWord(uint32_t address, uint32_t value) {
    size_t off;
    if (!region_offset(address, &off) || off + 4u > sizeof(ck5200_wl_host_region) || off % 4u != 0) return FLASH_ERROR;
    word_writes++;
    uint32_t cur;
    memcpy(&cur, ck5200_wl_host_region + off, 4);
    const uint32_t next = cur & value; /* flash programming only clears bits */
    memcpy(ck5200_wl_host_region + off, &next, 4);
    return FLASH_COMPLETE;
}
