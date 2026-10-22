/*
 * CK-5200 stock-extension report-builder shim (offline research artifact).
 *
 * This is the code the patched V122 registration points at: the OEM
 * scanner calls it with the proven builder ABI
 *   (a0 = key-state object, a1 = matrix[6], a2 = 6)
 * (builder invocation 0x259E-0x25A6, pointer at scan_obj+0x18).
 *
 * Default device mode delegates to the original OEM builder. The enabled
 * research mode keeps OEM actions in the original policy and overlays
 * custom keyboard actions through an eight-report FIFO. A copied-state
 * preview removes the native activity marker's usage, not its latch effect.
 * Bounded native/sticky/consumer comparisons pass. Full IRQ and transport
 * compatibility remains unproven. The host fixture is not the OEM policy.
 *
 * Host harness (CK_SHIM_HOST): ck_shim_process() runs the engine and
 * returns the report frame for assertion, with injectable time.
 * Device build: startup initializes extension SRAM. A separate timer
 * hook counts nominal 1 ms SysTick services and handles idle deadlines.
 * Both OEM companion submission and EP1 HID submission are distinct calls.
 *
 * The separate V122 profile executes CRT initialization and the bounded
 * scanner/report path. IRQ/configuration cases also execute real SysTick
 * and USB completion handlers. Full startup, enumeration, iAP2 auth,
 * physical delivery, and persistence remain outside that proof.
 * Native SYM/NAV selects programmable layers by default when the engine
 * is enabled. Each native mode can be disabled. Custom activators win.
 * Offline RAM configuration executes through the OEM command adapter.
 * See keyboards/ck5200/docs/stock-extension-feasibility.md, section 14.
 */
#ifndef CK5200_EXTENSION_SHIM_H
#define CK5200_EXTENSION_SHIM_H

#include <stddef.h>
#include <stdint.h>

#include "ck5200_keymap_engine.h"

#define CK_FRAME_MAX 16
#define CK_FRAME_KEYS 4 /* V122 descriptor: four usages, then consumer bitmap */

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
