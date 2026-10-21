/*
 * CK-5200 stock-extension keymap engine implementation (research artifact).
 * Host-testable, pure C99. See the header for the contract and
 * keyboards/ck5200/docs/stock-extension-feasibility.md for the milestone.
 */
#include "ck5200_keymap_engine.h"

#include <string.h>

#define NO_POS 0xFFFFu

static const ck_key_def_t *def_of(const ck_engine_t *e, int pos)
{
    return &e->map->keys[pos];
}

static int toggle_target_layer(const ck_engine_t *e)
{
    if (e->toggle_owner < 0)
        return CK_LAYER_BASE;
    return def_of(e, e->toggle_owner)->layer_switch;
}

void ck_engine_init(ck_engine_t *e, const ck_keymap_t *map)
{
    memset(e, 0, sizeof(*e));
    e->map = map;
    e->active_layer = CK_LAYER_BASE;
    e->held_layer = -1;
    e->oneshot_from = -1;
    e->toggle_owner = -1;
    e->tap_hold_pos = NO_POS;
    e->tap_fire_pos = NO_POS;
    e->tap_default_ticks = 200;
    e->oneshot_ticks_left = 0;
}

static int effective_layer(const ck_engine_t *e)
{
    if (e->held_layer >= 0)
        return e->held_layer;
    if (e->toggle_owner >= 0)
        return toggle_target_layer(e);
    return e->active_layer;
}

static void build_report(ck_engine_t *e, uint8_t *mods,
                         uint8_t usages[CK_REPORT_KEYS], uint8_t *count)
{
    uint8_t m = 0;
    uint8_t n = 0;
    for (int pos = 0; pos < CK_KEYS; pos++) {
        if (!e->held_active[pos])
            continue;
        const ck_key_def_t *d = def_of(e, pos);
        m |= d->mods;
        uint16_t u = e->held_usage[pos];
        if (u != 0 && n < CK_REPORT_KEYS)
            usages[n++] = (uint8_t)u;
    }
    *mods = m;
    *count = n;
}

static void press(ck_engine_t *e, int pos)
{
    const ck_key_def_t *d = def_of(e, pos);
    e->held_active[pos] = true;

    if (d->activation != CK_ACT_NONE) {
        switch (d->activation) {
        case CK_ACT_TAP:
            /* Hold the target layer while pressed; arm the tap window. */
            e->held_layer = d->layer_switch;
            e->tap_hold_pos = (uint16_t)pos;
            e->tap_ticks_left = d->tap_ticks ? d->tap_ticks
                                             : e->tap_default_ticks;
            e->tap_second_key = false;
            break;
        case CK_ACT_HOLD:
            e->held_layer = d->layer_switch;
            break;
        case CK_ACT_TOGGLE:
            if (e->toggle_owner == pos)
                e->toggle_owner = -1;   /* same key toggles the layer off */
            else
                e->toggle_owner = pos;   /* press switches the layer on */
            break;
        case CK_ACT_ONESHOT:
            e->oneshot_armed = true;
            e->oneshot_from = pos;
            e->oneshot_ticks_left = 300; /* expiry window; re-press refreshes */
            break;
        default:
            break;
        }
        /* A layer key contributes no usage while held; TAP emits its
         * tap_usage only on a qualifying quick release. */
        e->held_usage[pos] = 0;
        return;
    }

    /* Plain key: resolve the layer for this press now and remember it so
     * the release is correct even after the layer later changes. */
    int layer = effective_layer(e);
    if (e->oneshot_armed) {
        layer = def_of(e, e->oneshot_from)->layer_switch;
        e->oneshot_armed = false;   /* consumed by the first press */
    }
    e->held_usage[pos] = d->usage[layer];
    if (e->tap_hold_pos != NO_POS)
        e->tap_second_key = true;  /* TAP must not fire after other keys */
}

static void release(ck_engine_t *e, int pos)
{
    const ck_key_def_t *d = def_of(e, pos);
    e->held_active[pos] = false;

    if (d->activation != CK_ACT_NONE) {
        switch (d->activation) {
        case CK_ACT_TAP: {
            bool quick = e->tap_hold_pos == (uint16_t)pos
                         && !e->tap_second_key && e->tap_ticks_left > 0;
            e->tap_hold_pos = NO_POS;
            e->tap_ticks_left = 0;
            e->held_layer = -1;
            if (quick && d->tap_usage != 0) {
                /* Emit the tap usage for exactly one report cycle. */
                e->held_active[pos] = true;
                e->held_usage[pos] = d->tap_usage;
                e->tap_fire_pos = (uint16_t)pos;
            }
            break;
        }
        case CK_ACT_HOLD:
            e->held_layer = -1;
            break;
        case CK_ACT_ONESHOT:
            /* The armed one-shot SURVIVES the release of its key: it is
             * consumed by the first subsequent press, or expires by
             * countdown. This is the defining one-shot semantic. */
            break;
        default:
            break;
        }
        return;
    }

    /* Plain key: drop exactly the usage this position contributed. */
    e->held_usage[pos] = 0;
}

void ck_engine_tick(ck_engine_t *e, const uint8_t matrix[CK_ROWS],
                    uint8_t *out_mods, uint8_t out_usages[CK_REPORT_KEYS],
                    uint8_t *out_count)
{
    /* Clear the previous tick's TAP emission exactly one cycle after it
     * fired (the position is not physically held anymore). */
    if (e->tap_fire_pos != NO_POS) {
        int pos = e->tap_fire_pos;
        e->held_active[pos] = false;
        e->held_usage[pos] = 0;
        e->tap_fire_pos = NO_POS;
    }

    if (e->tap_hold_pos != NO_POS && e->tap_ticks_left > 0)
        e->tap_ticks_left--;

    for (int row = 0; row < CK_ROWS; row++) {
        uint8_t now = matrix[row];
        uint8_t was = e->prev_matrix[row];
        for (uint8_t bit = 1, col = 0; bit != 0; bit <<= 1, col++) {
            bool p_now = (now & bit) != 0;
            bool p_was = (was & bit) != 0;
            int pos = ck_pos(row, col);
            if (p_now && !p_was)
                press(e, pos);
            else if (!p_now && p_was)
                release(e, pos);
        }
    }
    memcpy(e->prev_matrix, matrix, sizeof(e->prev_matrix));

    /* An armed, unconsumed one-shot expires by countdown once its key has
     * been released (the window refreshes while the key is held). */
    if (e->oneshot_armed && e->oneshot_from >= 0
        && !e->held_active[e->oneshot_from]) {
        if (e->oneshot_ticks_left > 0)
            e->oneshot_ticks_left--;
        if (e->oneshot_ticks_left == 0) {
            e->oneshot_armed = false;
            e->oneshot_from = -1;
        }
    }

    build_report(e, out_mods, out_usages, out_count);
}
