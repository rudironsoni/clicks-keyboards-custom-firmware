/*
 * Behavioral tests for the CK-5200 stock-extension keymap engine v2.
 * Offline research artifact for the correctness-and-integration milestone.
 * See keyboards/ck5200/docs/stock-extension-feasibility.md.
 *
 * Compile and run (host, no deps):
 *   cc -std=c99 -Wall -Werror -Os -I stock_extension \
 *      stock_extension/ck5200_keymap_engine.c \
 *      tests/ck5200_keymap_engine_test.c -o /tmp/keymap_engine_test
 *   /tmp/keymap_engine_test
 */
#include "ck5200_keymap_engine.h"

#include <stdio.h>
#include <string.h>

enum {
    U_NONE = 0x00,
    U_A = 0x04, U_B = 0x05, U_C = 0x06, U_D = 0x07, U_E = 0x08, U_F = 0x09,
    U_G = 0x0A, U_H = 0x0B, U_I = 0x0C, U_J = 0x0D, U_K = 0x0E, U_L = 0x0F,
    U_M = 0x10, U_N = 0x11, U_O = 0x12, U_P = 0x13, U_Q = 0x14, U_R = 0x15,
    U_S = 0x16, U_T = 0x17, U_U = 0x18, U_V = 0x19, U_W = 0x1A, U_X = 0x1B,
    U_Y = 0x1C, U_Z = 0x1D,
    U_0 = 0x27, U_1 = 0x1E, U_2 = 0x1F, U_3 = 0x20, U_4 = 0x21,
    U_5 = 0x22, U_6 = 0x23, U_7 = 0x24, U_8 = 0x25, U_9 = 0x26,
    U_ENTER = 0x28, U_ESC = 0x29, U_BACKSPACE = 0x2A, U_TAB = 0x2B,
    U_SPACE = 0x2C, U_MINUS = 0x2D, U_QUOTE = 0x34, U_COMMA = 0x36,
    U_DOT = 0x37, U_SLASH = 0x38, U_SEMI = 0x33,
    U_RIGHT = 0x4F, U_LEFT = 0x50, U_DOWN = 0x51, U_UP = 0x52,
};

static int failures = 0;
static int passes = 0;
#define CHECK(cond, what)                                                  \
    do {                                                                   \
        if (cond) {                                                        \
            passes++;                                                      \
            printf("PASS %s\n", what);                                     \
        } else {                                                           \
            failures++;                                                    \
            printf("FAIL %s (line %d)\n", what, __LINE__);                 \
        }                                                                  \
    } while (0)

typedef struct {
    ck_engine_t e;
    uint8_t matrix[CK_ROWS];
    uint32_t now;
    uint8_t mods, usages[CK_REPORT_KEYS], count;
} rig_t;

/* The single shared stock transcription (see ck5200_default_keymap.h):
 * one fixture for the engine and shim test binaries, so corrections
 * cannot diverge between them. */
#include "ck5200_default_keymap.h"

static void rig_init(rig_t *r, const ck_keymap_t *map)
{
    memset(r, 0, sizeof(*r));
    ck_engine_init(&r->e, map);
}

static void tick(rig_t *r)
{
    ck_engine_tick(&r->e, r->matrix, r->now,
                   &r->mods, r->usages, &r->count);
}

static void key(rig_t *r, int row, int col, bool down)
{
    if (down)
        r->matrix[row] |= (uint8_t)(1u << col);
    else
        r->matrix[row] &= (uint8_t)~(1u << col);
    tick(r);
}

static void advance(rig_t *r, uint32_t ms)
{
    r->now += ms;
    tick(r);   /* time-driven tick: no matrix change required */
}

static bool report_is(rig_t *r, uint8_t want_mods, const uint16_t *want, uint8_t n)
{
    if (r->mods != want_mods || r->count != n)
        return false;
    for (uint8_t i = 0; i < n; i++)
        if (r->usages[i] != (uint8_t)want[i])
            return false;
    return true;
}


/* ---------------- default behavior ---------------- */

static void test_stock_default(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    rig_t r;
    rig_init(&r, &map);

    key(&r, 0, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_Q}, 1), "stock base: q emits HID q");
    key(&r, 0, 0, false);
    CHECK(r.count == 0, "stock base: q release clears");

    /* Corrected Shift position: 2/03 holds Left Shift. */
    key(&r, 2, 3, true);
    CHECK(r.mods == CK_MOD_LSHIFT, "correction: 2/03 is Left Shift, not a spare");
    key(&r, 0, 0, true);
    CHECK(report_is(&r, CK_MOD_LSHIFT, (uint16_t[]){U_Q}, 1),
          "correction: Shift + q composes");
    key(&r, 0, 0, false);
    key(&r, 2, 3, false);
    CHECK(r.mods == 0 && r.count == 0, "correction: Shift release clears");

    /* GUI and Ctrl positions. */
    key(&r, 2, 5, true);
    CHECK(r.mods == CK_MOD_LGUI, "stock base: LeftGUI bit set");
    key(&r, 2, 5, false);
    key(&r, 4, 5, true);
    CHECK(r.mods == CK_MOD_LCTL, "stock base: LeftCtrl bit set");
    key(&r, 4, 5, false);

    /* One-shot SYM: r emits 4, reverts to base. */
    key(&r, 5, 4, true);
    key(&r, 3, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_4}, 1), "stock sym: one-shot + r emits 4");
    key(&r, 3, 0, false);
    key(&r, 5, 4, false);
    key(&r, 3, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_R}, 1),
          "stock sym: one-shot consumed once, base returns");
    key(&r, 3, 0, false);
    CHECK(r.count == 0, "stock sym: clean end state");
}

/* ------------- the P1 regression: per-layer modifiers ------------- */

static void test_nav_remap_no_base_regression(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    /* NAV Undo on m (4/04): usage z with GUI modifier, NAV layer only. */
    set_plain_any(&map, CK_LAYER_NAV, 4, 4, U_Z, CK_MOD_LGUI);
    /* NAV Redo on c (5/03): usage z with GUI+SHIFT, NAV layer only. */
    set_plain_any(&map, CK_LAYER_NAV, 5, 3, U_Z, CK_MOD_LGUI | CK_MOD_LSHIFT);
    /* NAV Esc on the Shift position's NAV layer (2/03 nav). */
    set_plain_any(&map, CK_LAYER_NAV, 2, 3, U_ESC, 0);
    /* NAV Tab on n (3/04). */
    set_plain_any(&map, CK_LAYER_NAV, 3, 4, U_TAB, 0);
    /* NAV activator: hold on Backspace position (0/03). */
    {
        ck_action_t *a = &map.action[CK_LAYER_BASE][ck_pos(0, 3)];
        memset(a, 0, sizeof(*a));
        a->activation = CK_ACT_HOLD;
        a->layer_switch = CK_LAYER_NAV;
    }
    CHECK(ck_keymap_validate(&map) == 0, "nav fixture validates");

    rig_t r;
    rig_init(&r, &map);

    /* BASE layer typing is unchanged by the NAV assignments. */
    key(&r, 4, 4, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_M}, 1),
          "no-regression: base m is plain m (no GUI)");
    key(&r, 4, 4, false);
    key(&r, 5, 3, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_C}, 1),
          "no-regression: base c is plain c (no GUI+SHIFT)");
    key(&r, 5, 3, false);
    key(&r, 3, 4, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_N}, 1),
          "no-regression: base n is plain n (no Tab)");
    key(&r, 3, 4, false);

    /* And the NAV actions produce the intended reports. */
    key(&r, 0, 3, true);              /* hold NAV */
    key(&r, 4, 4, true);
    CHECK(report_is(&r, CK_MOD_LGUI, (uint16_t[]){U_Z}, 1),
          "nav: Undo (GUI+Z) emits correctly");
    key(&r, 4, 4, false);
    key(&r, 5, 3, true);
    CHECK(report_is(&r, CK_MOD_LGUI | CK_MOD_LSHIFT, (uint16_t[]){U_Z}, 1),
          "nav: Redo (GUI+SHIFT+Z) emits correctly");
    key(&r, 5, 3, false);
    /* NAV Esc on the Shift position's NAV action. */
    key(&r, 2, 3, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_ESC}, 1),
          "nav: Esc on 2/03 NAV action");
    key(&r, 2, 3, false);
    /* Base 2/03 is still Shift: released NAV, press 2/03 -> Shift bit. */
    key(&r, 0, 3, false);
    key(&r, 2, 3, true);
    CHECK(r.mods == CK_MOD_LSHIFT,
          "nav: base 2/03 still Left Shift after NAV use");
    key(&r, 2, 3, false);
    CHECK(r.count == 0 && r.mods == 0, "nav: releases clean");
}

/* ---------------- activation modes (ms timing) ---------------- */

static void test_hold_and_toggle_and_oneshot(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_action_t *hold_key = &map.action[0][ck_pos(0, 3)];
    hold_key->activation = CK_ACT_HOLD; hold_key->layer_switch = CK_LAYER_NAV;
    ck_action_t *toggle_key = &map.action[0][ck_pos(0, 4)];
    toggle_key->activation = CK_ACT_TOGGLE; toggle_key->layer_switch = CK_LAYER_NAV;
    /* NAV usage to observe: w -> Up. */
    rig_t r;
    rig_init(&r, &map);

    /* HOLD: layer while held, gone on release. */
    key(&r, 0, 3, true);
    key(&r, 1, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_UP}, 1), "hold: w emits Up while held");
    key(&r, 0, 3, false);
    key(&r, 1, 0, false);
    key(&r, 1, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_W}, 1), "hold: base w after release");
    key(&r, 1, 0, false);

    /* TOGGLE: press/release flips on, again flips off. */
    key(&r, 0, 4, true);
    key(&r, 0, 4, false);
    key(&r, 1, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_UP}, 1), "toggle: on after first press");
    key(&r, 1, 0, false);
    key(&r, 0, 4, true);
    key(&r, 0, 4, false);
    key(&r, 1, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_W}, 1), "toggle: off after second press");
    key(&r, 1, 0, false);

    /* ONESHOT: survives its release, consumed by one press, expires by
     * time if unused. */
    key(&r, 5, 4, true);
    key(&r, 5, 4, false);
    CHECK(ck_engine_next_deadline(&r.e) != CK_NO_DEADLINE,
          "oneshot: engine reports its expiry deadline");
    key(&r, 3, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_4}, 1),
          "oneshot: applies after its own release");
    key(&r, 3, 0, false);
    key(&r, 3, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_R}, 1),
          "oneshot: consumed by exactly one press");
    key(&r, 3, 0, false);

    /* Unused one-shot expires by time, no matrix event needed. */
    key(&r, 5, 4, true);
    key(&r, 5, 4, false);
    advance(&r, 1500);
    key(&r, 3, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_R}, 1),
          "oneshot: expires by elapsed time without a press");
    key(&r, 3, 0, false);
}

static void test_tap_timing_and_time_driven_release(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_action_t *t = &map.action[0][ck_pos(5, 4)];
    t->activation = CK_ACT_TAP;
    t->layer_switch = CK_LAYER_SYM;
    t->tap_usage = U_ESC;
    t->tap_ms = 200;

    rig_t r;
    rig_init(&r, &map);

    /* Quick solo tap: press at 100ms, release at 150ms -> Esc fires. */
    r.now = 100;
    key(&r, 5, 4, true);
    r.now = 150;
    key(&r, 5, 4, false);
    CHECK(report_is(&r, 0, (uint16_t[]){U_ESC}, 1),
          "tap: quick solo release emits tap usage");

    /* THE decisive check: the release report arrives from a TIME-DRIVEN
     * tick with NO matrix change and no other key event. */
    uint32_t d = ck_engine_next_deadline(&r.e);
    CHECK(d != CK_NO_DEADLINE, "tap: engine schedules the release tick");
    r.now = d + 1;
    tick(&r);
    CHECK(r.count == 0,
          "tap: release reaches the host without any further key event");

    /* Held past the window: layer semantics, no tap usage on release. */
    r.now = 1000;
    key(&r, 5, 4, true);
    advance(&r, 250);                       /* still held, window closed */
    key(&r, 3, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_4}, 1),
          "tap: held past window gives the layer (r -> 4)");
    r.now = 1400;
    key(&r, 5, 4, false);                   /* slow release: no tap */
    CHECK(report_is(&r, 0, (uint16_t[]){U_4}, 1),
          "tap: slow release emits no tap usage");
    key(&r, 3, 0, false);
    CHECK(r.count == 0, "tap: clean end state");

    /* Second key inside the window suppresses the tap usage. */
    r.now = 2000;
    key(&r, 5, 4, true);
    key(&r, 3, 0, true);                    /* consumes the layer */
    r.now = 2050;
    key(&r, 5, 4, false);
    CHECK(report_is(&r, 0, (uint16_t[]){U_4}, 1),
          "tap: second key inside window suppresses tap usage");
    key(&r, 3, 0, false);
    CHECK(r.count == 0, "tap: final release clears");
}

/* ---------------- overlapping activators ---------------- */

static void test_overlapping_activators(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_action_t *a1 = &map.action[0][ck_pos(0, 3)];
    a1->activation = CK_ACT_HOLD; a1->layer_switch = CK_LAYER_NAV;
    ck_action_t *a2 = &map.action[0][ck_pos(0, 4)];
    a2->activation = CK_ACT_HOLD; a2->layer_switch = CK_LAYER_SYM;

    rig_t r;
    rig_init(&r, &map);

    /* Press NAV holder, then SYM holder: the most recent wins. */
    key(&r, 0, 3, true);
    key(&r, 1, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_UP}, 1), "overlap: NAV first -> Up");
    key(&r, 0, 4, true);
    key(&r, 1, 0, false);
    key(&r, 1, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_2}, 1),
          "overlap: SYM activator pressed later takes precedence");
    /* Release the SYM holder: NAV holder still held -> NAV returns. */
    key(&r, 0, 4, false);
    key(&r, 1, 0, false);
    key(&r, 1, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_UP}, 1),
          "overlap: releasing the later activator falls back to the earlier");
    key(&r, 1, 0, false);
    key(&r, 0, 3, false);
    key(&r, 1, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_W}, 1),
          "overlap: base after all activators released");
    key(&r, 1, 0, false);
    CHECK(r.count == 0, "overlap: clean end state");
}

/* ---------------- release across layer transitions ---------------- */

static void test_nested_actions_and_explicit_transparency(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_action_t *a = &map.action[0][ck_pos(0, 3)];
    *a = (ck_action_t){.activation = CK_ACT_HOLD, .layer_switch = CK_LAYER_NAV};
    map.action[CK_LAYER_NAV][ck_pos(0, 4)] =
        (ck_action_t){.activation = CK_ACT_HOLD, .layer_switch = CK_LAYER_SYM};
    rig_t r;
    rig_init(&r, &map);
    /* Same-scan activators resolve in row/column order, not host timing. */
    r.matrix[0] = 0x18;
    tick(&r);
    key(&r, 1, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_2}, 1),
          "nested: NAV-defined activator uses its SYM snapshot");
    key(&r, 1, 0, false);
    key(&r, 0, 3, false);
    key(&r, 1, 0, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_2}, 1),
          "nested: release earlier activator keeps later snapshot");
    key(&r, 1, 0, false);
    key(&r, 0, 4, false);

    for (int mode = CK_ACT_TOGGLE; mode <= CK_ACT_ONESHOT; mode++) {
        map.action[CK_LAYER_NAV][ck_pos(0, 4)].activation = mode;
        rig_init(&r, &map);
        key(&r, 0, 3, true);
        key(&r, 0, 4, true);
        key(&r, 0, 4, false);
        key(&r, 0, 3, false);
        key(&r, 1, 0, true);
        CHECK(report_is(&r, 0, (uint16_t[]){U_2}, 1),
              "nested: toggle/oneshot snapshot survives activator release");
    }

    for (int layer = 0; layer < CK_LAYERS; layer++) {
        map.action[layer][0] = (ck_action_t){.kind = CK_ACTION_NO};
        CHECK(ck_engine_resolve_action(&map, layer, 0)->kind == CK_ACTION_NO,
              "NO: disabled on each layer without Base fallback");
        map.action[layer][0] = (ck_action_t){.kind = CK_ACTION_TRANSPARENT};
        const ck_action_t *resolved = ck_engine_resolve_action(&map, layer, 0);
        CHECK(resolved == &map.action[0][0], "transparent: resolves to Base on each layer");
        map.action[layer][0] = (ck_action_t){.usage = U_Q};
    }
    map.action[CK_LAYER_NAV][0] = (ck_action_t){.kind = CK_ACTION_NO};
    rig_init(&r, &map);
    key(&r, 0, 3, true);
    key(&r, 0, 0, true);
    CHECK(r.count == 0, "NO: held disabled key emits no Base usage");
}

static void test_release_across_layer_switch(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_action_t *a = &map.action[0][ck_pos(0, 3)];
    a->activation = CK_ACT_HOLD; a->layer_switch = CK_LAYER_NAV;

    rig_t r;
    rig_init(&r, &map);

    /* Press q on base, then hold NAV, then release q: clean. */
    key(&r, 0, 0, true);
    key(&r, 0, 3, true);
    CHECK(report_is(&r, 0, (uint16_t[]){U_Q}, 1),
          "transition: held base key keeps its usage under NAV");
    key(&r, 0, 0, false);
    CHECK(r.count == 0, "transition: release under another layer clears");
    key(&r, 0, 3, false);

    /* Press Up under NAV, release the activator, then release the key. */
    key(&r, 0, 3, true);
    key(&r, 1, 0, true);
    key(&r, 0, 3, false);
    CHECK(report_is(&r, 0, (uint16_t[]){U_UP}, 1),
          "transition: nav key held after layer release keeps its usage");
    key(&r, 1, 0, false);
    CHECK(r.count == 0, "transition: late release clears the arrow");
}

/* ---------------- 108-position sweep ---------------- */

static void test_all_positions_all_layers(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    /* Hold-activators for each layer placed on 0/04 (SYM) and 0/03 (NAV). */
    ck_action_t *sym_hold = &map.action[0][ck_pos(0, 4)];
    sym_hold->activation = CK_ACT_HOLD; sym_hold->layer_switch = CK_LAYER_SYM;
    ck_action_t *nav_hold = &map.action[0][ck_pos(0, 3)];
    nav_hold->activation = CK_ACT_HOLD; nav_hold->layer_switch = CK_LAYER_NAV;
    CHECK(ck_keymap_validate(&map) == 0, "sweep: fixture validates");

    int layer_holders[CK_LAYERS][2] = {
        {-1, -1}, {0, 4}, {0, 3}
    };

    for (int layer = 0; layer < CK_LAYERS; layer++) {
        rig_t r;
        rig_init(&r, &map);
        if (layer != CK_LAYER_BASE) {
            key(&r, layer_holders[layer][0], layer_holders[layer][1], true);
        }
        for (int row = 0; row < CK_ROWS; row++) {
            for (int col = 0; col < CK_COLS; col++) {
                int pos = ck_pos(row, col);
                /* Skip the two test activators on every layer: pressing
                 * them mid-sweep would toggle/release the held layer. */
                if (pos == ck_pos(0, 3) || pos == ck_pos(0, 4))
                    continue;
                const ck_action_t *a = ck_engine_resolve_action(&map, layer, pos);
                key(&r, row, col, true);
                bool is_layer_action = a->activation != CK_ACT_NONE;
                uint16_t want_usage = a->usage;
                uint8_t want_mods = a->mods;
                if (is_layer_action || (want_usage == 0 && want_mods == 0)) {
                    if (!is_layer_action)
                        CHECK(r.count == 0,
                              "sweep: empty action emits nothing");
                } else if (want_usage == 0) {
                    CHECK(r.mods == want_mods && r.count == 0,
                          "sweep: modifier-only action emits its bits");
                } else {
                    CHECK(report_is(&r, want_mods, &want_usage, 1),
                          "sweep: plain action emits usage+mods");
                }
                key(&r, row, col, false);
                /* After release the report may retain the holder's layer
                 * and nothing else. */
                CHECK(r.count == 0,
                      "sweep: every release clears its contribution");
            }
        }
        /* Any still-held modifier state must be gone after releases. */
        CHECK(r.mods == 0, "sweep: no modifier leaks after all releases");
    }
    printf("PASS sweep: all 108 layer-position combinations exercised\n");
    passes++;
}

/* ---------------- malformed input and keymaps ---------------- */

static void test_boundary_bits_and_malformed_keymap(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    rig_t r;
    rig_init(&r, &map);

    /* Bits 0x40/0x80 in a row are outside the six-column matrix and must
     * be ignored, not treated as a phantom seventh/eighth key. */
    r.matrix[0] = 0xC0;
    tick(&r);
    CHECK(r.count == 0 && r.mods == 0, "boundary: bits 0x40/0x80 ignored");
    r.matrix[0] = 0x00;
    tick(&r);
    CHECK(r.count == 0, "boundary: clear after masked bits");

    /* Out-of-page usage: validation flags it, runtime ignores it. */
    ck_keymap_t bad = map;
    bad.action[CK_LAYER_BASE][ck_pos(0, 0)].usage = 0x1FF;
    CHECK(ck_keymap_validate(&bad) > 0, "malformed: out-of-page usage flagged");
    rig_t rb;
    rig_init(&rb, &bad);
    key(&rb, 0, 0, true);
    CHECK(rb.count == 0, "malformed: invalid usage emits nothing");
    key(&rb, 0, 0, false);

    /* Bad layer target: flagged, and the layer action cannot resolve. */
    ck_keymap_t bad2 = map;
    bad2.action[0][ck_pos(5, 4)].layer_switch = 3;
    CHECK(ck_keymap_validate(&bad2) > 0, "malformed: bad layer target flagged");
}

/* ---------------- chords and rollover ---------------- */

static void test_chords(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    rig_t r;
    rig_init(&r, &map);

    key(&r, 4, 5, true);   /* LeftCtrl */
    key(&r, 0, 0, true);
    key(&r, 0, 1, true);
    key(&r, 1, 0, true);
    key(&r, 1, 1, true);
    CHECK(r.mods == CK_MOD_LCTL && r.count == 4, "chord: ctrl with 4 usages");
    key(&r, 1, 1, false);
    CHECK(r.mods == CK_MOD_LCTL && r.count == 3, "chord: partial release");
    key(&r, 1, 0, false);
    key(&r, 0, 1, false);
    key(&r, 0, 0, false);
    key(&r, 4, 5, false);
    CHECK(r.mods == 0 && r.count == 0, "chord: full release clears");
}

static void test_multiple_pending_taps(void)
{
    ck_keymap_t map = {0};
    map.action[0][0] = (ck_action_t){.activation = CK_ACT_TAP,
        .layer_switch = CK_LAYER_NAV, .tap_usage = U_ESC, .tap_ms = 200};
    map.action[0][1] = (ck_action_t){.activation = CK_ACT_TAP,
        .layer_switch = CK_LAYER_NAV, .tap_usage = U_TAB, .tap_ms = 200};
    rig_t r;
    rig_init(&r, &map);
    /* Two qualifying releases before either 1 ms deadline. The API
     * permits multiple matrix updates at the same clock value. */
    key(&r, 0, 0, true); key(&r, 0, 0, false);
    key(&r, 0, 1, true); key(&r, 0, 1, false);
    CHECK(report_is(&r, 0, (uint16_t[]){U_ESC, U_TAB}, 2),
          "pending taps: both press emissions remain visible");
    advance(&r, 1);
    CHECK(r.count == 0 && ck_engine_next_deadline(&r.e) == CK_NO_DEADLINE,
          "pending taps: both releases clear without another matrix event");
    key(&r, 0, 0, true); key(&r, 0, 0, false);
    key(&r, 0, 0, true); advance(&r, 1);
    key(&r, 0, 0, false); advance(&r, 1);
    CHECK(r.count == 0, "pending taps: same-position repress also clears");
}

int main(void)
{
    test_stock_default();
    test_multiple_pending_taps();
    test_nav_remap_no_base_regression();
    test_hold_and_toggle_and_oneshot();
    test_tap_timing_and_time_driven_release();
    test_overlapping_activators();
    test_nested_actions_and_explicit_transparency();
    test_release_across_layer_switch();
    test_all_positions_all_layers();
    test_boundary_bits_and_malformed_keymap();
    test_chords();
    printf(failures == 0 ? "ok\n" : "==== %d failure(s) ====\n", failures);
    return failures == 0 ? 0 : 1;
}
