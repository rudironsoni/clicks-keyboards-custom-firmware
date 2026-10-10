/* Host-test seam for the real staging writer: the harness implements the
 * flash backend and the counters the driver reads. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Provided by the driver under test (ck5200_staging.c). */
extern unsigned staging_host_reboots;
extern unsigned staging_host_page_writes;
extern uint32_t staging_host_fail_at; /* page address that fails once */

/* Provided by the harness (test file). */
bool     staging_flash_page(uint32_t address, const uint8_t data[256]);
bool     staging_flash_word(uint32_t address, uint32_t value);
uint32_t staging_flash_read_word(uint32_t address);
