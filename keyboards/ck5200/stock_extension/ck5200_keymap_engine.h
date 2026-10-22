/*
 * CK-5200 stock-extension keymap engine, version 2 (research artifact).
 *
 * Correctness milestone (2026-10-22 review) changes from version 1:
 *  - Per-layer actions: usage, modifiers, and activation are independently
 *    configurable on each of the three layers (version 1 shared modifier
 *    bits across layers, letting NAV assignments alter base typing).
 *  - A physical press resolves to ONE immutable action snapshot, held for
 *    the lifetime of that press; releases always clear exactly what the
 *    press contributed, across any layer transition.
 *  - Overlapping layer activators use a press-order stack: releasing one
 *    activator falls back to the next still-held activator.
 *  - All timing is elapsed milliseconds supplied by the caller, never
 *    callback counts (the OEM scanner callback is event-driven).
 *  - The engine exposes the next deadline so the shim can schedule a
 *    report update without waiting for another matrix event (a tap's
 *    release must not depend on a future keypress).
 *  - Matrix rows are masked to the six column bits; bits 0x40/0x80 are
 *    ignored rather than indexing out of bounds.
 *  - Keyboard-page usage validation: usages above 0xFF are invalid for
 *    this report path and are rejected at validation time.
 *
 * See keyboards/ck5200/docs/stock-extension-feasibility.md.
 */
#ifndef CK5200_KEYMAP_ENGINE_H
#define CK5200_KEYMAP_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#define CK_ROWS 6
#define CK_COLS 6
#define CK_KEYS 36
#define CK_LAYERS 3            /* base, sym, nav */
#define CK_REPORT_KEYS 6       /* usages per report */
#define CK_LAYER_BASE 0
#define CK_LAYER_SYM 1
#define CK_LAYER_NAV 2
#define CK_ROW_MASK 0x3F       /* the six column bits of a matrix row */

/* HID modifier bits (report byte 0). */
#define CK_MOD_LCTL 0x01
#define CK_MOD_LSHIFT 0x02
#define CK_MOD_LALT 0x04
#define CK_MOD_LGUI 0x08

#define CK_NO_DEADLINE UINT32_MAX

/* Layer activation modes for a layer action. */
typedef enum {
    CK_ACT_NONE = 0,   /* plain usage action */
    CK_ACT_TAP,        /* layer while held; quick solo release emits tap usage */
    CK_ACT_HOLD,       /* layer only while held */
    CK_ACT_TOGGLE,     /* each press flips the layer on/off */
    CK_ACT_ONESHOT     /* layer for the next non-layer press, then expires */
} ck_activation_t;

/* One action, configured per position and per layer. activation == NONE is
 * a plain usage action (usage + mods). Otherwise it is a layer action for
 * layer_switch with the given mode; usage then applies to nothing. */
typedef struct {
    uint16_t usage;        /* keyboard-page HID usage, 0 = none */
    uint8_t mods;          /* modifier bits held while this action is held */
    uint8_t activation;    /* ck_activation_t */
    uint8_t layer_switch;  /* target layer for layer actions */
    uint16_t tap_usage;     /* CK_ACT_TAP quick-release usage */
    uint16_t tap_ms;       /* CK_ACT_TAP window in elapsed ms */
} ck_action_t;

typedef struct {
    ck_action_t action[CK_LAYERS][CK_KEYS];
} ck_keymap_t;

static inline int ck_pos(int row, int col)
{
    return row * CK_COLS + col;
}

/* Action resolution with base transparency: an empty action on the
 * effective layer inherits the base-layer action. This matches the stock
 * ledger ("-" columns keep base behavior) and lets activators defined on
 * the base layer still work while another layer is active (a toggle key
 * must be able to switch its layer off). A non-empty action on any layer
 * shadows the base entry. */
const ck_action_t *ck_engine_resolve_action(const ck_keymap_t *map,
                                             int layer, int pos);

static inline int ck_col_of(uint8_t bit)
{
    int c = 0;
    while ((bit >>= 1) != 0)
        c++;
    return c;
}

/* One immutable action snapshot taken at press time. */
typedef struct {
    bool is_layer_action;
    uint16_t usage;
    uint8_t mods;
    uint8_t activation;
    uint8_t layer_switch;
    uint16_t tap_usage;
    uint32_t tap_deadline;  /* absolute ms for the TAP window close */
} ck_held_action_t;

typedef struct {
    const ck_keymap_t *map;

    uint8_t prev_matrix[CK_ROWS];

    /* Press-order stack of currently held layer activators. */
    int activator_stack[CK_KEYS];
    int activator_depth;

    int toggle_owner;          /* position owning the toggle, or -1 */
    bool oneshot_armed;
    int oneshot_from;          /* arming position, or -1 */
    uint32_t oneshot_deadline; /* absolute ms */

    ck_held_action_t held[CK_KEYS];
    bool held_active[CK_KEYS];
    bool tap_second_key;

    uint32_t now_ms;           /* last observed time */
    uint32_t last_press_ms;

    /* The pending TAP emission: after a quick tap, the tap usage must be
     * visible for one report and then clear on a TIME-DRIVEN follow-up
     * tick, without any matrix change. */
    int tap_fire_pos;          /* emitting position, or -1 */
    uint32_t tap_clear_deadline;
} ck_engine_t;

/* Validate a keymap for the keyboard-page report path. Returns 0 when
 * valid, otherwise a count of invalid entries (usages > 0xFF). Invalid
 * actions are ignored at runtime, so a malformed map can never emit a
 * corrupted report byte; this function exists for tooling and tests. */
int ck_keymap_validate(const ck_keymap_t *map);

void ck_engine_init(ck_engine_t *e, const ck_keymap_t *map);

/* Advance to time now_ms with the given matrix rows. Rows are masked to
 * CK_ROW_MASK internally. Outputs the recomputed report. */
void ck_engine_tick(ck_engine_t *e, const uint8_t matrix[CK_ROWS], uint32_t now_ms,
                    uint8_t *out_mods, uint8_t out_usages[CK_REPORT_KEYS],
                    uint8_t *out_count);

/* The next engine deadline (ms) at which a tick must be re-run even with
 * no matrix change, or CK_NO_DEADLINE. The shim uses this to schedule
 * tap-release and one-shot-expiry report updates. */
uint32_t ck_engine_next_deadline(const ck_engine_t *e);

#endif /* CK5200_KEYMAP_ENGINE_H */
