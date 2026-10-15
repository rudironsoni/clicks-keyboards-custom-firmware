#pragma once

#include "ck5200_update_protocol.h"

extern const ck5200_update_ops_t ck5200_staging_ops;

/* One-way restriction for this boot. Entry/abort only change RAM state. */
void ck5200_staging_require_stock(void);
void ck5200_staging_abort(void);
/* Refuse a rescue-entry reset while staging or an install marker is present. */
bool ck5200_staging_rescue_entry_allowed(void);

/* Runtime region guard: true only for an aligned 256-byte page fully
 * inside [0x08008A00, 0x0800F500). The installer region, application
 * region, serial page, stock-config page, and the reserved region from
 * 0x0800F800 are never writable (review 2026-10-12). */
bool ck5200_staging_page_in_range(uint32_t address);
