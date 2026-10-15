/* Actual production main + protocol + iAP2 + staging + pinned QMK core.
 * Only hardware boundaries (USB, auth chip, flash, matrix, reset) are modeled. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
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

int ck5200_main(void);
void advance_time(uint32_t ms);
void tud_mount_cb(void);
void tud_umount_cb(void);
void tud_vendor_rx_cb(uint8_t, const uint8_t *, uint32_t);
void tud_vendor_tx_cb(uint8_t, uint32_t);

static jmp_buf escape;
static bool expect_rescue, stop_loop, fail_platform, mounted;
static unsigned usb_tasks, matrix_inits, matrix_scans, pre_init, post_init;
static unsigned auth_init, auth_reads, auth_writes, usb_packets, backlight_calls;
static uint8_t flash[0x10000], response[8];
static unsigned response_len, writes;

void platform_setup(void) { if (fail_platform) longjmp(escape, 3); }
void mcu_reset(void) { longjmp(escape, 2); }
void bootloader_jump(void) { assert(false); }
void suspend_power_down_kb(void) {}
void suspend_wakeup_init_kb(void) {}
void keyboard_pre_init_user(void) { assert(!expect_rescue); ++pre_init; }
void keyboard_post_init_user(void) { assert(!expect_rescue); ++post_init; }
void matrix_init_custom(void) { assert(!expect_rescue); ++matrix_inits; }
bool matrix_scan_custom(matrix_row_t rows[]) {
    (void)rows; assert(!expect_rescue); ++matrix_scans; return false;
}
void ck5200_backlight_init(void) { assert(!expect_rescue); ++backlight_calls; }
void ck5200_backlight_set(uint8_t raw) { (void)raw; assert(!expect_rescue); ++backlight_calls; }
uint8_t ck5200_backlight_get(void) { assert(!expect_rescue); return 0; }

bool tud_mounted(void) { return mounted; }
bool tud_suspended(void) { return false; }
bool tud_hid_ready(void) { return false; }
bool tud_hid_report(uint8_t id, const void *p, uint16_t n) {
    (void)id; (void)p; (void)n; assert(!expect_rescue); return false;
}
void tud_init(uint8_t port) { assert(port == 0); }
void tud_task(void) {
    advance_time(1);
    if (stop_loop && ++usb_tasks == 8) longjmp(escape, 1);
}
uint32_t tud_vendor_n_write(uint8_t itf, const void *p, uint32_t n) {
    ++usb_packets;
    if (itf == 1) { assert(n <= 8); memcpy(response, p, n); response_len = n; }
    return n;
}
void iap2_auth_init(void) { ++auth_init; }
bool iap2_auth_write(uint8_t addr, uint8_t sel, const uint8_t *p, size_t n) {
    (void)addr; (void)sel; (void)p; (void)n; ++auth_writes; return true;
}
bool iap2_auth_read(uint8_t addr, uint8_t sel, uint8_t *p, size_t n) {
    (void)addr; ++auth_reads; memset(p, 0, n);
    if (sel == IAP2_AUTH_SEL_CMD) p[0] = 0x70;
    return sel != IAP2_AUTH_SEL_PEEK;
}
bool staging_flash_write_page(uint32_t addr, const uint8_t data[256]) {
    assert(addr >= 0x08008A00 && addr <= 0x0800F400 && !(addr & 255));
    ++writes; memcpy(flash + (addr - 0x08000000), data, 256); return true;
}
void staging_flash_read_page(uint32_t addr, uint8_t out[256]) {
    assert(ck5200_staging_page_in_range(addr));
    memcpy(out, flash + (addr - 0x08000000), 256);
}

static void service(unsigned n) { while (n--) ck5200_transport_task(); }
static void word(uint8_t *p, uint32_t n) {
    p[0] = n >> 24; p[1] = n >> 16; p[2] = n >> 8; p[3] = n;
}
static unsigned command(uint8_t cmd, uint32_t value, const uint8_t *data, size_t n) {
    uint8_t packet[64] = {6, cmd}; word(packet + 2, value);
    if (cmd == 0xA2) { packet[0] += n; memcpy(packet + 6, data, n); }
    if (cmd == 0xA0 || cmd == 0x03 || cmd == 0x94) packet[0] = 2;
    response_len = 0;
    tud_vendor_rx_cb(1, packet, packet[0]); service(1);
    if (response_len) tud_vendor_tx_cb(1, response_len);
    return response_len ? response[3] : 255;
}
/* Phone control messages enter through the real EP2 parser, not context writes. */
static void message(uint16_t type, const uint8_t *body, size_t n) {
    uint8_t p[80] = {0xff,0x5a,0,0,0x40,1,0,10,0,0x40,0x40,0,0,0,0};
    p[3] = 16 + n; p[12] = 6 + n; p[13] = type >> 8; p[14] = type;
    if (n) memcpy(p + 15, body, n);
    for (unsigned i = 0; i < 8; ++i) p[8] -= p[i];
    for (unsigned i = 9; i < 15 + n; ++i) p[15+n] -= p[i];
    tud_vendor_rx_cb(0, p, 16+n); service(12);
}
static void connect_phone(void) {
    mounted = true; tud_mount_cb(); service(2);
    const uint8_t echo[] = {0xff,0x55,2,0,0xee,0x10};
    tud_vendor_rx_cb(0, echo, sizeof(echo)); service(3);
    if (expect_rescue) assert(command(0xA1, 18648, NULL, 0) == 255);
    const uint8_t challenge[] = {0,8,0,0,1,2,3,4};
    message(0xaa02, challenge, sizeof(challenge));
    assert(auth_writes && auth_reads);
    message(0xaa04, NULL, 0); message(0xea00, NULL, 0);
    assert(iap2_restore_authorized());
}
static void transfer(const uint8_t *image, size_t len) {
    assert(command(0xA1, len, NULL, 0) == 0);
    for (size_t off = 0; off < len;) {
        size_t n = len-off > 32 ? 32 : len-off;
        assert(command(0xA2, off, image+off, n) == 0); off += n;
    }
}
static uint32_t marker(void) { uint32_t v; memcpy(&v, flash+0x8A00, 4); return v; }

int main(int argc, char **argv) {
    assert(argc == 3);
    expect_rescue = strcmp(argv[1], "normal") != 0;
    memset(flash, 0xA5, sizeof(flash));
    memset(flash+0x8A00, 0, 4); /* Fixture: no pending install. */
    if (expect_rescue) {
        /* Actual software entry request sets the production one-shot token. */
        if (setjmp(escape) == 0) ck5200_request_rescue();
    }
    fail_platform = strcmp(argv[1], "early-failure") == 0;
    stop_loop = true;
    int exit_reason = setjmp(escape);
    if (exit_reason == 0) ck5200_main();
    stop_loop = false;
    assert(writes == 0);
    if (fail_platform) {
        assert(exit_reason == 3 && auth_init == 0 && usb_tasks == 0);
        puts("startup failure before transport: outside rescue guarantee"); return 0;
    }
    assert(exit_reason == 1 && auth_init == 1 && usb_tasks == 8);
    if (!expect_rescue) {
        assert(pre_init == 1 && post_init == 1 && matrix_inits == 1 && matrix_scans > 0);
        assert(backlight_calls && eeconfig_is_enabled());
        assert(command(0x94, 0, NULL, 0) == 255); /* No unauthenticated reset. */
        connect_phone();
        memset(flash+0x8A00, 0xA5, 4);
        assert(command(0x94, 0, NULL, 0) == 255); /* Unknown/pending marker blocks reset. */
        memset(flash+0x8A00, 0, 4);
        int reset_reason = setjmp(escape);
        if (!reset_reason) { command(0x94, 0, NULL, 0); assert(false); }
        assert(reset_reason == 2 && writes == 0);
        puts("normal production entry reaches real QMK init/tasks; authenticated RAM request resets only"); return 0;
    }
    assert(ck5200_rescue_active());
    assert(!pre_init && !post_init && !matrix_inits && !matrix_scans && !backlight_calls);
    assert(!eeconfig_is_enabled());
    assert(command(0xA1, 18648, NULL, 0) == 255 && writes == 0);
    const uint8_t raw_update[] = {6,0xA1,0,0,0x48,0xD8};
    tud_vendor_rx_cb(0, raw_update, sizeof(raw_update));
    assert(writes == 0);
    connect_phone(); assert(usb_packets);
    tud_vendor_rx_cb(0, raw_update, sizeof(raw_update));
    assert(writes == 0); /* No EP2 bypass, including before authentication. */
    assert(command(0x03, 0, NULL, 0) == 0 && response[4] == 'R' && response[5] == 'S');
    assert(command(0xA0, 0, NULL, 0) == 255 && staging_host_reboots == 0);
    assert(command(0xA1, 32, NULL, 0) != 0 && writes == 0);
    FILE *f = fopen(argv[2], "rb"); assert(f);
    uint8_t image[18648]; assert(fread(image,1,sizeof(image),f) == sizeof(image)); fclose(f);
    image[100] ^= 1; transfer(image, sizeof(image));
    assert(command(0xA3, sizeof(image), NULL, 0) != 0); /* RED before digest validation. */
    assert(marker() != sizeof(image));
    image[100] ^= 1;
    assert(command(0xA1, sizeof(image), NULL, 0) == 0);
    assert(command(0xA2, 0, image, 32) == 0);
    assert(command(0xA3, sizeof(image), NULL, 0) != 0);
    mounted = false; tud_umount_cb(); connect_phone();
    assert(command(0xA2, 32, image+32, 32) != 0);
    assert(command(0xA3, sizeof(image), NULL, 0) != 0);
    assert(command(0xA1, sizeof(image), NULL, 0) == 0);
    message(0xea01, NULL, 0); message(0xaa04, NULL, 0); message(0xea00, NULL, 0);
    assert(command(0xA2, 0, image, 32) != 0); /* Session close cancels the transaction. */
    assert(command(0xA1, sizeof(image), NULL, 0) == 0);
    service(2600); /* Watchdog expiry must cancel even without unplugging. */
    message(0xaa04, NULL, 0); message(0xea00, NULL, 0);
    assert(command(0xA2, 0, image, 32) != 0);
    transfer(image, sizeof(image));
    /* Detect corruption in stored flash, not just on the transport. */
    flash[0x8A04+100] ^= 1;
    assert(command(0xA3, sizeof(image), NULL, 0) != 0);
    transfer(image, sizeof(image));
    assert(command(0xA3, sizeof(image), NULL, 0) == 0 && marker() == sizeof(image));
    assert(memcmp(flash+0x8A04, image, sizeof(image)) == 0);
    mounted = false; tud_umount_cb(); connect_phone();
    command(0xA0, 0, NULL, 0); assert(staging_host_reboots == 0);
    transfer(image, sizeof(image)); assert(command(0xA3, sizeof(image), NULL, 0) == 0);
    command(0xA0, 0, NULL, 0); assert(staging_host_reboots == 1);
    for (unsigned i=0; i<sizeof(flash); ++i)
        if (i < 0x8A00 || i >= 0xF500) assert(flash[i] == 0xA5);
    puts("rescue production entry, stock-only commit, reconnect cancellation, and staging bounds pass");
}
