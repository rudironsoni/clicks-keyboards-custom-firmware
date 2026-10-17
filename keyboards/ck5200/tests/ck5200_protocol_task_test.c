/* Protocol-layer harness: real protocol.c + real QMK core + tusb stub.
 * Simulates exactly the TinyUSB 0.21 call pattern:
 *   tud_hid_report() accepts -> endpoint busy ->
 *   hidd_xfer_cb -> tud_hid_report_complete_cb(instance, epin, xferred=10).
 *
 * Ported from the 2026-10-11 adversarial review (reviewer 3), which used
 * it to prove the F1 completion-callback freeze on the production file.
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
#include "tusb.h"
#include "ck5200_update_protocol.h"
#include "ck5200_consumer.h"
#include "ch32x035_gpio.h"
#include "ch32x035_rcc.h"

void     set_time(uint32_t t);
void     advance_time(uint32_t ms);

/* protocol.c public surface (quantum/main.c hooks). */
void protocol_setup(void);
void protocol_pre_task(void);
void protocol_keyboard_task(void);
void protocol_post_task(void);

/* ---------------- TinyUSB stub ------------------------------------------ */
bool stub_mounted, stub_suspended, stub_hid_ready;
uint8_t stub_epin_buf[64];
long stub_submit_count;
int stub_init_ports[4] = {0};
static uint16_t wire_len;
static uint8_t wire_history[512][10];
static unsigned wire_count;
static bool reject_report;

bool tud_mounted(void) { return stub_mounted; }
bool tud_suspended(void) { return stub_suspended; }
bool tud_hid_ready(void) { return stub_hid_ready; }
void tud_task(void) {}
void tud_init(uint8_t rhport) { ++stub_init_ports[rhport & 3u]; }
uint32_t tud_vendor_n_write(uint8_t itf, const void *buf, uint32_t len) {
    (void)itf; (void)buf; (void)len;
    return len; /* pretend the endpoint accepted */
}

bool tud_hid_report(uint8_t report_id, void const *report, uint16_t len) {
    (void)report_id;
    if (!stub_mounted || stub_suspended || !stub_hid_ready) return false;
    if (reject_report) return false;
    if (len > sizeof(stub_epin_buf)) return false;
    memcpy(stub_epin_buf, report, len);
    wire_len = len;
    if (wire_count < 512) {
        memset(wire_history[wire_count], 0, 10);
        memcpy(wire_history[wire_count++], report, len);
    }
    stub_submit_count++;
    return true;
}

/* TinyUSB-side events the harness drives. */
extern void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len);
static void usb_complete_hid(void) { tud_hid_report_complete_cb(0, stub_epin_buf, wire_len); }

/* ---------------- matrix injection ------------------------------------- */
static matrix_row_t test_raw[MATRIX_ROWS];
static long scan_calls;
static GPIO_TypeDef gpio_a, gpio_b;
GPIO_TypeDef *GPIOA = &gpio_a, *GPIOB = &gpio_b;
static uint16_t selected_row;
void RCC_APB2PeriphClockCmd(uint32_t peripherals, unsigned enable) {
    assert(peripherals == (RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB) && enable == ENABLE);
}
void GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init) {
    assert(init->GPIO_Pin == 0x3f);
    assert((port == GPIOA && init->GPIO_Mode == GPIO_Mode_IPD) ||
           (port == GPIOB && init->GPIO_Mode == GPIO_Mode_Out_PP && init->GPIO_Speed == GPIO_Speed_50MHz));
}
void GPIO_ResetBits(GPIO_TypeDef *port, uint16_t pins) {
    assert(port == GPIOB && pins == 0x3f); selected_row = 0;
}
void GPIO_SetBits(GPIO_TypeDef *port, uint16_t pins) {
    assert(port == GPIOB && selected_row == 0 && pins && !(pins & (pins - 1)) && pins <= 32);
    selected_row = pins;
}
uint16_t GPIO_ReadInputData(GPIO_TypeDef *port) {
    assert(port == GPIOA && selected_row);
    unsigned row = 0;
    while ((1u << row) != selected_row) ++row;
    if (!row) ++scan_calls;
    return test_raw[row] | 0xffc0; /* unrelated inputs must be masked out */
}

/* ---------------- iAP2 / backlight / update stubs ----------------------- */
static bool stub_session_active;
bool ck5200_rescue_active(void) { return false; }
void ck5200_request_rescue(void) { assert(false); }
void ck5200_staging_abort(void) {}
bool ck5200_staging_rescue_entry_allowed(void) { return false; }
bool iap2_restore_authorized(void) { return false; }
void iap2_init(void) {}
void iap2_set_connected(bool c) { (void)c; }
bool iap2_session_active(void) { return stub_session_active; }
void iap2_tick_1ms(void) {}
void iap2_elapsed_ms(uint32_t elapsed) { (void)elapsed; }
void iap2_task(void) {}
void iap2_activity(void) {}
void iap2_ep2_rx(const uint8_t *buf, uint32_t len) { (void)buf; (void)len; }
static uint8_t bl_raw;
void ck5200_backlight_init(void) {}
void ck5200_backlight_set(uint8_t raw) { bl_raw = raw; }
uint8_t ck5200_backlight_get(void) { return bl_raw; }

/* The staging writer is covered by the repo's own restore test; here it is
 * a null ops table so protocol.c links without flash-address code. */
const ck5200_update_ops_t ck5200_staging_ops = {0};
size_t ck5200_update_handle(const uint8_t *request, size_t request_len, uint8_t response[8], const ck5200_update_ops_t *ops) {
    (void)request; (void)request_len; (void)response; (void)ops;
    return 0;
}
void bootloader_jump(void) { assert(!"bootloader_jump must not run on host"); }
void mcu_reset(void) { assert(!"mcu_reset must not run on host"); }
void suspend_power_down_kb(void) {}
void suspend_wakeup_init_kb(void) {}

/* ---------------- helpers ------------------------------------------------ */
static int failures;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (cond) { printf("PASS " __VA_ARGS__); printf("\n"); }        \
        else { failures++; printf("FAIL " __VA_ARGS__); printf("\n"); } \
    } while (0)

static void set_key(int row, int col, bool on) {
    if (on) test_raw[row] |= (matrix_row_t)1 << col;
    else test_raw[row] &= ~((matrix_row_t)1 << col);
}

/* One main-loop iteration, exactly like quantum/main.c. */
static void loop_tick(void) {
    advance_time(1);
    protocol_pre_task();
    protocol_keyboard_task();
    protocol_post_task();
}

static bool report_wire_has(uint8_t keycode) {
    /* 10-byte wire report: mods, reserved, keys[6], consumer low, consumer high */
    for (int i = 2; i < 8; ++i)
        if (stub_epin_buf[i] == keycode) return true;
    return false;
}

/* ---------------- scenarios ---------------------------------------------- */
static void setup_firmware(void) {
    stub_mounted = false;
    stub_suspended = false;
    stub_hid_ready = true;
    protocol_setup();
    keyboard_setup();
    keyboard_init();
    stub_mounted = true; /* enumeration done */
}

static void settle_ticks(int n) {
    for (int i = 0; i < n; ++i) { loop_tick(); usb_complete_hid(); }
}

static void p1_chord_pending_loop(void) {
    printf("== P1 chord (two reports in one scan): pending-report lifecycle ==\n");
    /* Let the matrix settle with no keys first. */
    settle_ticks(12);

    set_key(0, 0, true);
    set_key(4, 4, true);
    /* Debounce needs DEBOUNCE(5) ms; both presses land in ONE matrix pass:
     * report1 is submitted, report2 goes to pending. */
    settle_ticks(12);
    long submits_after_chord = stub_submit_count;

    long scans_before = scan_calls;
    long submits_before = stub_submit_count;
    for (int i = 0; i < 20; ++i) { loop_tick(); usb_complete_hid(); }
    long scan_delta = scan_calls - scans_before;
    long submit_delta = stub_submit_count - submits_before;

    printf("     chord submits=%ld; over 20 idle cycles: scans +%ld, submits +%ld\n",
           submits_after_chord, scan_delta, submit_delta);
    CHECK(submits_after_chord == 2, "the chord produced exactly two report submissions (n=%ld)", submits_after_chord);
    CHECK(scan_delta >= 19, "keyboard_task kept scanning every cycle (matrix alive, delta=%ld)", scan_delta);
    CHECK(submit_delta == 0, "no repeated submission of an already-completed report (delta=%ld)", submit_delta);

    set_key(0, 0, false);
    set_key(4, 4, false);
    settle_ticks(12);
}

static void p2_idle_resend(void) {
    printf("== P2 SET_IDLE accepted => periodic resend while idle ==\n");
    extern bool tud_hid_set_idle_cb(uint8_t instance, uint8_t idle_rate);
    CHECK(tud_hid_set_idle_cb(0, 10), "SET_IDLE(10) accepted (40 ms cadence)");

    /* Press and release one key so there is a last report to repeat. */
    set_key(1, 1, true);
    settle_ticks(12);
    set_key(1, 1, false);
    settle_ticks(12);

    long submits_before = stub_submit_count;
    for (int i = 0; i < 100; ++i) { loop_tick(); usb_complete_hid(); } /* 100 ms idle */
    long idle_resends = stub_submit_count - submits_before;
    printf("     idle resends observed over 100 ms: %ld\n", idle_resends);
    CHECK(idle_resends >= 2, "periodic idle reports were re-sent (HID 7.2.9 requirement)");
}

static void p3_suspend_key_held(void) {
    printf("== P3 suspend with a pending report, resume must stay alive ==\n");
    extern void tud_suspend_cb(bool remote_wakeup_en);
    extern void tud_resume_cb(void);
    settle_ticks(12);

    /* Chord press: report1 in flight, report2 pending. */
    set_key(0, 0, true);
    set_key(1, 1, true);
    for (int i = 0; i < 8; ++i) loop_tick(); /* let debounce push the chord */

    /* Bus suspends BEFORE the first report completes. */
    tud_suspend_cb(true);
    stub_suspended = true;
    usb_complete_hid(); /* the in-flight transfer still completes */
    stub_suspended = false;
    tud_resume_cb();

    long scans_before = scan_calls;
    long submits_before = stub_submit_count;
    /* The retained pending report legitimately consumes a pass or two
     * after resume before scanning resumes; let it flush first. */
    for (int i = 0; i < 12; ++i) { loop_tick(); usb_complete_hid(); }
    for (int i = 0; i < 20; ++i) { loop_tick(); usb_complete_hid(); }
    printf("     across resume: scans +%ld, submits +%ld\n", scan_calls - scans_before, stub_submit_count - submits_before);
    CHECK(scan_calls - scans_before >= 19, "matrix scanning alive after resume (delta=%ld)", scan_calls - scans_before);

    set_key(0, 0, false);
    set_key(1, 1, false);
    settle_ticks(12);
}

static void p4_get_report(void) {
    printf("== P4 GET_REPORT returns the current 10-byte report ==\n");
    extern uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen);
    settle_ticks(12);
    set_key(1, 3, true); /* KC_L */
    settle_ticks(12);
    uint8_t buf[16] = {0};
    uint16_t n = tud_hid_get_report_cb(0, 0, HID_REPORT_TYPE_INPUT, buf, sizeof(buf));
    bool has_l = false;
    for (int i = 2; i < 8; ++i) if (buf[i] == KC_L) has_l = true;
    CHECK(n == 10, "GET_REPORT returned the full 10-byte report (n=%d)", n);
    CHECK(has_l, "GET_REPORT payload contains the held keycode");
    set_key(1, 3, false);
    settle_ticks(12);
}

static void p6_root_port_init(void) {
    printf("== P6 protocol_setup initializes root port 0 exactly once ==\n");
    int before[4];
    for (int i = 0; i < 4; ++i) before[i] = stub_init_ports[i];
    keyboard_setup();
    keyboard_init();
    protocol_setup();
    stub_mounted = true;
    CHECK(stub_init_ports[0] - before[0] == 1, "protocol_setup called tud_init(0) exactly once (delta %d)", stub_init_ports[0] - before[0]);
    CHECK(stub_init_ports[1] == before[1], "no tud_init(1) ever (the V203 root-port call, review finding 1)");
    settle_ticks(5);
}

static void p5_plain_tap(void) {
    printf("== P5 ordinary tap (press+release flushed in one pass) ==\n");
    settle_ticks(12);
    set_key(5, 2, true);  /* LT(2, KC_J): tapping sends J */
    for (int i = 0; i < 80; ++i) { loop_tick(); usb_complete_hid(); }
    set_key(5, 2, false);
    /* The tap flush emits [J] then [] in one keyboard_task pass; the second
     * report goes to pending while the first is in flight. */
    for (int i = 0; i < 12; ++i) { loop_tick(); usb_complete_hid(); }

    long scans_before = scan_calls;
    long submits_before = stub_submit_count;
    for (int i = 0; i < 20; ++i) { loop_tick(); usb_complete_hid(); }
    printf("     after a single tap: scans +%ld, submits +%ld\n", scan_calls - scans_before, stub_submit_count - submits_before);
    CHECK(scan_calls - scans_before >= 19, "matrix still scanning after a plain tap (delta=%ld)", scan_calls - scans_before);
    CHECK(stub_submit_count - submits_before == 0, "no endless resubmission after a plain tap (delta=%ld)", stub_submit_count - submits_before);

    /* A second key pressed afterwards must still reach the host wire. */
    set_key(1, 1, true);
    bool saw_i = false;
    for (int i = 0; i < 12; ++i) {
        loop_tick();
        usb_complete_hid();
        if (report_wire_has(KC_I)) saw_i = true;
    }
    CHECK(saw_i, "a key pressed after the tap reached the host report");
    set_key(1, 1, false);
    settle_ticks(12);
}

static bool history_has(uint8_t key, uint8_t mods, uint16_t consumer) {
    for (unsigned h = 0; h < wire_count; ++h) {
        uint8_t *r = wire_history[h];
        if (r[0] != mods || (uint16_t)(r[8] | r[9] << 8) != consumer) continue;
        if (!key) return true;
        for (unsigned i = 2; i < 8; ++i) if (r[i] == key) return true;
    }
    return false;
}

static bool wire_empty(void) {
    const uint8_t empty[10] = {0};
    return memcmp(stub_epin_buf, empty, wire_len) == 0;
}

static void p7_all_crossings(void) {
    extern bool tud_hid_set_idle_cb(uint8_t, uint8_t);
    tud_hid_set_idle_cb(0, 0);
    /* Independent expected base usages, including the delayed J tap. */
    static const uint8_t keys[6][6] = {
        {KC_Q,KC_U,KC_K,KC_BSPC,KC_ENT,0},
        {KC_W,KC_I,KC_D,KC_L,KC_V,0},
        {KC_E,KC_O,KC_F,0,KC_B,0},
        {KC_R,KC_P,KC_G,KC_Z,KC_N,KC_SPC},
        {KC_T,KC_A,KC_H,KC_X,KC_M,0},
        {KC_Y,KC_S,KC_J,KC_C,0,0},
    };
    for (unsigned row = 0; row < 6; ++row) for (unsigned col = 0; col < 6; ++col) {
        settle_ticks(220);
        wire_count = 0;
        set_key(row, col, true); settle_ticks(12);
        set_key(row, col, false); settle_ticks(12);
        uint8_t mods = row == 2 && col == 3 ? 2 : col == 5 ? (row == 2 ? 8 : row == 4 ? 1 : 0) : 0;
        uint16_t consumer = col == 5 ? (row == 0 ? 0xcf : row == 1 ? 0x29d : row == 5 ? 0xb8 : 0) : 0;
        if (row == 5 && col == 4) {
            set_key(0, 0, true); settle_ticks(12);
            set_key(0, 0, false); settle_ticks(12);
            CHECK(history_has(KC_1, 0, 0), "SYM one-shot reaches wire");
            wire_count = 0;
            set_key(0, 0, true); settle_ticks(12);
            CHECK(history_has(KC_Q, 0, 0), "SYM consumed, next key uses base layer");
            set_key(0, 0, false); settle_ticks(12);
        } else CHECK(history_has(keys[row][col], mods, consumer), "crossing (%u,%u) reaches wire", row, col);
        CHECK(wire_empty() && selected_row == 0, "crossing (%u,%u) releases report and row drive", row, col);
    }
    set_key(2, 5, true); set_key(4, 5, true); set_key(0, 0, true);
    settle_ticks(12);
    CHECK(stub_epin_buf[0] == 9 && report_wire_has(KC_Q), "GUI + Ctrl + Q modifiers combine");
    set_key(2, 5, false); set_key(4, 5, false); set_key(0, 0, false);
    settle_ticks(12);
    CHECK(wire_empty(), "modifier chord releases");

    /* Stock 0x2a86..0x2ab0 adds Shift to these eight fixed SYM usages.
     * Z/J use configuration-dependent paths, intentionally not guessed. */
    const uint8_t symbols[][3] = {{1,2,KC_SCLN},{1,3,KC_QUOTE},{1,4,KC_SLASH},
                                 {2,4,KC_1},{3,2,KC_9},{4,2,KC_0},{4,3,KC_7},{5,3,KC_2}};
    for (unsigned i = 0; i < sizeof(symbols)/sizeof(symbols[0]); ++i) {
        set_key(5, 4, true); settle_ticks(12);
        set_key(5, 4, false); settle_ticks(12);
        wire_count = 0;
        set_key(symbols[i][0], symbols[i][1], true); settle_ticks(12);
        set_key(symbols[i][0], symbols[i][1], false); settle_ticks(12);
        CHECK(history_has(symbols[i][2], 2, 0) && wire_empty(), "shifted SYM crossing %u press/release", i);
    }
    set_key(2, 3, true); set_key(0, 0, true); settle_ticks(12);
    CHECK(stub_epin_buf[0] == 2 && report_wire_has(KC_Q), "physical Shift crossing + Q");
    set_key(2, 3, false); set_key(0, 0, false); settle_ticks(12);
    CHECK(wire_empty(), "Shift chord releases");

    set_key(5, 2, true); settle_ticks(220);
    const uint8_t arrows[][3] = {{0,2,KC_DOWN},{1,0,KC_UP},{1,1,KC_UP},{1,2,KC_RIGHT},
                                {1,3,KC_RIGHT},{4,1,KC_LEFT},{5,1,KC_DOWN}};
    for (unsigned i = 0; i < sizeof(arrows)/sizeof(arrows[0]); ++i) {
        set_key(arrows[i][0], arrows[i][1], true); settle_ticks(12);
        CHECK(report_wire_has(arrows[i][2]), "held Clicks navigation crossing %u", i);
        set_key(arrows[i][0], arrows[i][1], false); settle_ticks(12);
    }
    set_key(5, 2, false); settle_ticks(12);
    CHECK(wire_empty(), "navigation layer releases");

    wire_count = 0;
    for (unsigned i = 0; i < 20; ++i) {
        set_key(0, i % 2, true); settle_ticks(8);
        set_key(0, i % 2, false); settle_ticks(8);
    }
    CHECK(wire_count == 40, "20 rapid taps preserve all 40 edges (got %u)", wire_count);
    for (unsigned i = 0; i < wire_count; ++i) {
        uint8_t expected[10] = {0};
        if (!(i & 1)) expected[2] = (i / 2) % 2 ? KC_U : KC_Q;
        assert(memcmp(wire_history[i], expected, 10) == 0);
    }
}

static void p8_report_recovery(void) {
    extern void tud_mount_cb(void), tud_umount_cb(void), tud_suspend_cb(bool), tud_resume_cb(void);
    extern void tud_hid_set_protocol_cb(uint8_t, uint8_t);
    extern void tud_hid_report_failed_cb(uint8_t, hid_report_type_t, const uint8_t *, uint16_t);
    extern uint16_t tud_hid_get_report_cb(uint8_t, uint8_t, hid_report_type_t, uint8_t *, uint16_t);
    uint8_t out[16];
    for (unsigned disconnected = 0; disconnected < 2; ++disconnected) {
        set_key(0, 0, true); settle_ticks(12);
        set_key(5, 5, true); settle_ticks(12);
        if (disconnected) { stub_mounted = false; tud_umount_cb(); }
        else { stub_suspended = true; tud_suspend_cb(false); }
        long before = stub_submit_count;
        set_key(0, 0, false); set_key(5, 5, false); settle_ticks(12);
        CHECK(stub_submit_count == before, "no report while %s", disconnected ? "unmounted" : "suspended");
        if (disconnected) { stub_mounted = true; tud_mount_cb(); }
        else { stub_suspended = false; tud_resume_cb(); }
        settle_ticks(12);
        CHECK(wire_empty() && stub_submit_count > before, "current release state sent after reconnect/resume");
    }
    set_key(0, 0, true);
    for (unsigned i = 0; i < 8; ++i) loop_tick();
    long before = stub_submit_count;
    tud_hid_report_failed_cb(0, HID_REPORT_TYPE_INPUT, stub_epin_buf, 0);
    settle_ticks(12);
    CHECK(stub_submit_count > before && report_wire_has(KC_Q), "failed held-key transfer retries");
    set_key(0, 0, false); settle_ticks(12);

    reject_report = true;
    set_key(1, 5, true); settle_ticks(12); /* consumer-only report rejected */
    before = stub_submit_count;
    reject_report = false; settle_ticks(12);
    CHECK(stub_submit_count > before && stub_epin_buf[8] == 0x9d && stub_epin_buf[9] == 2,
          "rejected consumer submission stays queued for retry");
    set_key(1, 5, false); settle_ticks(12);
    CHECK(wire_empty(), "consumer release reaches wire");

    tud_hid_set_protocol_cb(0, 0);
    set_key(0, 0, true); settle_ticks(12);
    CHECK(wire_len == 8 && report_wire_has(KC_Q), "boot protocol interrupt report is eight bytes");
    CHECK(tud_hid_get_report_cb(0, 0, HID_REPORT_TYPE_INPUT, out, sizeof(out)) == 8,
          "boot protocol GET_REPORT is eight bytes");
    stub_mounted = false; tud_umount_cb();
    stub_mounted = true; tud_mount_cb(); settle_ticks(12);
    CHECK(wire_len == 10 && report_wire_has(KC_Q), "new mount resets protocol and resends held key");
    set_key(0, 0, false); settle_ticks(12);
    CHECK(wire_empty(), "reconnected held key releases");

    /* A failed press must be retried before its already-queued release. */
    report_keyboard_t tap = {0};
    tap.keys[0] = KC_A;
    wire_count = 0;
    host_keyboard_send(&tap);
    tap.keys[0] = 0;
    host_keyboard_send(&tap);
    tud_hid_report_failed_cb(0, HID_REPORT_TYPE_INPUT, stub_epin_buf, 0);
    settle_ticks(12);
    CHECK(wire_count == 3 && wire_history[0][2] == KC_A && wire_history[1][2] == KC_A && wire_empty(),
          "failed tap retries the press before the queued release");

    wire_count = 0;
    for (unsigned i = 0; i < 40; ++i) {
        tap.keys[0] = KC_A + i % 26;
        host_keyboard_send(&tap);
    }
    tap.keys[0] = 0;
    host_keyboard_send(&tap);
    settle_ticks(40);
    CHECK(wire_count == 16 && wire_history[0][2] == KC_A && wire_empty(),
          "bounded queue overload retains in-flight head and final release");
}

static void p9_missing_hid_completion(void) {
    printf("== P9 lost HID callback must not freeze matrix or trap final release ==\n");
    extern bool tud_hid_set_idle_cb(uint8_t, uint8_t);
    (void)tud_hid_set_idle_cb(0, 0); /* Do not confuse the result with SET_IDLE retransmits. */
    settle_ticks(15);

    const long before = stub_submit_count;
    set_key(0, 0, true); /* KC_Q press goes to the endpoint. */
    for (int i = 0; i < 12; ++i) loop_tick(); /* Deliberately omit USB completion callback. */
    CHECK(stub_submit_count == before + 1 && report_wire_has(KC_Q),
          "press accepted before the missing completion");

    /* The endpoint is genuinely busy. We MUST NOT resend while it is busy,
     * but scanning must continue so that the final release is recorded. */
    stub_hid_ready = false;
    const long scans_before = scan_calls;
    set_key(0, 0, false);
    for (int i = 0; i < 80; ++i) loop_tick();
    CHECK(scan_calls - scans_before >= 75,
          "matrix remains live through 80 ms without a HID completion (delta=%ld)",
          scan_calls - scans_before);
    CHECK(stub_submit_count == before + 1,
          "never resend to a busy endpoint");

    /* TinyUSB's busy bit clears but the completion callback was lost.
     * The queue must recover without assuming that the original report
     * reached the host; retry the press, then transmit its queued release. */
    stub_hid_ready = true;
    long observed = stub_submit_count;
    bool retried_press = false, delivered_release = false;
    for (int i = 0; i < 150; ++i) {
        loop_tick();
        if (stub_submit_count > observed) {
            if (report_wire_has(KC_Q)) retried_press = true;
            if (wire_empty()) delivered_release = true;
            usb_complete_hid(); /* Only newly submitted transfers get callbacks. */
            observed = stub_submit_count;
        }
    }
    CHECK(retried_press, "endpoint ready without callback triggers bounded retry");
    CHECK(delivered_release && wire_empty(),
          "final key release follows the retried report and reaches the host");
}

static void p10_consumer_tap_during_busy_endpoint(void) {
    printf("== P10 consumer tap survives a busy endpoint ==\n");
    extern bool tud_hid_set_idle_cb(uint8_t, uint8_t);
    (void)tud_hid_set_idle_cb(0, 0);
    settle_ticks(15);
    wire_count = 0;
    set_key(0, 0, true);
    for (int i = 0; i < 12; ++i) loop_tick();
    CHECK(wire_count == 1 && wire_history[0][2] == KC_Q,
          "Q press owns the endpoint before consumer tap");

    /* Q press, consumer press, and consumer release use three of 16 slots. */
    stub_hid_ready = false;
    long scans_before = scan_calls;
    set_key(5, 5, true);
    for (int i = 0; i < 12; ++i) loop_tick();
    CHECK(ck5200_consumer_usage() == 0x00B8, "consumer press reaches QMK state");
    set_key(5, 5, false);
    for (int i = 0; i < 12; ++i) loop_tick();
    CHECK(ck5200_consumer_usage() == 0, "consumer release reaches QMK state");
    CHECK(scan_calls - scans_before >= 22, "matrix scans while endpoint is busy");
    CHECK(wire_count == 1, "busy endpoint receives no extra submission");

    stub_hid_ready = true;
    for (int i = 0; i < 40; ++i) {
        unsigned before = wire_count;
        loop_tick();
        if (wire_count > before) usb_complete_hid();
    }
    const uint8_t q_plain[10] = {0, 0, KC_Q};
    const uint8_t q_eject[10] = {0, 0, KC_Q, 0, 0, 0, 0, 0, 0xB8};
    CHECK(history_has(KC_Q, 0, 0x00B8), "missing consumer usage 0x00B8 after recovery");
    CHECK(wire_count == 4 && wire_len == 10 &&
          memcmp(wire_history[1], q_plain, 10) == 0 &&
          memcmp(wire_history[2], q_eject, 10) == 0 &&
          memcmp(wire_history[3], q_plain, 10) == 0,
          "retry keeps exact Q/0000, Q/00B8, Q/0000 reports (count=%u)", wire_count);
    unsigned idle_count = wire_count;
    settle_ticks(12);
    CHECK(wire_count == idle_count, "idle does not replay the tap");
    set_key(0, 0, false);
    settle_ticks(12);
    CHECK(wire_empty(), "Q release reaches wire after consumer tap");
}

static void p11_two_consumer_crossings_one_scan(void) {
    printf("== P11 two consumer crossings in one scan stay distinct ==\n");
    settle_ticks(15);
    wire_count = 0;
    stub_hid_ready = false;
    set_key(0, 5, true); /* row order sends 0x00CF first */
    set_key(5, 5, true); /* then 0x00B8 in the same QMK matrix pass */
    for (int i = 0; i < 12; ++i) loop_tick();
    CHECK(ck5200_consumer_usage() == 0x00B8, "last crossing is active");
    set_key(0, 5, false);
    set_key(5, 5, false);
    for (int i = 0; i < 12; ++i) loop_tick();
    CHECK(ck5200_consumer_usage() == 0 && wire_count == 0,
          "same-scan releases update state while endpoint stays busy");
    stub_hid_ready = true;
    for (int i = 0; i < 12; ++i) { loop_tick(); usb_complete_hid(); }
    const uint8_t first[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0xCF};
    const uint8_t second[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0xB8};
    const uint8_t clear[10] = {0};
    CHECK(wire_count == 3 && wire_len == 10 &&
          memcmp(wire_history[0], first, 10) == 0 &&
          memcmp(wire_history[1], second, 10) == 0 &&
          memcmp(wire_history[2], clear, 10) == 0,
          "same-scan 0x00CF, 0x00B8, release reach wire in order (count=%u)", wire_count);
}

int main(int argc, char **argv) {
    set_time(0);
    setup_firmware();
    const char *which = argc > 1 ? argv[1] : "all";
    if (strcmp(which, "p1") == 0 || strcmp(which, "all") == 0) p1_chord_pending_loop();
    if (strcmp(which, "p2") == 0 || strcmp(which, "all") == 0) p2_idle_resend();
    if (strcmp(which, "p3") == 0 || strcmp(which, "all") == 0) p3_suspend_key_held();
    if (strcmp(which, "p4") == 0 || strcmp(which, "all") == 0) p4_get_report();
    if (strcmp(which, "p5") == 0 || strcmp(which, "all") == 0) p5_plain_tap();
    if (strcmp(which, "p6") == 0 || strcmp(which, "all") == 0) p6_root_port_init();
    if (strcmp(which, "p7") == 0 || strcmp(which, "all") == 0) p7_all_crossings();
    if (strcmp(which, "p8") == 0 || strcmp(which, "all") == 0) p8_report_recovery();
    if (strcmp(which, "p9") == 0 || strcmp(which, "all") == 0) p9_missing_hid_completion();
    if (strcmp(which, "p10") == 0 || strcmp(which, "all") == 0) p10_consumer_tap_during_busy_endpoint();
    if (strcmp(which, "p11") == 0 || strcmp(which, "all") == 0) p11_two_consumer_crossings_one_scan();
    printf("==== %d failure(s) ====\n", failures);
    if (!failures) printf("ok\n");
    return failures ? 1 : 0;
}
