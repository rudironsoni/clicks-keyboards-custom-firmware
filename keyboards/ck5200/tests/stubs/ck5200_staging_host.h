/* Host-test seam for the real staging writer: the harness implements the
 * X035-constrained flash backend and the counters the driver reads. The
 * model reproduces the CH32X035 programming constraints from the EVT SDK
 * (ch32x035_flash.c): 256-byte pages, erase before every program, and
 * programming only clears bits. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Provided by the driver under test (ck5200_staging.c). */
extern unsigned staging_host_reboots;
extern unsigned staging_host_page_writes;
extern uint32_t staging_host_fail_at; /* page address whose write fails once */

/* Simulated MCU power cycle: clears the driver's volatile state while the
 * harness's flash array is preserved. Only available in host builds; the
 * real device gets this from power loss itself (review 2026-10-12,
 * finding 3). */
void ck5200_staging_test_power_cycle(void);

/* Fault-injection controls provided by the harness: simulate a power loss
 * inside the write of the page at staging_host_fail_at. */
typedef enum {
    STAGING_FAULT_NONE = 0,
    STAGING_FAULT_BEFORE_ERASE,   /* write fails, page untouched */
    STAGING_FAULT_AFTER_ERASE,    /* page erased to all-ones, then "power lost" */
    STAGING_FAULT_AFTER_PROGRAM_MARKER, /* page programmed through the marker word only */
    STAGING_FAULT_HALF_PROGRAMMED,       /* half the page programmed, rest erased */
} staging_fault_phase_t;
extern staging_fault_phase_t staging_host_fault_phase;

/* Provided by the harness (test file): page-granular backend. */
bool staging_flash_write_page(uint32_t address, const uint8_t data[256]);
void staging_flash_read_page(uint32_t address, uint8_t out[256]);
