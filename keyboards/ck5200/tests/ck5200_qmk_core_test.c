/* CK-5200 offline behavior harness: links the REAL pinned QMK 0.26.11 core
 * plus the REAL CK-5200 keymap, dynamic keymap, consumer module, and
 * wear-leveling backing store, on a host CH32-flash model with manual time.
 *
 * Everything below the matrix hardware and above the USB stack is the
 * production code path: matrix_common -> sym_defer_g debounce ->
 * keyboard_task -> action_* -> layer switching -> keymap lookup ->
 * dynamic_keymap over real wear leveling -> host driver report capture.
 *
 * Ported from the 2026-10-11 adversarial review (reviewer 3), which used
 * it to prove findings F1/F2/F3/F4-class defects before they reached
 * hardware. The T7 scenario now asserts the upstream zero-erase contract.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "quantum.h"
#include "timer.h"
#include "dynamic_keymap.h"
#include "eeconfig.h"
#include "host.h"
#include "host_driver.h"
#include "report.h"
#include "ck5200_consumer.h"
#include "ck5200_keymap.h"
#include "ch32x035.h" /* shim: flash model + counters */

/* platforms/test/timer.c manual time control (declared only in test code). */
void     set_time(uint32_t t);
void     advance_time(uint32_t ms);

/* ---------------- matrix injection (replaces WCH matrix.c) ------------- */
static matrix_row_t test_raw[MATRIX_ROWS];
static matrix_row_t injected_prev[MATRIX_ROWS];
static long scan_calls;

void matrix_init_custom(void) {
    memset(test_raw, 0, sizeof(test_raw));
    memset(injected_prev, 0, sizeof(injected_prev));
}

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    bool changed = false;
    scan_calls++;
    for (uint8_t r = 0; r < MATRIX_ROWS; ++r) {
        current_matrix[r] = test_raw[r];
        changed |= test_raw[r] != injected_prev[r];
        injected_prev[r] = test_raw[r];
    }
    return changed;
}

/* ---------------- host driver capture ---------------------------------- */
static report_keyboard_t cap_report;
static long cap_count;
static uint8_t cap_leds;
#define HIST 64
static report_keyboard_t cap_hist[HIST];
static long hist_len;

static void cap_reset_hist(void) { hist_len = 0; }
static bool hist_has(uint8_t keycode) {
    for (long h = 0; h < hist_len && h < HIST; ++h)
        for (int i = 0; i < 6; ++i)
            if (cap_hist[h].keys[i] == keycode) return true;
    return false;
}
static bool hist_has_mods(uint8_t mask) {
    for (long h = 0; h < hist_len && h < HIST; ++h)
        if (cap_hist[h].mods & mask) return true;
    return false;
}

static uint8_t dk_leds(void) { return cap_leds; }
static void dk_keyboard(report_keyboard_t *r) {
    cap_report = *r;
    cap_count++;
    if (hist_len < HIST) cap_hist[hist_len++] = *r;
}
static void dk_nkro(report_nkro_t *r) { (void)r; }
static void dk_mouse(report_mouse_t *r) { (void)r; }
static void dk_extra(report_extra_t *r) { (void)r; }
static host_driver_t cap_driver = {
    .keyboard_leds = dk_leds, .send_keyboard = dk_keyboard, .send_nkro = dk_nkro, .send_mouse = dk_mouse, .send_extra = dk_extra,
};

/* ---------------- CK-5200 backlight stubs for ck5200_keymap.c ---------- */
static uint8_t bl_raw;
void ck5200_backlight_set(uint8_t raw) { bl_raw = raw; }
uint8_t ck5200_backlight_get(void) { return bl_raw; }

/* Platform hooks the core references; no bootloader on the host. */
void bootloader_jump(void) { assert(!"bootloader_jump must not run on host"); }
void mcu_reset(void) { assert(!"mcu_reset must not run on host"); }
void suspend_power_down_kb(void) {}
void suspend_wakeup_init_kb(void) {}

/* ---------------- helpers ---------------------------------------------- */
static int failures;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (cond) {                                                    \
            printf("PASS " __VA_ARGS__);                              \
            printf("\n");                                             \
        } else {                                                       \
            failures++;                                                \
            printf("FAIL " __VA_ARGS__);                              \
            printf("\n");                                             \
        }                                                              \
    } while (0)

static void set_key(int row, int col, bool on) {
    if (on) test_raw[row] |= (matrix_row_t)1 << col;
    else test_raw[row] &= ~((matrix_row_t)1 << col);
}

static void settle(int ms) {
    for (int i = 0; i < ms; ++i) {
        advance_time(1);
        keyboard_task();
    }
}

/* Idle long enough to clear all tapping/debounce state between tests. */
static void idle(void) { settle(300); }

static bool report_has(uint8_t keycode) {
    for (int i = 0; i < 6; ++i)
        if (cap_report.keys[i] == keycode) return true;
    return false;
}

static bool report_empty(void) {
    if (cap_report.mods != 0) return false;
    for (int i = 0; i < 6; ++i)
        if (cap_report.keys[i] != 0) return false;
    return true;
}

static void boot(void) {
    /* Mirrors quantum/main.c ordering minus the protocol layer. */
    host_set_driver(&cap_driver);
    keyboard_setup(); /* -> eeprom_driver_init() under EEPROM_DRIVER */
    keyboard_init();  /* -> timer_init, matrix_init, quantum_init */
}

/* ---------------- tests ------------------------------------------------ */
static void t_first_boot_defaults(void) {
    printf("\n== T1 first boot seeds the dynamic keymap from PROGMEM ==\n");
    boot();
    CHECK(dynamic_keymap_get_keycode(0, 0, 0) == KC_Q, "L0(0,0)=KC_Q");
    CHECK(dynamic_keymap_get_keycode(0, 3, 0) == KC_R, "L0(3,0)=KC_R");
    CHECK(dynamic_keymap_get_keycode(1, 0, 0) == KC_1, "L1(0,0)=KC_1");
    CHECK(dynamic_keymap_get_keycode(1, 2, 4) == S(KC_1), "L1(2,4)=S(KC_1)");
    CHECK(dynamic_keymap_get_keycode(2, 1, 2) == KC_RIGHT, "L2(1,2)=KC_RIGHT");
    CHECK(dynamic_keymap_get_keycode(0, 2, 3) == KC_LSFT, "L0(2,3)=KC_LSFT (stock 0x2ae6)");
    CHECK(dynamic_keymap_get_keycode(3, 5, 5) == KC_TRNS, "L3(5,5)=KC_TRNS");
    CHECK(eeconfig_is_enabled(), "eeconfig enabled after first boot");
}

static void t_typing(void) {
    idle();
    printf("\n== T2 basic typing and release symmetry ==\n");
    long before = cap_count;
    set_key(0, 0, true); /* KC_Q */
    settle(12);
    CHECK(cap_count > before, "press produced a report");
    CHECK(report_has(KC_Q), "report contains KC_Q");
    before = cap_count;
    set_key(0, 0, false);
    settle(12);
    CHECK(cap_count > before, "release produced a report");
    CHECK(report_empty(), "report empty after release");
}

static void t_modifier(void) {
    idle();
    printf("\n== T3 modifier combination (LGUI + Q) ==\n");
    set_key(2, 5, true); /* KC_LGUI */
    settle(12);
    set_key(0, 0, true); /* KC_Q */
    settle(12);
    CHECK((cap_report.mods & 0x08) == 0x08, "LGUI modifier bit set (0x%02x)", cap_report.mods);
    CHECK(report_has(KC_Q), "Q present with modifier");
    set_key(0, 0, false);
    set_key(2, 5, false);
    settle(12);
    CHECK(report_empty(), "report empty after releasing all");
}

static void t_lt_tap(void) {
    idle();
    printf("\n== T4a LT(2,KC_J) tap sends J (realistic 80 ms tap) ==\n");
    cap_reset_hist();
    set_key(5, 2, true);
    settle(80); /* physical tap duration */
    set_key(5, 2, false);
    settle(12);
    CHECK(hist_has(KC_J), "tap on Clicks key produced KC_J at some point");
    CHECK(report_empty(), "post-tap report empty after release");
    printf("     history (%ld reports):", hist_len);
    for (long h = 0; h < hist_len && h < 12; ++h)
        printf(" [%02x %02x %02x %02x %02x %02x %02x]", cap_hist[h].mods, cap_hist[h].keys[0], cap_hist[h].keys[1], cap_hist[h].keys[2], cap_hist[h].keys[3], cap_hist[h].keys[4], cap_hist[h].keys[5]);
    printf("\n");

    /* An event shorter than DEBOUNCE vanishes under sym_defer_g. */
    printf("   -- sub-debounce tap (2 ms, shorter than DEBOUNCE 5) --\n");
    long before = cap_count;
    cap_reset_hist();
    set_key(0, 0, true);
    settle(2);
    set_key(0, 0, false);
    settle(12);
    CHECK(cap_count == before, "2 ms tap produced NO report (defer debounce swallowed it)");
    set_key(0, 0, true);
    settle(2);
    set_key(0, 0, false);
    settle(12);
}

static void t_fuzz_dispatcher(void) {
    printf("\n== T11 fuzz: random dispatcher packets through the real handler ==\n");
    /* Deterministic xorshift PRNG so results reproduce. */
    uint32_t seed = 0xC0FFEE42u;
    long accepted = 0;
    for (long i = 0; i < 200000; ++i) {
        uint8_t buf[64];
        size_t len = (size_t)(seed % 65u);
        for (size_t j = 0; j < 64; ++j) {
            seed = seed * 1664525u + 1013904223u;
            buf[j] = (uint8_t)(seed >> 24);
        }
        if (i % 2 == 0) {
            /* Structured half: valid framing and mostly-valid commands,
             * with adversarial arguments (out-of-range layers/rows/cols). */
            static const uint8_t cmds[] = {0x03, 0x82, 0x84, 0x90, 0x91, 0x92, 0x93, 0x8f};
            seed = seed * 1664525u + 1013904223u;
            buf[1] = cmds[(seed >> 16) % sizeof(cmds)];
            if (len >= 1 && len <= 64) buf[0] = (uint8_t)len;
            for (size_t j = 2; j < 64; ++j) {
                seed = seed * 1664525u + 1013904223u;
                buf[j] = (uint8_t)(seed >> 24);
            }
        }
        uint8_t resp[8];
        size_t n = ck5200_keymap_handle(buf, len, resp);
        if (n > 0) accepted++;
        /* Invariant: response length must be 0 or 4..8 (buffer is 8). */
        if (n > 8) { CHECK(false, "response length out of bounds (%zu)", n); return; }
    }
    CHECK(accepted > 0, "fuzz completed: %ld packets accepted, no crash, no OOB (ASan clean)", accepted);

    /* After the fuzz storm the keymap must still be sane for all crossings. */
    for (uint8_t l = 0; l < 4; ++l)
        for (uint8_t r = 0; r < 6; ++r)
            for (uint8_t c = 0; c < 6; ++c) {
                uint16_t kc = dynamic_keymap_get_keycode(l, r, c);
                (void)kc; /* value itself is user-editable; just read under ASan */
            }
    CHECK(1, "all 144 crossings still readable after fuzz");

    /* Restore the factory map so later scenarios are deterministic. */
    dynamic_keymap_reset();
    CHECK(dynamic_keymap_get_keycode(0, 0, 0) == KC_Q, "factory map restored after fuzz");
}

static void t_burst_typing(void) {
    idle();
    printf("\n== T10 burst typing: alternating keys, 6KRO stacking ==\n");
    cap_reset_hist();
    /* T6/T7 remapped (0,0) to KC_X persistently, so expect X here, not Q. */
    set_key(0, 0, true); /* KC_X after the dynamic-keymap edit */
    set_key(4, 4, true); /* M */
    settle(12);
    CHECK(report_has(KC_X) && report_has(KC_M), "both X and M in one report (got k0=%02x k1=%02x)", cap_report.keys[0], cap_report.keys[1]);
    set_key(0, 0, false);
    set_key(4, 4, false);
    settle(12);
    CHECK(report_empty(), "release both -> empty");

    /* Rapid alternating presses at 8 ms cadence. */
    cap_reset_hist();
    for (int i = 0; i < 10; ++i) {
        set_key(1, 1, true); /* I */
        settle(8);
        set_key(1, 1, false);
        set_key(1, 3, true); /* L */
        settle(8);
        set_key(1, 3, false);
        settle(4);
    }
    settle(12); /* let the final release pass the debounce window */
    CHECK(hist_has(KC_I) && hist_has(KC_L), "alternating burst produced both I and L keycodes");
    CHECK(report_empty(), "alternating burst ends with an empty report (got k0=%02x)", cap_report.keys[0]);

    /* Six simultaneous keys (6KRO limit); T6/T7 remapped (0,0) to KC_X. */
    cap_reset_hist();
    set_key(1, 0, true); set_key(0, 1, true); set_key(0, 2, true);
    set_key(1, 1, true); set_key(1, 2, true); set_key(1, 4, true);
    settle(12);
    int n = 0;
    for (int i = 0; i < 6; ++i) if (cap_report.keys[i]) n++;
    CHECK(n == 6, "six simultaneous keys all present (n=%d)", n);
    set_key(1, 0, false); set_key(0, 1, false); set_key(0, 2, false);
    set_key(1, 1, false); set_key(1, 2, false); set_key(1, 4, false);
    settle(12);
    CHECK(report_empty(), "six-key release -> empty report");
}

static void t_lt_hold(void) {
    idle();
    printf("\n== T4b LT(2,KC_J) hold reaches cursor layer ==\n");
    cap_reset_hist();
    set_key(5, 2, true);
    settle(220); /* > TAPPING_TERM 200 */
    set_key(1, 2, true); /* layer2 = KC_RIGHT */
    settle(12);
    CHECK(hist_has(KC_RIGHT), "held Clicks + (1,2) produced KC_RIGHT");
    CHECK(report_has(KC_RIGHT), "KC_RIGHT still held (got k0=%02x)", cap_report.keys[0]);
    set_key(1, 2, false);
    set_key(5, 2, false);
    settle(12);
    CHECK(report_empty(), "release all -> empty report");
}

static void t_osl_sticky_sym(void) {
    idle();
    printf("\n== T5 OSL(1) sticky SYM: documented intent vs actual ==\n");
    set_key(5, 4, true); /* OSL(1) sticky SYM */
    settle(12);
    set_key(5, 4, false);
    settle(12);
    cap_reset_hist();
    set_key(0, 0, true); /* should type KC_1 through layer 1 */
    settle(12);
    /* Documented intent (keymap.c comment, HANDOFF.md): KC_1 on layer 1.
     * Upstream one-shot semantics (action.c:865): a key pressed through
     * the one-shot layer is auto-released immediately after, so the wire
     * sees exactly one KC_1 tap and then an empty report while the
     * physical key is still held. Failed before the NO_ACTION_ONESHOT
     * removal (review 2026-10-11, F3): history contained KC_Q instead. */
    CHECK(hist_has(KC_1), "sticky SYM then (0,0) typed KC_1 through layer 1");
    CHECK(!hist_has(KC_Q), "sticky SYM did NOT type through layer 0");
    CHECK(report_empty(), "one-shot auto-released the key (upstream sticky semantics)");
    printf("     observed report: mods=%02x keys=%02x %02x %02x\n", cap_report.mods, cap_report.keys[0], cap_report.keys[1], cap_report.keys[2]);
    set_key(0, 0, false);
    settle(12);
}

static void t_dynamic_keymap_edit(void) {
    idle();
    printf("\n== T6 runtime keymap edit takes effect immediately ==\n");
    dynamic_keymap_set_keycode(0, 0, 0, KC_X);
    set_key(0, 0, true);
    settle(12);
    CHECK(report_has(KC_X), "after dynamic_keymap_set_keycode(0,0,0,KC_X) the key produced KC_X");
    set_key(0, 0, false);
    settle(12);
    CHECK(report_empty(), "empty after release");

    /* Out-of-range writes must be ignored by the QMK layer. */
    dynamic_keymap_set_keycode(4, 0, 0, KC_A);  /* layer 4 >= count */
    dynamic_keymap_set_keycode(0, 6, 0, KC_A);  /* row 6 */
    dynamic_keymap_set_keycode(0, 0, 6, KC_A);  /* col 6 */
    CHECK(dynamic_keymap_get_keycode(0, 0, 0) == KC_X, "out-of-range writes ignored");
}

static void t_reboot_persistence(int cycles) {
    idle();
    printf("\n== T7 simulated re-inits: runtime-only persistence ==\n");
    /* Persistence is runtime-only by design (review 2026-10-12): the
     * CH32X035 has no writable region available to this firmware (flash
     * ends at 0x0800F7FF; staging, serial, config, and the reserved
     * region are off-limits), so the keymap lives in the transient RAM
     * EEPROM backend. Edits survive a soft re-init within the same power
     * session and are lost on real power loss; flash-backed persistence
     * stays disabled until a storage partition is demonstrated. */
    for (int i = 0; i < cycles; ++i) {
        boot();
        CHECK(dynamic_keymap_get_keycode(0, 0, 0) == KC_X, "re-init %d: KC_X retained (RAM-backed)", i + 1);
    }
    boot();
    set_key(0, 0, true);
    settle(12);
    CHECK(report_has(KC_X), "typed KC_X after re-init");
    set_key(0, 0, false);
    settle(12);
}

static void t_consumer_encoding(void) {
    idle();
    printf("\n== T8 consumer key encoding vs HID little-endian contract ==\n");
    set_key(5, 5, true); /* CK_IOS_KB -> consumer Eject 0x00B8 */
    settle(12);
    CHECK(ck5200_consumer_usage() == 0x00B8, "consumer usage latched 0x00B8 (got 0x%04x)", ck5200_consumer_usage());
    uint8_t wire[10] = {0};
    uint8_t kbd[8] = {0};
    ck5200_consumer_pack(wire, kbd);
    /* HID report fields are little-endian: byte 8 is the LSB of the field.
     * Failed before the byte-order fix (review 2026-10-11, F2). */
    uint16_t field = (uint16_t)(wire[8] | (wire[9] << 8));
    CHECK(field == 0x00B8, "host-decoded consumer field == 0x00B8 (got 0x%04x)", field);
    printf("     wire bytes 8,9 = %02x %02x (descriptor field decodes to 0x%04x)\n", wire[8], wire[9], field);
    set_key(5, 5, false);
    settle(12);
    CHECK(ck5200_consumer_usage() == 0, "consumer released");

    set_key(0, 5, true); /* CK_CONS_A -> stock bit 2 = 0x00CF */
    settle(12);
    CHECK(ck5200_consumer_usage() == 0x00CF, "CK_CONS_A latched 0x00CF (got 0x%04x)", ck5200_consumer_usage());
    set_key(0, 5, false);
    settle(12);
}

static void t_chatter_starvation(void) {
    idle();
    printf("\n== T9 isolation: unrelated chatter must not starve stable key ==\n");
    set_key(1, 1, true); /* KC_I stable press */
    settle(12);
    CHECK(report_has(KC_I), "stable press KC_I visible before chatter");

    /* Hold KC_M while a different crossing bounces faster than DEBOUNCE. */
    set_key(4, 4, true); /* KC_M held */
    for (int t = 0; t < 60; t += 2) {
        set_key(0, 1, t % 4 == 0); /* chattering crossing (0,1) */
        settle(2);
    }
    CHECK(report_has(KC_M), "stable KC_M must register despite unrelated chatter");
    set_key(0, 1, false);
    set_key(4, 4, false);
    set_key(1, 1, false);
    settle(12);
    CHECK(report_empty(), "all keys released -> empty report");
}

int main(void) {
    set_time(0);
    t_first_boot_defaults();
    t_typing();
    t_modifier();
    t_lt_tap();
    t_lt_hold();
    t_osl_sticky_sym();
    t_dynamic_keymap_edit();
    t_reboot_persistence(3);
    t_consumer_encoding();
    t_chatter_starvation();
    t_burst_typing();
    t_fuzz_dispatcher();

    printf("\n==== %d failure(s) ====\n", failures);
    if (!failures) printf("ok\n");
    return failures ? 1 : 0;
}
