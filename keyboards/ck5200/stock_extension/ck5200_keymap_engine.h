/*
 * CK-5200 stock-extension keymap engine (research artifact).
 *
 * Three-layer, 36-position keymap processing for the offline programmable
 * stock-extension feasibility milestone. See
 * keyboards/ck5200/docs/stock-extension-feasibility.md.
 *
 * The engine models the report-builder callback that the patched V122
 * registration would redirect to an extension: consume the 6-byte matrix
 * rows, apply a keymap record, and produce a boot-keyboard HID report
 * (modifier bitmask + up to 6 usages) with correct press/release tracking
 * and configurable layer activation (tap, hold, toggle, one-shot).
 *
 * Pure C99, no hardware or USB dependencies, host-testable.
 * NOT part of any installable image; installation is gated by R3.
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

/* HID modifier bits (report byte 0). */
#define CK_MOD_LCTL 0x01
#define CK_MOD_LSHIFT 0x02
#define CK_MOD_LALT 0x04
#define CK_MOD_LGUI 0x08

/* Layer activation modes for a layer-switch position. */
typedef enum {
    CK_ACT_NONE = 0,   /* position does not switch layers */
    CK_ACT_TAP,        /* layer while held; a quick solo release emits tap_usage */
    CK_ACT_HOLD,       /* layer only while held */
    CK_ACT_TOGGLE,     /* each press flips the layer on/off */
    CK_ACT_ONESHOT     /* layer for the next non-layer key press, then revert */
} ck_activation_t;

/* Per-position definition. usage == 0 means the position emits nothing on
 * that layer. Modifier flags apply on every layer while the position is
 * held (they compose with usages from other positions). */
typedef struct {
    uint16_t usage[CK_LAYERS]; /* HID usage per layer (0 = none) */
    uint8_t mods;              /* modifier bits held while pressed */
    uint8_t layer_switch;      /* 0 = none, else target layer index */
    uint8_t activation;        /* ck_activation_t for layer_switch */
    uint16_t tap_usage;        /* emitted on a CK_ACT_TAP quick release */
    uint16_t tap_ticks;        /* CK_ACT_TAP window length in ticks */
} ck_key_def_t;

typedef struct {
    ck_key_def_t keys[CK_KEYS];
} ck_keymap_t;

/* Keymap indices: row-major over matrix bytes, bit b = column.
 * ck_pos(r, b) matches the capability ledger's r/b notation. */
static inline int ck_pos(int row, uint8_t bit_index)
{
    return row * CK_COLS + bit_index;
}

/* Column of a matrix byte for bit 0x01,0x02,0x04,0x08,0x10,0x20. */
static inline int ck_col_of(uint8_t bit)
{
    int c = 0;
    while ((bit >>= 1) != 0)
        c++;
    return c;
}

typedef struct {
    /* Configuration. */
    const ck_keymap_t *map;
    uint16_t tap_default_ticks; /* default CK_ACT_TAP window */

    /* Runtime state. */
    uint8_t prev_matrix[CK_ROWS];
    int active_layer;          /* effective layer for NEW presses */
    int held_layer;            /* layer forced by a held TAP/HOLD key, or -1 */
    bool toggle_layer_active;  /* a TOGGLE key currently holds the layer */
    bool oneshot_armed;        /* next non-layer press uses held_layer's target */
    int oneshot_from;          /* position index of the pending toggle, or -1 */
    uint16_t oneshot_ticks_left; /* ticks before an unconsumed one-shot expires */
    int toggle_owner;          /* position owning the toggle, or -1 */

    /* Pressed-position bookkeeping: the usage each held position currently
     * contributes, so releases are correct even if the layer changed since
     * the press (no stuck keys after layer switches). */
    uint16_t held_usage[CK_KEYS];
    bool held_active[CK_KEYS];
    uint16_t tap_hold_pos;     /* position index of an armed TAP, or -1 sentinel */
    uint16_t tap_ticks_left;
    bool tap_second_key;       /* another key pressed during the TAP window */
    uint16_t tap_fire_pos;     /* position emitting a one-cycle TAP usage, or 0xFFFF */
} ck_engine_t;

void ck_engine_init(ck_engine_t *e, const ck_keymap_t *map);

/* Advance one tick. Reads the current matrix rows, recomputes the report
 * from scratch, and updates engine state. Outputs the modifier bitmask and
 * the (up to CK_REPORT_KEYS) usages currently held. */
void ck_engine_tick(ck_engine_t *e, const uint8_t matrix[CK_ROWS],
                    uint8_t *out_mods, uint8_t out_usages[CK_REPORT_KEYS],
                    uint8_t *out_count);

#endif /* CK5200_KEYMAP_ENGINE_H */
