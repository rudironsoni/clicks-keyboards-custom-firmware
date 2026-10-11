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

bool tud_mounted(void) { return stub_mounted; }
bool tud_suspended(void) { return stub_suspended; }
bool tud_hid_ready(void) { return stub_hid_ready; }
void tud_task(void) {}
void tud_init(uint8_t rhport) { (void)rhport; }
uint32_t tud_vendor_n_write(uint8_t itf, const void *buf, uint32_t len) {
    (void)itf; (void)buf; (void)len;
    return len; /* pretend the endpoint accepted */
}

bool tud_hid_report(uint8_t report_id, void const *report, uint16_t len) {
    (void)report_id;
    if (!stub_mounted || stub_suspended || !stub_hid_ready) return false;
    if (len > sizeof(stub_epin_buf)) return false;
    memcpy(stub_epin_buf, report, len);
    stub_submit_count++;
    return true;
}

/* TinyUSB-side events the harness drives. */
extern void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len);
static void usb_complete_hid(void) { tud_hid_report_complete_cb(0, stub_epin_buf, 10); }

/* ---------------- matrix injection ------------------------------------- */
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

/* ---------------- iAP2 / backlight / update stubs ----------------------- */
static bool stub_session_active;
void iap2_init(void) {}
void iap2_set_connected(bool c) { (void)c; }
bool iap2_session_active(void) { return stub_session_active; }
void iap2_tick_1ms(void) {}
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
    /* 10-byte wire report: mods, reserved, keys[6], consumer hi, consumer lo */
    for (int i = 2; i < 8; ++i)
        if (stub_epin_buf[i] == keycode) return true;
    return false;
}

/* ---------------- scenarios ---------------------------------------------- */
static void setup_firmware(void) {
    stub_mounted = false;
    stub_suspended = false;
    stub_hid_ready = true;
    keyboard_setup();
    keyboard_init();
    protocol_setup();
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

int main(int argc, char **argv) {
    set_time(0);
    setup_firmware();
    const char *which = argc > 1 ? argv[1] : "all";
    if (strcmp(which, "p1") == 0 || strcmp(which, "all") == 0) p1_chord_pending_loop();
    if (strcmp(which, "p2") == 0 || strcmp(which, "all") == 0) p2_idle_resend();
    if (strcmp(which, "p3") == 0 || strcmp(which, "all") == 0) p3_suspend_key_held();
    if (strcmp(which, "p4") == 0 || strcmp(which, "all") == 0) p4_get_report();
    if (strcmp(which, "p5") == 0 || strcmp(which, "all") == 0) p5_plain_tap();
    printf("==== %d failure(s) ====\n", failures);
    if (!failures) printf("ok\n");
    return failures ? 1 : 0;
}
