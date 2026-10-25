/* Real main + protocol + iAP2 + staging + pinned QMK. Hardware hooks and the
 * explicitly named installer model are host-only. See StockManagerBridge.h. */
#include "StockManagerBridge.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "quantum.h"
#include "eeconfig.h"
#include "tusb.h"
#include "iap2.h"
#include "iap2_auth.h"
#include "ck5200_rescue.h"
#include "ck5200_staging.h"
#include "ck5200_staging_host.h"
#include "sha-256.h"

int ck5200_main(void);
void advance_time(uint32_t ms);
void tud_mount_cb(void);
void tud_umount_cb(void);
void tud_vendor_rx_cb(uint8_t, const uint8_t *, uint32_t);
void tud_vendor_tx_cb(uint8_t, uint32_t);

static jmp_buf escape;
static bool boot_called, ready, boot_loop, mounted, reset_armed;
static bool stock_identity, installed, committed;
static int boot_mode;
static unsigned usb_tasks, matrix_inits, matrix_scans, pre_init, post_init;
static unsigned auth_inits, auth_reads, auth_writes, backlight_calls, reset_requests;
static unsigned writes, fail_countdown;
static uint8_t flash[0x10000], reply[8];
static size_t reply_length;

void platform_setup(void) {}
void mcu_reset(void) {
    ++reset_requests;
    if (!reset_armed) abort();
    longjmp(escape, 2);
}
void bootloader_jump(void) { abort(); }
void suspend_power_down_kb(void) {}
void suspend_wakeup_init_kb(void) {}
void keyboard_pre_init_user(void) { assert(boot_mode != 2); ++pre_init; }
void keyboard_post_init_user(void) { assert(boot_mode != 2); ++post_init; }
void matrix_init_custom(void) { assert(boot_mode != 2); ++matrix_inits; }
bool matrix_scan_custom(matrix_row_t rows[]) {
    (void)rows; assert(boot_mode != 2); ++matrix_scans; return false;
}
void ck5200_backlight_init(void) { assert(boot_mode != 2); ++backlight_calls; }
void ck5200_backlight_set(uint8_t raw) { (void)raw; assert(boot_mode != 2); ++backlight_calls; }
uint8_t ck5200_backlight_get(void) { assert(boot_mode != 2); return 0; }
bool tud_mounted(void) { return mounted; }
bool tud_suspended(void) { return false; }
bool tud_hid_ready(void) { return false; }
bool tud_hid_report(uint8_t id, const void *p, uint16_t n) {
    (void)id; (void)p; (void)n; assert(boot_mode != 2); return false;
}
void tud_init(uint8_t port) { assert(port == 0); }
void tud_task(void) {
    advance_time(1);
    if (boot_loop && ++usb_tasks == 8) longjmp(escape, 1);
}
uint32_t tud_vendor_n_write(uint8_t itf, const void *p, uint32_t n) {
    if (itf == 1) {
        assert(n <= sizeof(reply));
        memcpy(reply, p, n);
        reply_length = n;
        /* Only the observed version value changes in stock-reference mode. */
        if (stock_identity && n == 8 && reply[2] == 3 && reply[3] == 0) {
            const uint8_t stock_version[] = {1, 0x20, 1, 0x22};
            memcpy(reply + 4, stock_version, sizeof(stock_version));
        }
    }
    return n;
}
void iap2_auth_init(void) { ++auth_inits; }
bool iap2_auth_write(uint8_t addr, uint8_t sel, const uint8_t *p, size_t n) {
    (void)addr; (void)sel; (void)p; (void)n; ++auth_writes; return true;
}
bool iap2_auth_read(uint8_t addr, uint8_t sel, uint8_t *p, size_t n) {
    (void)addr; ++auth_reads; memset(p, 0, n);
    if (sel == IAP2_AUTH_SEL_CMD && n) p[0] = 0x70;
    return sel != IAP2_AUTH_SEL_PEEK;
}
bool staging_flash_write_page(uint32_t addr, const uint8_t data[256]) {
    assert(ck5200_staging_page_in_range(addr));
    ++writes;
    if (fail_countdown && --fail_countdown == 0) return false;
    uint8_t *page = flash + (addr - 0x08000000u);
    memset(page, 0xff, 256);
    for (unsigned i = 0; i < 256; ++i) page[i] &= data[i];
    return memcmp(page, data, 256) == 0;
}
void staging_flash_read_page(uint32_t addr, uint8_t out[256]) {
    assert(ck5200_staging_page_in_range(addr));
    memcpy(out, flash + (addr - 0x08000000u), 256);
}
static void service(unsigned n) { while (n--) ck5200_transport_task(); }
static void message(uint16_t type, const uint8_t *body, size_t n) {
    uint8_t p[80] = {0xff,0x5a,0,0,0x40,1,0,10,0,0x40,0x40,0,0,0,0};
    assert(n <= sizeof(p) - 16);
    p[3] = 16 + n; p[12] = 6 + n; p[13] = type >> 8; p[14] = type;
    if (n) memcpy(p + 15, body, n);
    for (unsigned i = 0; i < 8; ++i) p[8] -= p[i];
    for (unsigned i = 9; i < 15 + n; ++i) p[15 + n] -= p[i];
    tud_vendor_rx_cb(0, p, 16 + n);
    service(12);
}
bool sm_boot(int mode) {
    if (boot_called || mode < 0 || mode > 2) return false;
    boot_called = true;
    boot_mode = mode;
    stock_identity = mode == 0;
    memset(flash, 0xa5, sizeof(flash));
    memset(flash + 0x8a00, 0, 4);
    if (mode == 2) {
        reset_armed = true;
        if (setjmp(escape) == 0) ck5200_request_rescue();
        reset_armed = false;
        reset_requests = 0; /* Startup token creation is not an update reboot. */
    }
    boot_loop = true;
    int reason = setjmp(escape);
    if (reason == 0) ck5200_main();
    boot_loop = false;
    ready = reason == 1 && usb_tasks == 8 && auth_inits == 1 && writes == 0;
    if (mode == 2) {
        ready = ready && ck5200_rescue_active() && !pre_init && !post_init &&
            !matrix_inits && !matrix_scans && !backlight_calls && !eeconfig_is_enabled();
    } else {
        ready = ready && !ck5200_rescue_active() && pre_init == 1 && post_init == 1 &&
            matrix_inits == 1 && matrix_scans > 0 && backlight_calls && eeconfig_is_enabled();
    }
    return ready;
}
bool sm_authenticate(void) {
    if (!ready) return false;
    mounted = true;
    tud_mount_cb();
    service(2);
    const uint8_t echo[] = {0xff,0x55,2,0,0xee,0x10};
    tud_vendor_rx_cb(0, echo, sizeof(echo));
    service(3);
    unsigned before_reads = auth_reads, before_writes = auth_writes;
    const uint8_t challenge[] = {0,8,0,0,1,2,3,4};
    message(0xaa02, challenge, sizeof(challenge));
    if (auth_reads == before_reads || auth_writes == before_writes) return false;
    message(0xaa04, NULL, 0);
    message(0xea00, NULL, 0);
    return iap2_restore_authorized();
}
size_t sm_send(const uint8_t *bytes, size_t length, uint8_t response[8]) {
    if (!ready || !bytes || !response || length < 2 || length > 64) return 0;
    reply_length = 0;
    reset_armed = true;
    if (setjmp(escape) == 0) {
        tud_vendor_rx_cb(1, bytes, (uint32_t)length);
        service(1);
    }
    reset_armed = false;
    if (reply_length) {
        memcpy(response, reply, reply_length);
        tud_vendor_tx_cb(1, (uint32_t)reply_length);
        if (reply[2] == 0xa1) committed = false;
        if (reply[2] == 0xa3) committed = reply[3] == 0;
    }
    return reply_length;
}
void sm_close_session(void) {
    if (!ready || !mounted) return;
    message(0xea01, NULL, 0);
    if (boot_mode == 2) committed = false;
}
void sm_advance_time(uint32_t milliseconds) {
    if (!ready) return;
    advance_time(milliseconds);
    service(1);
    if (boot_mode == 2 && !iap2_restore_authorized()) committed = false;
}
void sm_disconnect(void) {
    if (!ready) return;
    sm_close_session();
    mounted = false;
    tud_umount_cb();
    /* Explicit host lifecycle cleanup. QM does not do this automatically. */
    ck5200_staging_abort();
    committed = false;
}
void sm_fail_write(int nth) { fail_countdown = nth > 0 ? (unsigned)nth : 0; }
uint32_t sm_write_count(void) { return writes; }
uint32_t sm_reboot_count(void) { return staging_host_reboots + reset_requests; }
uint32_t sm_marker(void) {
    return (uint32_t)flash[0x8a00] | ((uint32_t)flash[0x8a01] << 8) |
        ((uint32_t)flash[0x8a02] << 16) | ((uint32_t)flash[0x8a03] << 24);
}
bool sm_staged_matches(const uint8_t *bytes, size_t length) {
    return ready && bytes && length > 0 && length <= 0x6a00 &&
        memcmp(flash + 0x8a04, bytes, length) == 0;
}
bool sm_protected_intact(void) {
    if (!ready) return false;
    for (unsigned i = 0; i < sizeof(flash); ++i) {
        bool protected = i < (installed ? 0x2000u : 0x8a00u) || i >= 0xf500;
        if (protected && flash[i] != 0xa5) return false;
    }
    return true;
}
bool sm_model_install(void) {
    static const uint8_t official_sha[32] = {
        0x8e,0xe8,0x69,0x35,0xf5,0xfb,0xd6,0x22,0x97,0x2f,0xa5,0x50,0x33,0xf2,0x9f,0xa5,
        0x51,0xba,0x57,0x22,0x88,0x12,0x30,0x46,0xe2,0x1d,0x79,0x00,0x6f,0x3b,0x22,0xf8
    };
    if (!ready || !committed || sm_marker() != 18648 || !sm_protected_intact()) return false;
    uint8_t digest[32];
    calc_sha_256(digest, flash + 0x8a04, 18648);
    if (memcmp(digest, official_sha, sizeof(digest))) return false;
    /* This copy is a host assumption, NOT recovered proprietary installer code. */
    memcpy(flash + 0x2000, flash + 0x8a04, 18648);
    installed = true;
    if (memcmp(flash + 0x2000, flash + 0x8a04, 18648) || !sm_protected_intact()) return false;
    stock_identity = true;
    committed = false;
    return true;
}
