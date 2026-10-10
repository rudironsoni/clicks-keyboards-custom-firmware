/* CK-5200 Apple accessory session (iAP2/MFi) stack.
 *
 * Faithful port of the stock V122 session layer recovered from
 * iKeyboard_CK-5200_V122_120.bin; stock addresses and byte layouts are
 * recorded in .stock/audit/NOTES.md. Layers:
 *
 * 1. Outer transport (stock 0x6242/0x6150): attach handshake
 *    FF 55 02 00 EE 10 on EP2 (retried every 475 ms), then link-active
 *    state 3.
 * 2. Inner link machine (stock 0x5f9e): iAP2 link layer with FF 5A
 *    framing on EP2: sync exchange, identify (0x1D01), MFi auth relay
 *    to the authentication chip over bit-banged I2C, and power updates.
 *
 * The companion-protocol channel stays raw on EP3 while the outer state
 * is 3 (stock 0x629e/0x62cc gates), where the existing dispatcher runs.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IAP2_PKT_MAX 384u

typedef struct iap2_ctx {
    /* Inner link flags (stock transport ctx+0x30 word 0). */
    uint16_t flags;        /* bit0 their-SYN seen, bit2 auth OK, bit3 session
                              open, bit5 usb-connected mirror, bit6 copy,
                              bit11 EA session open */
    bool     connected;    /* USB configured (stock ctx+1) */
    uint8_t  state;        /* outer: 0 off, 1 attach, 3 link active */
    uint16_t attach_backoff;
    uint32_t watchdog;     /* link silence; reset to 2500 on activity */

    uint8_t  tstate;       /* inner state 0..10 */
    uint8_t  tnext;        /* state after the current op completes */
    uint8_t  tsaved;       /* state to restore after a failed-op backoff */
    uint16_t tretry;       /* backoff / resend timer in 1 ms ticks */
    uint8_t  seq_tx;       /* last sent sequence number */
    uint8_t  ack_rx;       /* last received in-sequence psn */
    uint8_t  their_ack;    /* psn of ours that they acknowledged */
    uint8_t  session_id;   /* advertised control session, 10 */
    uint8_t  resend_count; /* sync resend counter, max 798 */

    /* Current op plus the 8-slot request ring (stock 0x4f68/0x4fa8). */
    bool   (*pending_op)(void);
    struct {
        uint8_t        next;
        bool       (*op)(void);
    } ring[8];
    uint8_t ring_write, ring_read;

    /* MFi auth session object (stock ctx+0x6). */
    uint8_t  auth_addr;    /* I2C address byte, 0x22 or fallback 0x20 */
    uint8_t  peek_fail;    /* consecutive peek failures, toggles auth_addr at 20 */
    uint8_t  auth_len;     /* challenge length */
    uint8_t  auth_buf[36]; /* challenge bytes */
    uint8_t  msg_cursor;   /* streamed-message chunk cursor */
    uint8_t  msg_chunks;   /* streamed-message chunk count */
    uint16_t msg_total;   /* streamed-message total length */

    /* Shared packet buffer (stock ctx+0xcc via transport ctx+0x90). */
    uint8_t  pkt[IAP2_PKT_MAX];
} iap2_ctx_t;

extern iap2_ctx_t iap2_ctx;

/* One-time setup; also configures the auth-chip bus. */
void iap2_init(void);

/* USB configuration state (stock 0x61ea): false resets the session. */
void iap2_set_connected(bool connected);

/* 1 ms tick from the USB SOF interrupt (stock 0x61c6): retry timers. */
void iap2_tick_1ms(void);

/* Periodic step (stock 0x6242): attach resend and the inner machine. */
void iap2_task(void);

/* EP2 bulk OUT data (stock 0x46d2 -> 0x6150). Consumed by the session
 * stack when it matches the attach pattern, a link packet, or the
 * pre-session reject path. */
void iap2_ep2_rx(const uint8_t *buf, uint32_t len);

/* True when the companion channel is active on EP3 (stock 0x61f4 open
 * plus outer state 3, as used by the 0x629e/0x62cc gates). */
bool iap2_session_active(void);

/* Feed EP3 traffic into the watchdog (stock 0x629e resets it too). */
void iap2_activity(void);
