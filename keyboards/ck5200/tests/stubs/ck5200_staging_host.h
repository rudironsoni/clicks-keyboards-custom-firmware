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

/* Fault-injection controls provided by the harness: simulate a power loss
 * inside the write of the page at staging_host_fail_at. */
typedef enum {
    STAGING_FAULT_NONE = 0,
    STAGING_FAULT_BEFORE_ERASE, /* write fails, page untouched */
    STAGING_FAULT_AFTER_ERASE,  /* page erased to all-ones, then "power lost" */
} staging_fault_phase_t;
extern staging_fault_phase_t staging_host_fault_phase;

/* Provided by the harness (test file): page-granular backend. */
bool staging_flash_write_page(uint32_t address, const uint8_t data[256]);
void staging_flash_read_page(uint32_t address, uint8_t out[256]);
