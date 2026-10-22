/*
 * CK-5200 stock-extension keymap engine, version 2 (research artifact).
 * See the header for the contract and the version-2 change list.
 */
#include "ck5200_keymap_engine.h"

#include <string.h>

static const ck_action_t *action_of(const ck_engine_t *e, int layer, int pos)
{
    return &e->map->action[layer][pos];
}

static bool action_is_empty(const ck_action_t *a)
{
    return a->usage == 0 && a->mods == 0 && a->activation == CK_ACT_NONE;
}

const ck_action_t *ck_engine_resolve_action(const ck_keymap_t *map,
                                             int layer, int pos)
{
    if (layer != CK_LAYER_BASE) {
        const ck_action_t *a = &map->action[layer][pos];
        if (!action_is_empty(a))
            return a;
    }
    return &map->action[CK_LAYER_BASE][pos];
}

int ck_keymap_validate(const ck_keymap_t *map)
{
    int invalid = 0;
    for (int layer = 0; layer < CK_LAYERS; layer++) {
        for (int pos = 0; pos < CK_KEYS; pos++) {
            const ck_action_t *a = &map->action[layer][pos];
            if (a->usage > 0xFF)
                invalid++;
            if (a->activation != CK_ACT_NONE && a->tap_usage > 0xFF)
                invalid++;
            if (a->activation != CK_ACT_NONE
                && a->layer_switch >= CK_LAYERS)
                invalid++;
        }
    }
    return invalid;
}

void ck_engine_init(ck_engine_t *e, const ck_keymap_t *map)
{
    memset(e, 0, sizeof(*e));
    e->map = map;
    e->toggle_owner = -1;
    e->oneshot_from = -1;
    e->tap_fire_pos = -1;
}

static int effective_layer(const ck_engine_t *e)
{
    if (e->activator_depth > 0)
        return action_of(e, CK_LAYER_BASE,
                         e->activator_stack[e->activator_depth - 1]
                        )->layer_switch;
    if (e->oneshot_armed)
        return action_of(e, CK_LAYER_BASE, e->oneshot_from)->layer_switch;
    if (e->toggle_owner >= 0)
        return action_of(e, CK_LAYER_BASE, e->toggle_owner)->layer_switch;
    return CK_LAYER_BASE;
}

static void push_activator(ck_engine_t *e, int pos)
{
    if (e->activator_depth < CK_KEYS)
        e->activator_stack[e->activator_depth++] = pos;
}

static void pop_activator(ck_engine_t *e, int pos)
{
    for (int i = 0; i < e->activator_depth; i++) {
        if (e->activator_stack[i] == pos) {
            for (int j = i; j < e->activator_depth - 1; j++)
                e->activator_stack[j] = e->activator_stack[j + 1];
            e->activator_depth--;
            return;
        }
    }
}

static void build_report(ck_engine_t *e, uint8_t *mods,
                         uint8_t usages[CK_REPORT_KEYS], uint8_t *count)
{
    uint8_t m = 0;
    uint8_t n = 0;
    for (int pos = 0; pos < CK_KEYS; pos++) {
        if (!e->held_active[pos])
            continue;
        m |= e->held[pos].mods;
        uint16_t u = e->held[pos].usage;
        /* Out-of-page usages are structurally impossible here because
         * presses snapshot validated actions; guard anyway so a malformed
         * map can never emit a corrupted byte. */
        if (u != 0 && u <= 0xFF && n < CK_REPORT_KEYS)
            usages[n++] = (uint8_t)u;
    }
    *mods = m;
    *count = n;
}

static void press(ck_engine_t *e, int pos, uint32_t now_ms)
{
    /* A new press of a position with a pending one-cycle tap emission
     * cancels that emission; the fresh press must not be clobbered by
     * the scheduled clear. */
    if (e->tap_fire_pos == pos) {
        e->tap_fire_pos = -1;
        e->held_active[pos] = false;
    }

    int layer = effective_layer(e);
    const ck_action_t *a = ck_engine_resolve_action(e->map, layer, pos);

    ck_held_action_t *h = &e->held[pos];
    e->held_active[pos] = true;
    h->is_layer_action = (a->activation != CK_ACT_NONE);
    h->usage = a->usage;
    h->mods = a->mods;            /* per-action modifiers: the version-1
                                     cross-layer leak is fixed here */
    h->activation = a->activation;
    h->layer_switch = a->layer_switch;
    h->tap_usage = a->tap_usage;
    h->tap_deadline = now_ms + (a->tap_ms ? a->tap_ms : 200);

    e->now_ms = now_ms;
    e->last_press_ms = now_ms;

    if (h->is_layer_action) {
        /* A layer action contributes no usage and no modifiers while
         * held; only its activation effect applies. The tap emission
         * (if it fires) sets usage explicitly at release time. */
        h->usage = 0;
        h->mods = 0;
        switch (a->activation) {
        case CK_ACT_TAP:
        case CK_ACT_HOLD:
            push_activator(e, pos);
            e->tap_second_key = false;
            break;
        case CK_ACT_TOGGLE:
            if (e->toggle_owner == pos)
                e->toggle_owner = -1;
            else
                e->toggle_owner = pos;
            break;
        case CK_ACT_ONESHOT:
            e->oneshot_armed = true;
            e->oneshot_from = pos;
            e->oneshot_deadline = now_ms + 1000;
            break;
        default:
            break;
        }
        return;
    }

    /* Plain key: consume the one-shot with this press, and mark that a
     * key event occurred inside any open TAP window (suppresses the tap
     * usage on release). */
    if (e->oneshot_armed) {
        e->oneshot_armed = false;
        e->oneshot_from = -1;
    }
    e->tap_second_key = true;
}

static void release(ck_engine_t *e, int pos, uint32_t now_ms)
{
    const ck_held_action_t *h = &e->held[pos];
    e->now_ms = now_ms;

    if (h->is_layer_action) {
        switch (h->activation) {
        case CK_ACT_TAP: {
            pop_activator(e, pos);
            bool quick = !e->tap_second_key && now_ms < h->tap_deadline
                         && h->tap_usage != 0;
            if (quick) {
                /* Emit the tap usage; a TIME-DRIVEN follow-up tick clears
                 * it (see ck_engine_next_deadline). */
                e->held_active[pos] = true;
                e->held[pos].usage = h->tap_usage;
                e->held[pos].mods = 0;
                e->held[pos].is_layer_action = false;
                e->tap_fire_pos = pos;
                e->tap_clear_deadline = now_ms + 1; /* one report cycle */
            } else {
                e->held_active[pos] = false;
            }
            break;
        }
        case CK_ACT_HOLD:
            pop_activator(e, pos);
            break;
        case CK_ACT_ONESHOT:
            /* The armed one-shot survives its release; expiry is
             * time-driven. */
            break;
        default:
            break;
        }
        return;
    }
    e->held_active[pos] = false;
}

void ck_engine_tick(ck_engine_t *e, const uint8_t matrix[CK_ROWS], uint32_t now_ms,
                    uint8_t *out_mods, uint8_t out_usages[CK_REPORT_KEYS],
                    uint8_t *out_count)
{
    e->now_ms = now_ms;

    /* Time-driven one-shot expiry: independent of matrix events. */
    if (e->oneshot_armed && now_ms >= e->oneshot_deadline) {
        e->oneshot_armed = false;
        e->oneshot_from = -1;
    }

    /* Time-driven tap-emission clear: the release report for a tap does
     * not wait for any future keypress. */
    if (e->tap_fire_pos >= 0 && now_ms >= e->tap_clear_deadline) {
        e->held_active[e->tap_fire_pos] = false;
        e->held[e->tap_fire_pos].usage = 0;
        e->tap_fire_pos = -1;
    }

    /* Process matrix changes; bits outside the six-column mask are
     * ignored so malformed rows cannot index out of bounds. */
    for (int row = 0; row < CK_ROWS; row++) {
        uint8_t now = matrix[row] & CK_ROW_MASK;
        uint8_t was = e->prev_matrix[row];
        if (now == was)
            continue;
        for (uint8_t bit = 1, col = 0; col < CK_COLS; bit <<= 1, col++) {
            bool p_now = (now & bit) != 0;
            bool p_was = (was & bit) != 0;
            int pos = ck_pos(row, col);
            if (p_now && !p_was)
                press(e, pos, now_ms);
            else if (!p_now && p_was)
                release(e, pos, now_ms);
        }
        e->prev_matrix[row] = now;
    }

    build_report(e, out_mods, out_usages, out_count);
}

uint32_t ck_engine_next_deadline(const ck_engine_t *e)
{
    uint32_t next = CK_NO_DEADLINE;
    if (e->tap_fire_pos >= 0 && e->tap_clear_deadline < next)
        next = e->tap_clear_deadline;
    if (e->oneshot_armed && e->oneshot_deadline < next)
        next = e->oneshot_deadline;
    /* Held TAP keys do not need a deadline: the quick/hold decision is
     * made at release time by comparing the release timestamp with the
     * press deadline, so no scheduled tick is required while held. */
    return next;
}
