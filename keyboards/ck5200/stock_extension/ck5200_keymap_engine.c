/*
 * CK-5200 stock-extension keymap engine, version 2 (research artifact).
 * See the header for the contract and the version-2 change list.
 */
#include "ck5200_keymap_engine.h"

#include <string.h>

const ck_action_t *ck_engine_resolve_action(const ck_keymap_t *map,
                                             int layer, int pos)
{
    if (layer != CK_LAYER_BASE) {
        const ck_action_t *a = &map->action[layer][pos];
        if (a->kind != CK_ACTION_TRANSPARENT)
            return a;
    }
    return &map->action[CK_LAYER_BASE][pos];
}

int ck_keymap_validate(const ck_keymap_t *map)
{
    int invalid = (map->native_mask & ~CK_NATIVE_LAYERS) != 0;
    for (int layer = 0; layer < CK_LAYERS; layer++) {
        for (int pos = 0; pos < CK_KEYS; pos++) {
            const ck_action_t *a = &map->action[layer][pos];
            if (a->kind > CK_ACTION_OEM
                || a->activation > CK_ACT_ONESHOT)
                invalid++;
            if (a->kind != CK_ACTION_KEY
                && (a->usage || a->mods || a->activation || a->layer_switch
                    || a->tap_usage || a->tap_ms))
                invalid++;
            if (a->usage > 0xFF)
                invalid++;
            if (a->mods & 0xF0)
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
}

bool ck_engine_custom_layer(const ck_engine_t *e)
{
    return e->activator_depth > 0 || e->oneshot_armed || e->toggle_owner >= 0;
}

int ck_engine_layer(const ck_engine_t *e)
{
    if (e->activator_depth > 0)
        return e->held[e->activator_stack[e->activator_depth - 1]].layer_switch;
    if (e->oneshot_armed)
        return e->oneshot_layer;
    if (e->toggle_owner >= 0)
        return e->toggle_layer;
    uint8_t native = e->native_layers & e->map->native_mask;
    if (native & (1u << CK_LAYER_NAV))
        return CK_LAYER_NAV;
    if (native & (1u << CK_LAYER_SYM))
        return CK_LAYER_SYM;
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

static void press(ck_engine_t *e, int pos, uint32_t now_ms, const ck_action_t *a)
{
    /* A new press of a position with a pending one-cycle tap emission
     * cancels that emission; the fresh press must not be clobbered by
     * the scheduled clear. */
    if (e->held[pos].tap_pending) {
        e->held[pos].tap_pending = false;
        e->held_active[pos] = false;
    }

    int layer = ck_engine_layer(e);
    if (!a)
        a = ck_engine_resolve_action(e->map, layer, pos);
    bool native_owner = !ck_engine_custom_layer(e)
        && (e->native_owners[pos / CK_COLS] & (1u << (pos % CK_COLS))) != 0;
    static const ck_action_t native_action = {.kind = CK_ACTION_OEM};
    if (native_owner)
        a = &native_action;

    /* A second activator also interrupts an earlier TAP. A later TAP
     * must not clear the earlier key's interruption state. */
    for (int i = 0; i < CK_KEYS; i++)
        if (e->held_active[i])
            e->held[i].tap_interrupted = true;
    if ((a->kind != CK_ACTION_KEY && a->kind != CK_ACTION_OEM) || a->usage > 0xFF
        || (a->mods & 0xF0)
        || a->activation > CK_ACT_ONESHOT
        || (a->activation && (a->layer_switch >= CK_LAYERS
                             || a->tap_usage > 0xFF)))
        return;

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
    h->tap_interrupted = false;
    h->tap_pending = false;
    h->oem = a->kind == CK_ACTION_OEM;
    h->native_projection = h->oem && !native_owner && ck_engine_custom_layer(e) ? layer : -1;

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
            break;
        case CK_ACT_TOGGLE:
            if (e->toggle_owner == pos)
                e->toggle_owner = -1;
            else {
                e->toggle_owner = pos;
                e->toggle_layer = h->layer_switch;
            }
            break;
        case CK_ACT_ONESHOT:
            e->oneshot_armed = true;
            e->oneshot_from = pos;
            e->oneshot_layer = h->layer_switch;
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
    if (e->oneshot_armed && !native_owner) {
        e->oneshot_armed = false;
        e->oneshot_from = -1;
    }
}

static void release(ck_engine_t *e, int pos, uint32_t now_ms)
{
    const ck_held_action_t *h = &e->held[pos];
    e->now_ms = now_ms;

    if (h->is_layer_action) {
        switch (h->activation) {
        case CK_ACT_TAP: {
            pop_activator(e, pos);
            bool quick = !h->tap_interrupted && now_ms < h->tap_deadline
                         && h->tap_usage != 0;
            if (quick) {
                /* Emit the tap usage; a TIME-DRIVEN follow-up tick clears
                 * it (see ck_engine_next_deadline). */
                e->held_active[pos] = true;
                e->held[pos].usage = h->tap_usage;
                e->held[pos].mods = 0;
                e->held[pos].is_layer_action = false;
                e->held[pos].tap_pending = true;
                e->held[pos].tap_deadline = now_ms + 1; /* one report cycle */
            } else {
                e->held_active[pos] = false;
            }
            break;
        }
        case CK_ACT_HOLD:
            pop_activator(e, pos);
            e->held_active[pos] = false;
            break;
        case CK_ACT_ONESHOT:
            /* The armed one-shot survives its release; expiry is
             * time-driven. */
            e->held_active[pos] = false;
            break;
        default:
            e->held_active[pos] = false;
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
    for (int pos = 0; pos < CK_KEYS; pos++)
        if (e->held[pos].tap_pending && now_ms >= e->held[pos].tap_deadline) {
            e->held_active[pos] = false;
            e->held[pos].usage = 0;
            e->held[pos].tap_pending = false;
        }

    /* Releases use their snapshots first. Activators visible in the
     * initial layer then precede ordinary presses in the same scan.
     * Each phase uses row/column order, independent of key location. */
    uint8_t activations[CK_ROWS] = {0};
    for (int row = 0; row < CK_ROWS; row++) {
        uint8_t now = matrix[row] & CK_ROW_MASK;
        uint8_t was = e->prev_matrix[row];
        for (int col = 0; col < CK_COLS; col++)
            if ((was & (1u << col)) && !(now & (1u << col)))
                release(e, ck_pos(row, col), now_ms);
    }
    int initial_layer = ck_engine_layer(e);
    for (int row = 0; row < CK_ROWS; row++)
        for (int col = 0; col < CK_COLS; col++) {
            uint8_t bit = 1u << col;
            const ck_action_t *a = ck_engine_resolve_action(e->map, initial_layer, ck_pos(row, col));
            if ((matrix[row] & bit) && !(e->prev_matrix[row] & bit)
                && !(e->native_owners[row] & bit) && a->kind == CK_ACTION_KEY && a->activation)
                activations[row] |= bit;
        }
    for (int phase = 0; phase < 2; phase++)
        for (int row = 0; row < CK_ROWS; row++)
            for (int col = 0; col < CK_COLS; col++) {
                uint8_t bit = 1u << col;
                if ((matrix[row] & bit) && !(e->prev_matrix[row] & bit)
                    && ((activations[row] & bit) != 0) == (phase == 0))
                    press(e, ck_pos(row, col), now_ms, phase == 0
                        ? ck_engine_resolve_action(e->map, initial_layer, ck_pos(row, col)) : NULL);
            }
    for (int row = 0; row < CK_ROWS; row++) {
        e->prev_matrix[row] = matrix[row] & CK_ROW_MASK;
    }

    build_report(e, out_mods, out_usages, out_count);
    memset(e->native_owners, 0, CK_ROWS);
}

uint32_t ck_engine_next_deadline(const ck_engine_t *e)
{
    uint32_t next = CK_NO_DEADLINE;
    for (int pos = 0; pos < CK_KEYS; pos++)
        if (e->held[pos].tap_pending && e->held[pos].tap_deadline < next)
            next = e->held[pos].tap_deadline;
    if (e->oneshot_armed && e->oneshot_deadline < next)
        next = e->oneshot_deadline;
    /* Held TAP keys do not need a deadline: the quick/hold decision is
     * made at release time by comparing the release timestamp with the
     * press deadline, so no scheduled tick is required while held. */
    return next;
}

void ck_engine_oem_matrix(const ck_engine_t *e, uint8_t matrix[CK_ROWS])
{
    memset(matrix, 0, CK_ROWS);
    for (int pos = 0; pos < CK_KEYS; pos++)
        if (e->held_active[pos] && e->held[pos].oem)
            matrix[pos / CK_COLS] |= 1u << (pos % CK_COLS);
}
