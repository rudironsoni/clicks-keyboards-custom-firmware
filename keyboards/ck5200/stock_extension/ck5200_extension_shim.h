/*
 * CK-5200 stock-extension report-builder shim (research artifact, v1).
 *
 * This is the code the patched V122 registration points at: the OEM
 * scanner calls it with the proven builder ABI
 *   (a0 = key-state object, a1 = matrix[6], a2 = 6)
 * (scan path 0x2536-0x2542 / 0x259E-0x25A6; feasibility doc section 5).
 *
 * The shim REPLACES the OEM report builder for the keyboard-page report:
 * it never calls the OEM builder, so an altered key replaces the OEM
 * action rather than emitting both. Positions at stock-default actions
 * produce stock-equivalent output through the engine's default keymap.
 *
 * Host harness (CK_SHIM_HOST): ck_shim_process() runs the engine and
 * returns the report frame for assertion, with injectable time.
 * Device build: ck_extension_builder() is the ABI entry; it reads the
 * OEM 1 ms tick as its time source and calls the OEM send routine.
 *
 * OPEN ITEMS pinned by the 2026-10-22 review, deliberately not guessed
 * here and required before any executable claim:
 *  - the exact OEM report framing consumed by the send routine at
 *    0x278C (the decoded constants are used; field [2] and the exact
 *    length formula at 0x2856-0x285E are not yet decoded);
 *  - the RAM address of the OEM 1 ms tick counter (device build uses a
 *    per-call placeholder counter marked CK_TIME_UNPINNED).
 * See keyboards/ck5200/docs/stock-extension-feasibility.md.
 */
#ifndef CK5200_EXTENSION_SHIM_H
#define CK5200_EXTENSION_SHIM_H

#include <stddef.h>
#include <stdint.h>

#include "ck5200_keymap_engine.h"

#define CK_FRAME_MAX 16

/* Decoded OEM report constants (V122 0x2796/0x2856 region). */
#define CK_FRAME_TYPE0 0x0B
#define CK_FRAME_TYPE1 0x02

typedef struct {
    uint8_t bytes[CK_FRAME_MAX];
    size_t len;
} ck_frame_t;

/* Core, host-testable: run one engine tick and assemble the frame. */
void ck_shim_process(ck_engine_t *e, const uint8_t matrix[CK_ROWS],
                     uint32_t now_ms, ck_frame_t *out);

#ifdef CK_SHIM_HOST
/* Host harness returns the engine's next deadline for scheduling tests. */
uint32_t ck_shim_next_deadline(const ck_engine_t *e);
#else
/* Device entry: the OEM builder ABI. never returns a value; the OEM
 * builder returns void. */
void ck_extension_builder(void *state_obj, const uint8_t *matrix, int size);
#endif

#endif /* CK5200_EXTENSION_SHIM_H */
