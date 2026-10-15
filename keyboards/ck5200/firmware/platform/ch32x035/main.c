/* Copyright 2021 QMK
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Normal loop adapted from pinned QMK quantum/main.c.
 * Project-owned override of QMK's weak main. Normal order matches pinned QMK.
 * The one-shot RAM request is NOT an independent boot/recovery mechanism.
 * Retention through the proprietary boot stage is unverified on hardware. */
#include "keyboard.h"
#include "ck5200_rescue.h"
#include "ck5200_staging.h"
#include <stdint.h>

void platform_setup(void);
void mcu_reset(void);
void protocol_setup(void);
void protocol_pre_init(void);
void protocol_post_init(void);
void protocol_pre_task(void);
void protocol_keyboard_task(void);
void protocol_post_task(void);

/* NOLOAD and outside the startup BSS clear. No flash flag or option byte. */
static volatile uint32_t rescue_request[2]
#ifndef CK5200_RESCUE_HOST_TEST
    __attribute__((section(".noinit")))
#endif
    ;
static bool rescue;

bool ck5200_rescue_active(void) { return rescue; }

void ck5200_request_rescue(void) {
    rescue_request[0] = 0x43524D32u;
    rescue_request[1] = ~0x43524D32u;
    mcu_reset();
}

int main(void) {
    rescue = rescue_request[0] == 0x43524D32u && rescue_request[1] == ~0x43524D32u;
    rescue_request[0] = rescue_request[1] = 0; /* Consume before any init. */
    platform_setup();
    if (rescue) {
        ck5200_staging_require_stock();
        ck5200_transport_setup();
        for (;;) ck5200_transport_task();
    }

    protocol_setup();
    keyboard_setup();
    protocol_pre_init();
    keyboard_init();
    protocol_post_init();
    for (;;) {
        protocol_pre_task();
        protocol_keyboard_task();
        protocol_post_task();
#ifdef RAW_ENABLE
        void raw_hid_task(void);
        raw_hid_task();
#endif
#ifdef CONSOLE_ENABLE
        void console_task(void);
        console_task();
#endif
#ifdef QUANTUM_PAINTER_ENABLE
        void qp_internal_task(void);
        qp_internal_task();
#endif
#ifdef DEFERRED_EXEC_ENABLE
        void deferred_exec_task(void);
        deferred_exec_task();
#endif
        housekeeping_task();
    }
}
