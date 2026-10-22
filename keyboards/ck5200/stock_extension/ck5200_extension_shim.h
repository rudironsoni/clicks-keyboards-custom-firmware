/*
 * CK-5200 stock-extension report-builder shim (offline research artifact).
 *
 * This is the code the patched V122 registration points at: the OEM
 * scanner calls it with the proven builder ABI
 *   (a0 = key-state object, a1 = matrix[6], a2 = 6)
 * (builder invocation 0x259E-0x25A6, pointer at scan_obj+0x18).
 *
 * Default device mode delegates to the original OEM builder. The enabled
 * research mode replaces keyboard-page output, with an eight-report FIFO.
 * Consumer/sticky behavior in enabled mode is NOT yet preserved. The host
 * keymap fixture is a keyboard-page model, not the complete OEM behavior.
 *
 * Host harness (CK_SHIM_HOST): ck_shim_process() runs the engine and
 * returns the report frame for assertion, with injectable time.
 * Device build: startup initializes extension SRAM. A separate timer
 * hook counts nominal 1 ms SysTick services and handles idle deadlines.
 * Both OEM companion submission and EP1 HID submission are distinct calls.
 *
 * The separate V122 profile executes CRT initialization and the bounded
 * scanner/report path. Full startup, IRQ/USB completion semantics, iAP2
 * delivery, and configuration/persistence remain outside that proof.
 * See keyboards/ck5200/docs/stock-extension-feasibility.md, section 11.
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
