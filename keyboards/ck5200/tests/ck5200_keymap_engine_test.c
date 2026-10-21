/*
 * Behavioral tests for the CK-5200 stock-extension keymap engine.
 * Offline research artifact for the programmable stock-extension
 * feasibility milestone (deliverable 2). See
 * keyboards/ck5200/docs/stock-extension-feasibility.md.
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

/* HID usage codes used below. */
enum {
    U_NONE = 0x00,
    U_A = 0x04, U_B = 0x05, U_C = 0x06, U_D = 0x07, U_E = 0x08, U_F = 0x09,
    U_G = 0x0A, U_H = 0x0B, U_I = 0x0C, U_J = 0x0D, U_K = 0x0E,
    U_L = 0x0F, U_M = 0x10, U_N = 0x11, U_O = 0x12, U_P = 0x13,
    U_Q = 0x14, U_R = 0x15, U_S = 0x16, U_T = 0x17, U_U = 0x18,
    U_V = 0x19, U_W = 0x1A, U_X = 0x1B, U_Y = 0x1C, U_Z = 0x1D,
    U_0 = 0x27, U_1 = 0x1E, U_2 = 0x1F, U_3 = 0x20, U_4 = 0x21,
    U_5 = 0x22, U_6 = 0x23, U_7 = 0x24, U_8 = 0x25, U_9 = 0x26,
    U_ENTER = 0x28, U_ESC = 0x29, U_BACKSPACE = 0x2A, U_TAB = 0x2B,
    U_SPACE = 0x2C, U_MINUS = 0x2D, U_QUOTE = 0x34, U_COMMA = 0x36,
    U_DOT = 0x37, U_SLASH = 0x38, U_SEMI = 0x33,
    U_RIGHT = 0x4F, U_LEFT = 0x50, U_DOWN = 0x51, U_UP = 0x52,
};

static int failures = 0;
#define CHECK(cond, what)                                                  \
    do {                                                                   \
        if (cond) {                                                        \
            printf("PASS %s\n", what);                                     \
        } else {                                                           \
            failures++;                                                    \
            printf("FAIL %s (line %d)\n", what, __LINE__);                 \
        }                                                                  \
    } while (0)

/* Row/col to matrix bytes: col c in row r sets bit (1 << c) in matrix[r].
 * ck_pos(r, c) is the engine's position index; matrix rows mirror the
 * ledger's r/b notation. */
static void key(ck_engine_t *e, uint8_t matrix[CK_ROWS], int row, int col,
                bool down, uint8_t *mods, uint8_t usages[CK_REPORT_KEYS],
                uint8_t *count)
{
    if (down)
        matrix[row] |= (uint8_t)(1u << col);
    else
        matrix[row] &= (uint8_t)~(1u << col);
    ck_engine_tick(e, matrix, mods, usages, count);
}

static bool report_is(uint8_t mods, const uint8_t *us, uint8_t count,
                      uint8_t want_mods, const uint16_t *want, uint8_t want_count)
{
    if (mods != want_mods || count != want_count)
        return false;
    for (uint8_t i = 0; i < count; i++)
        if (us[i] != (uint8_t)want[i])
            return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* Default keymap: the stock ledger transcription (base/SYM/NAV).       */
/* Consumers, GUI/Ctrl modifiers, and the Clicks key keep their stock   */
/* definitions; the engine models the boot-report subset.                */
/* ------------------------------------------------------------------ */

static void stock_keymap(ck_keymap_t *m)
{
    memset(m, 0, sizeof(*m));
    ck_key_def_t *k;
#define SET(r, c, base, sym, nav)             \
    do {                                      \
        k = &m->keys[ck_pos(r, c)];           \
        k->usage[CK_LAYER_BASE] = (base);     \
        k->usage[CK_LAYER_SYM] = (sym);       \
        k->usage[CK_LAYER_NAV] = (nav);        \
    } while (0)

    /* Row 0: q u k Backspace Enter (0/20 = consumer, not modeled) */
    SET(0, 0, U_Q, U_1, U_NONE);
    SET(0, 1, U_U, U_7, U_NONE);
    SET(0, 2, U_K, U_QUOTE, U_DOWN);
    SET(0, 3, U_BACKSPACE, U_NONE, U_NONE);
    SET(0, 4, U_ENTER, U_NONE, U_NONE);
    /* Row 1: w i d l v (1/20 = consumer) */
    SET(1, 0, U_W, U_2, U_UP);
    SET(1, 1, U_I, U_8, U_UP);
    SET(1, 2, U_D, U_SEMI, U_RIGHT);
    SET(1, 3, U_L, U_QUOTE, U_RIGHT);
    SET(1, 4, U_V, U_SLASH, U_NONE);
    /* Row 2: e o f [2/08 UNUSED] b LeftGUI */
    SET(2, 0, U_E, U_3, U_NONE);
    SET(2, 1, U_O, U_9, U_NONE);
    SET(2, 2, U_F, U_SEMI, U_NONE);
    SET(2, 3, U_NONE, U_NONE, U_NONE);   /* the spare crossing */
    SET(2, 4, U_B, U_1, U_NONE);
    k = &m->keys[ck_pos(2, 5)];
    k->usage[CK_LAYER_BASE] = U_NONE; k->mods = CK_MOD_LGUI;
    /* Row 3: r p g z n Space */
    SET(3, 0, U_R, U_4, U_NONE);
    SET(3, 1, U_P, U_0, U_NONE);
    SET(3, 2, U_G, U_9, U_NONE);
    SET(3, 3, U_Z, U_QUOTE, U_NONE);
    SET(3, 4, U_N, U_COMMA, U_NONE);
    SET(3, 5, U_SPACE, U_NONE, U_NONE);
    /* Row 4: t a h x m LeftCtrl */
    SET(4, 0, U_T, U_5, U_NONE);
    SET(4, 1, U_A, U_MINUS, U_LEFT);
    SET(4, 2, U_H, U_0, U_NONE);
    SET(4, 3, U_X, U_7, U_NONE);
    SET(4, 4, U_M, U_DOT, U_NONE);
    k = &m->keys[ck_pos(4, 5)];
    k->usage[CK_LAYER_BASE] = U_NONE; k->mods = CK_MOD_LCTL;
    /* Row 5: y s [Clicks key] c companion consumer */
    SET(5, 0, U_Y, U_6, U_NONE);
    SET(5, 1, U_S, U_SLASH, U_DOWN);
    SET(5, 2, U_J, U_NONE, U_LEFT);      /* Clicks key (stock: Tab/Control) */
    SET(5, 3, U_C, U_2, U_NONE);
    /* 5/10 companion = stock SYM switch; modeled as one-shot. */
    k = &m->keys[ck_pos(5, 4)];
    k->layer_switch = CK_LAYER_SYM;
    k->activation = CK_ACT_ONESHOT;
#undef SET
}

/* Test: stock-default keymap emits the ledger's outputs. */
static void test_stock_default_behavior(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_engine_t e;
    ck_engine_init(&e, &map);
    uint8_t matrix[CK_ROWS] = {0};
    uint8_t mods, usages[CK_REPORT_KEYS], count;

    /* Base layer letters, press and release. */
    key(&e, matrix, 0, 0, true, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_Q}, 1),
          "stock base: q emits HID q");
    key(&e, matrix, 0, 0, false, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, 0, NULL, 0),
          "stock base: q release clears the report");

    /* Modifiers: LeftGUI position holds the GUI bit. */
    key(&e, matrix, 2, 5, true, &mods, usages, &count);
    CHECK(mods == CK_MOD_LGUI, "stock base: LeftGUI position sets the GUI bit");
    key(&e, matrix, 0, 0, true, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, CK_MOD_LGUI, (uint16_t[]){U_Q}, 1),
          "stock base: GUI + q composes");
    key(&e, matrix, 0, 0, false, &mods, usages, &count);
    key(&e, matrix, 2, 5, false, &mods, usages, &count);
    CHECK(mods == 0 && count == 0, "stock base: modifier release clears");

    /* SYM layer via the one-shot companion: r emits 4. */
    key(&e, matrix, 5, 4, true, &mods, usages, &count);   /* companion down */
    key(&e, matrix, 3, 0, true, &mods, usages, &count);   /* r down */
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_4}, 1),
          "stock sym: one-shot companion + r emits 4");
    key(&e, matrix, 5, 4, false, &mods, usages, &count);  /* companion up */
    key(&e, matrix, 3, 0, false, &mods, usages, &count);
    CHECK(count == 0, "stock sym: releases clear after one-shot use");

    /* A second press after the one-shot reverted emits the base letter. */
    key(&e, matrix, 5, 4, true, &mods, usages, &count);
    key(&e, matrix, 3, 0, true, &mods, usages, &count);
    key(&e, matrix, 3, 0, false, &mods, usages, &count);
    key(&e, matrix, 5, 4, false, &mods, usages, &count);
    key(&e, matrix, 3, 0, true, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_R}, 1),
          "stock sym: one-shot consumed once, then base returns");
    key(&e, matrix, 3, 0, false, &mods, usages, &count);
}

/* Test: the NAV remap goal — Esc, Tab, Undo, Redo, arrows, with correct
 * releases and configurable activation. */
static void test_nav_custom_remap(void)
{
    ck_keymap_t map;
    stock_keymap(&map);

    /* NAV assignments for the goal: spare 2/08 = Esc; NAV layer adds
     * Tab (3/10 n), Undo = GUI+Z (4/10 m), Redo = GUI+SHIFT+Z (5/08 c).
     * Stock NAV arrows stay. */
    map.keys[ck_pos(2, 3)].usage[CK_LAYER_NAV] = U_ESC;
    map.keys[ck_pos(3, 4)].usage[CK_LAYER_NAV] = U_TAB;
    map.keys[ck_pos(4, 4)].usage[CK_LAYER_NAV] = U_Z;
    map.keys[ck_pos(4, 4)].mods = CK_MOD_LGUI;            /* Undo = CMD+Z */
    map.keys[ck_pos(5, 3)].usage[CK_LAYER_NAV] = U_Z;
    map.keys[ck_pos(5, 3)].mods = CK_MOD_LGUI | CK_MOD_LSHIFT; /* Redo */

    /* A NAV layer key: hold mode on 0/04 (Backspace position acts as the
     * NAV switch in this custom profile). */
    ck_key_def_t *nav = &map.keys[ck_pos(0, 3)];
    nav->usage[CK_LAYER_BASE] = U_NONE;
    nav->layer_switch = CK_LAYER_NAV;
    nav->activation = CK_ACT_HOLD;

    ck_engine_t e;
    ck_engine_init(&e, &map);
    uint8_t matrix[CK_ROWS] = {0};
    uint8_t mods, usages[CK_REPORT_KEYS], count;

    /* HOLD activation: NAV while held. */
    key(&e, matrix, 0, 3, true, &mods, usages, &count);
    key(&e, matrix, 2, 3, true, &mods, usages, &count);   /* spare */
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_ESC}, 1),
          "nav remap: spare 2/08 emits Esc under NAV");
    key(&e, matrix, 3, 4, true, &mods, usages, &count);   /* n */
    CHECK(report_is(mods, usages, count, 0,
                    (uint16_t[]){U_ESC, U_TAB}, 2),
          "nav remap: Esc + Tab held together");
    key(&e, matrix, 3, 4, false, &mods, usages, &count);
    key(&e, matrix, 2, 3, false, &mods, usages, &count);
    CHECK(count == 0, "nav remap: releases clear Esc and Tab");

    /* Undo: NAV + m emits CMD+Z. */
    key(&e, matrix, 0, 3, true, &mods, usages, &count);
    key(&e, matrix, 4, 4, true, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, CK_MOD_LGUI, (uint16_t[]){U_Z}, 1),
          "nav remap: Undo (CMD+Z) emits GUI modifier with z");
    key(&e, matrix, 4, 4, false, &mods, usages, &count);
    key(&e, matrix, 0, 3, false, &mods, usages, &count);
    CHECK(mods == 0 && count == 0, "nav remap: Undo release clears modifier");

    /* Redo: NAV + c emits SHIFT+CMD+Z. */
    key(&e, matrix, 0, 3, true, &mods, usages, &count);
    key(&e, matrix, 5, 3, true, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, CK_MOD_LGUI | CK_MOD_LSHIFT,
                    (uint16_t[]){U_Z}, 1),
          "nav remap: Redo (SHIFT+CMD+Z) composes both modifiers");
    key(&e, matrix, 5, 3, false, &mods, usages, &count);
    key(&e, matrix, 0, 3, false, &mods, usages, &count);
    CHECK(mods == 0 && count == 0, "nav remap: Redo release clears");

    /* Arrows on NAV still work from the stock transcription. */
    key(&e, matrix, 0, 3, true, &mods, usages, &count);
    key(&e, matrix, 1, 0, true, &mods, usages, &count);   /* w -> Up */
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_UP}, 1),
          "nav remap: w emits Up under NAV");
    key(&e, matrix, 1, 0, false, &mods, usages, &count);
    key(&e, matrix, 0, 3, false, &mods, usages, &count);
    CHECK(count == 0, "nav remap: arrow release clears");
}

/* Test: no stuck keys when the layer changes while a key is held. */
static void test_release_after_layer_switch(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_key_def_t *nav = &map.keys[ck_pos(0, 3)];
    nav->usage[CK_LAYER_BASE] = U_NONE;
    nav->layer_switch = CK_LAYER_NAV;
    nav->activation = CK_ACT_HOLD;

    ck_engine_t e;
    ck_engine_init(&e, &map);
    uint8_t matrix[CK_ROWS] = {0};
    uint8_t mods, usages[CK_REPORT_KEYS], count;

    /* Press q (base), then hold NAV, then release q: report must be empty
     * even though the effective layer is NAV at release time. */
    key(&e, matrix, 0, 0, true, &mods, usages, &count);
    key(&e, matrix, 0, 3, true, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_Q}, 1),
          "layer switch: held base key keeps its usage under NAV");
    key(&e, matrix, 0, 0, false, &mods, usages, &count);
    CHECK(count == 0, "layer switch: release under a different layer clears");
    key(&e, matrix, 0, 3, false, &mods, usages, &count);
    CHECK(count == 0, "layer switch: switch release leaves a clean report");

    /* Reverse order: press under NAV, release on base. */
    key(&e, matrix, 0, 3, true, &mods, usages, &count);
    key(&e, matrix, 1, 0, true, &mods, usages, &count);   /* Up */
    key(&e, matrix, 0, 3, false, &mods, usages, &count);  /* layer gone */
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_UP}, 1),
          "layer switch: nav key held after layer release keeps its usage");
    key(&e, matrix, 1, 0, false, &mods, usages, &count);
    CHECK(count == 0, "layer switch: late release clears the arrow");
}

/* Test: TOGGLE activation flips a layer on and off. */
static void test_toggle_activation(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    map.keys[ck_pos(2, 3)].usage[CK_LAYER_NAV] = U_ESC;
    ck_key_def_t *nav = &map.keys[ck_pos(0, 3)];
    nav->usage[CK_LAYER_BASE] = U_NONE;
    nav->layer_switch = CK_LAYER_NAV;
    nav->activation = CK_ACT_TOGGLE;

    ck_engine_t e;
    ck_engine_init(&e, &map);
    uint8_t matrix[CK_ROWS] = {0};
    uint8_t mods, usages[CK_REPORT_KEYS], count;

    key(&e, matrix, 0, 3, true, &mods, usages, &count);
    key(&e, matrix, 0, 3, false, &mods, usages, &count);
    key(&e, matrix, 2, 3, true, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_ESC}, 1),
          "toggle: first press switches NAV on (spare emits Esc)");
    key(&e, matrix, 2, 3, false, &mods, usages, &count);
    key(&e, matrix, 0, 3, true, &mods, usages, &count);
    key(&e, matrix, 0, 3, false, &mods, usages, &count);
    key(&e, matrix, 2, 3, true, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, 0, NULL, 0),
          "toggle: second press switches back to base (spare emits nothing)");
    key(&e, matrix, 2, 3, false, &mods, usages, &count);
}

/* Test: TAP activation emits tap_usage on a quick solo release, and only
 * the layer when held longer or followed by another key. */
static void test_tap_activation(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_key_def_t *sym = &map.keys[ck_pos(5, 4)];
    sym->activation = CK_ACT_TAP;
    sym->tap_usage = U_ESC;      /* tap = Esc, hold = SYM layer */
    sym->tap_ticks = 3;

    ck_engine_t e;
    ck_engine_init(&e, &map);
    uint8_t matrix[CK_ROWS] = {0};
    uint8_t mods, usages[CK_REPORT_KEYS], count;

    /* Quick solo tap -> Esc for exactly one report cycle. */
    key(&e, matrix, 5, 4, true, &mods, usages, &count);
    key(&e, matrix, 5, 4, false, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_ESC}, 1),
          "tap: quick solo release emits tap usage (Esc)");
    ck_engine_tick(&e, matrix, &mods, usages, &count);
    CHECK(count == 0, "tap: tap usage lasts exactly one report cycle");

    /* Held past the window with another key: SYM layer, no tap usage. */
    key(&e, matrix, 5, 4, true, &mods, usages, &count);
    ck_engine_tick(&e, matrix, &mods, usages, &count);
    ck_engine_tick(&e, matrix, &mods, usages, &count);
    key(&e, matrix, 3, 0, true, &mods, usages, &count);   /* r under SYM */
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_4}, 1),
          "tap: holding the layer key gives the SYM layer (r -> 4)");
    key(&e, matrix, 3, 0, false, &mods, usages, &count);
    key(&e, matrix, 5, 4, false, &mods, usages, &count);
    CHECK(count == 0, "tap: releases after held use clear the report");

    /* Quick release but a second key was pressed inside the window: the
     * layer applied to that key, so no tap usage may fire. */
    key(&e, matrix, 5, 4, true, &mods, usages, &count);
    key(&e, matrix, 3, 0, true, &mods, usages, &count);   /* r -> 4 */
    key(&e, matrix, 5, 4, false, &mods, usages, &count);  /* quick release */
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_4}, 1),
          "tap: second key inside the window suppresses the tap usage");
    key(&e, matrix, 3, 0, false, &mods, usages, &count);
    CHECK(count == 0, "tap: final release clears");
}

/* Test: ONESHOT activation applies the layer to exactly one next press. */
static void test_oneshot_activation(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_key_def_t *sym = &map.keys[ck_pos(5, 4)];
    sym->activation = CK_ACT_ONESHOT;

    ck_engine_t e;
    ck_engine_init(&e, &map);
    uint8_t matrix[CK_ROWS] = {0};
    uint8_t mods, usages[CK_REPORT_KEYS], count;

    key(&e, matrix, 5, 4, true, &mods, usages, &count);
    key(&e, matrix, 5, 4, false, &mods, usages, &count);
    key(&e, matrix, 3, 0, true, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_4}, 1),
          "one-shot: released companion still applies to the next press");
    key(&e, matrix, 3, 0, false, &mods, usages, &count);
    key(&e, matrix, 3, 0, true, &mods, usages, &count);
    CHECK(report_is(mods, usages, count, 0, (uint16_t[]){U_R}, 1),
          "one-shot: the following press returns to base");
    key(&e, matrix, 3, 0, false, &mods, usages, &count);
    CHECK(count == 0, "one-shot: clean end state");
}

/* Test: chord sanity — four simultaneous usages plus a modifier. */
static void test_chords_and_rollover(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_engine_t e;
    ck_engine_init(&e, &map);
    uint8_t matrix[CK_ROWS] = {0};
    uint8_t mods, usages[CK_REPORT_KEYS], count;

    key(&e, matrix, 4, 5, true, &mods, usages, &count);   /* LeftCtrl */
    key(&e, matrix, 0, 0, true, &mods, usages, &count);    /* q */
    key(&e, matrix, 0, 1, true, &mods, usages, &count);   /* u */
    key(&e, matrix, 1, 0, true, &mods, usages, &count);   /* w */
    key(&e, matrix, 1, 1, true, &mods, usages, &count);   /* i */
    CHECK(mods == CK_MOD_LCTL && count == 4,
          "chord: ctrl holds with four usages");
    key(&e, matrix, 1, 1, false, &mods, usages, &count);
    CHECK(mods == CK_MOD_LCTL && count == 3,
          "chord: partial release keeps the rest");
    key(&e, matrix, 1, 0, false, &mods, usages, &count);
    key(&e, matrix, 0, 1, false, &mods, usages, &count);
    key(&e, matrix, 0, 0, false, &mods, usages, &count);
    key(&e, matrix, 4, 5, false, &mods, usages, &count);
    CHECK(mods == 0 && count == 0, "chord: full release clears everything");
}

int main(void)
{
    test_stock_default_behavior();
    test_nav_custom_remap();
    test_release_after_layer_switch();
    test_toggle_activation();
    test_tap_activation();
    test_oneshot_activation();
    test_chords_and_rollover();
    printf(failures == 0 ? "ok\n" : "==== %d failure(s) ====\n", failures);
    return failures == 0 ? 0 : 1;
}
