/* CK-5200 Apple accessory session (iAP2/MFi) stack.
 * Faithful port of the stock V122 session layer; see .stock/audit/NOTES.md
 * for the per-function stock addresses. Timing constants, packet bytes,
 * retry rules, and the auth-chip protocol all match the stock image.
 */
#include "iap2.h"
#include "iap2_auth.h"
#include "iap2_ident_data.h"
#include "tusb.h"
#include <string.h>

iap2_ctx_t iap2_ctx;

/* Link parameters advertised in the sync request (stock ctx+0x8..0x16). */
#define L_MAX_OUTGOING     5u
#define L_MAX_LEN          4096u
#define L_RETRANSMIT_MS    2000u
#define L_ACK_TIMEOUT_MS   500u
#define L_MAX_RETRANSMIT   30u
#define L_MAX_ACK          3u
#define L_SESSION_ID       10u
#define L_SYNC_RESEND_MS   (L_RETRANSMIT_MS / 2u)
#define L_SYNC_RESEND_MAX  30u  /* stock compares the counter byte to the
                                 * max_retransmissions low byte (0x1E) */

#define FAIL_BACKOFF_TICKS 10u
#define WATCHDOG_TICKS     2500u
#define ATTACH_RETRY_OK    475u
#define ATTACH_RETRY_FAIL  50u
#define CHUNK_SIZE         128u

/* Stock image 0x68d0 and 0x64ec. */
static const uint8_t attach_pkt[6] = {0xff, 0x55, 0x02, 0x00, 0xee, 0x10};
static const uint8_t reject_pkt[18] =
    {0xff, 0x55, 0x0e, 0x00, 0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xeb, 0x00};
/* Status report payload: stock reads a runtime global at gp-0x7f8 whose
 * image initializer (0x68c8) is {08 34 01 00 00 01}; the status op reads
 * the {08 34} pair and the 0x01 byte. */
static const uint8_t ident_status0[2] = {0x08, 0x34};
static const uint8_t ident_status1[1] = {0x01};

/* Ack value carried by our most recent link packet (stock ctx+0x15). */
static uint8_t ack_sent;

static uint8_t cksum8(const uint8_t *p, uint32_t n) {
    uint8_t s = 0;
    while (n--) s = (uint8_t)(s + *p++);
    return (uint8_t)(0u - s);
}

static bool ep2_send(const uint8_t *buf, uint16_t len) {
    if (!tud_mounted() || tud_suspended()) return false;
    return tud_vendor_n_write(0, buf, len) == len;
}

/* Link header (stock 0x5534). payload_len counts payload bytes including
 * the trailing payload checksum byte; 0 sends a bare ACK. */
static uint16_t link_header(uint8_t *buf, uint16_t max, uint8_t control,
                            uint8_t seq, uint8_t ack, uint8_t session,
                            uint16_t payload_len) {
    const uint16_t total = (uint16_t)(payload_len + 9u);
    if (!buf || max < total) return 0;
    buf[0] = 0xff;
    buf[1] = 0x5a;
    buf[2] = (uint8_t)(total >> 8);
    buf[3] = (uint8_t)total;
    buf[4] = control;
    buf[5] = seq;
    buf[6] = ack;
    buf[7] = session;
    buf[8] = cksum8(buf, 8);
    return total;
}

/* KVP entry (stock 0x53e4). src NULL writes only the header; the value is
 * expected to already sit at dst+4. */
static uint16_t build_param(uint8_t *dst, uint16_t max, uint8_t id,
                           const uint8_t *src, uint16_t len) {
    const uint16_t entry = (uint16_t)(len + 4u);
    if (!dst || max < entry) return 0;
    dst[0] = (uint8_t)(entry >> 8);
    dst[1] = (uint8_t)entry;
    dst[2] = 0;
    dst[3] = id;
    if (src && len) memmove(dst + 4, src, len);
    return entry;
}

/* Control message envelope (stock 0x53b0): 40 40 <len> <type>. */
static uint16_t build_msg(uint8_t *dst, uint16_t max, uint16_t type,
                          uint16_t body_len) {
    const uint16_t frame = (uint16_t)(body_len + 7u);
    if (!dst || max < frame) return 0;
    dst[0] = 0x40;
    dst[1] = 0x40;
    dst[2] = (uint8_t)((body_len + 6u) >> 8);
    dst[3] = (uint8_t)(body_len + 6u);
    dst[4] = (uint8_t)(type >> 8);
    dst[5] = (uint8_t)type;
    return frame;
}

/* Common tail for data ops: control 0x40, seq = last+1, ack = last received,
 * control session 10 (stock 0x5534 call sites). */
static bool send_payload(uint16_t payload_len) {
    iap2_ctx_t *c = &iap2_ctx;
    const uint8_t seq = (uint8_t)(c->seq_tx + 1u);
    const uint16_t total = link_header(c->pkt, IAP2_PKT_MAX, 0x40, seq,
                                       c->ack_rx, c->session_id, payload_len);
    if (total == 0) return false;
    if (!ep2_send(c->pkt, total)) return false;
    c->seq_tx = seq;
    ack_sent = c->ack_rx;
    return true;
}

static void queue_req(uint8_t next, bool (*op)(void)) {
    iap2_ctx_t *c = &iap2_ctx;
    const uint8_t w = c->ring_write;
    if (((uint8_t)(w + 1u) & 7u) == c->ring_read) return; /* stock drops when full */
    c->ring[w].next = next;
    c->ring[w].op = op;
    c->ring_write = (uint8_t)((w + 1u) & 7u);
}

/* --- pending ops ------------------------------------------------- */

/* Stock 0x542a: LinkSyncRequest, or SYN|ACK when their SYN was seen. */
static bool op_send_sync(void) {
    iap2_ctx_t *c = &iap2_ctx;
    uint8_t *const pkt = c->pkt;
    const bool syn_ack = (c->flags & 1u) != 0;
    const uint8_t control = syn_ack ? 0xc0 : 0x80;
    const uint8_t seq = (uint8_t)(c->seq_tx + 1u);
    const uint8_t ack = syn_ack ? c->ack_rx : 0;
    uint8_t *const pl = pkt + 9;

    c->flags &= (uint16_t)~1u;
    pl[0] = 0x01;
    pl[1] = L_MAX_OUTGOING;
    pl[2] = (uint8_t)(L_MAX_LEN >> 8);
    pl[3] = (uint8_t)L_MAX_LEN;
    pl[4] = (uint8_t)(L_RETRANSMIT_MS >> 8);
    pl[5] = (uint8_t)L_RETRANSMIT_MS;
    pl[6] = (uint8_t)(L_ACK_TIMEOUT_MS >> 8);
    pl[7] = (uint8_t)L_ACK_TIMEOUT_MS;
    pl[8] = L_MAX_RETRANSMIT;
    pl[9] = L_MAX_ACK;
    pl[10] = L_SESSION_ID;
    pl[11] = 0;
    pl[12] = 0x01;
    pl[13] = cksum8(pl, 13);

    (void)link_header(pkt, IAP2_PKT_MAX, control, seq, ack, 0, 14);
    if (!ep2_send(pkt, 23)) return false;

    c->seq_tx = seq;
    /* Await their ACK of the sync, resending on the stock cadence. */
    c->tnext = 3;
    c->resend_count = 0;
    c->tretry = L_SYNC_RESEND_MS;
    return true;
}

/* Stock 0x5584/0x62ea: stream pending auth-chip messages as 0xAA01. */
static bool op_pump(void) {
    iap2_ctx_t *c = &iap2_ctx;
    uint8_t *const pkt = c->pkt;

    if (c->msg_cursor < c->msg_chunks) {
        uint16_t chunk;
        if ((uint32_t)c->msg_cursor * CHUNK_SIZE + CHUNK_SIZE < c->msg_total) {
            chunk = CHUNK_SIZE;
        } else {
            chunk = (uint16_t)(c->msg_total - (uint16_t)c->msg_cursor * CHUNK_SIZE);
        }
        if (!iap2_auth_read(c->auth_addr,IAP2_AUTH_SEL_CHUNK_BASE + c->msg_cursor, pkt + 9, chunk)) return false;
        c->msg_cursor++;
        pkt[9 + chunk] = cksum8(pkt + 9, chunk);
        return send_payload((uint16_t)(chunk + 1u));
    }

    /* Previous message fully sent: peek for the next one (stock 0x62ea). */
    uint8_t peek[2] = {0, 0};
    if (!iap2_auth_read(c->auth_addr,IAP2_AUTH_SEL_PEEK, peek, 2)) {
        if (c->peek_fail < 20) c->peek_fail++;
        if (c->peek_fail == 20) {
            /* Stock toggles the chip address after 20 failed peeks. */
            c->peek_fail = 0;
            c->auth_addr = (c->auth_addr == 0x22) ? 0x20 : 0x22;
        }
        return false; /* stock retries after the failure backoff */
    }
    c->msg_total = (uint16_t)((peek[0] << 8) | peek[1]);
    if (c->msg_total == 0) return false;

    /* First chunk: message header + KVP header + 128 bytes (stock always
     * reads 128 for the first chunk) + checksum = 139 payload bytes. */
    if (!iap2_auth_read(c->auth_addr,IAP2_AUTH_SEL_CHUNK_BASE, pkt + 0x13, CHUNK_SIZE)) return false;
    (void)build_param(pkt + 0x0f, (uint16_t)(IAP2_PKT_MAX - 0x0f), 0, NULL, c->msg_total);
    (void)build_msg(pkt + 9, (uint16_t)(IAP2_PKT_MAX - 9), 0xaa01,
                    (uint16_t)(c->msg_total + 4u));
    pkt[9 + 138] = cksum8(pkt + 9, 138);
    c->msg_cursor = 1;
    c->msg_chunks = (uint8_t)((c->msg_total + CHUNK_SIZE - 1u) / CHUNK_SIZE);
    return send_payload(139u);
}

/* Shared tail: checksum the control message at pkt+9 and send it. */
static bool finish_frame(uint16_t frame) {
    iap2_ctx_t *c = &iap2_ctx;
    if (frame == 0) return false;
    c->pkt[9 + frame - 1] = cksum8(c->pkt + 9, frame - 1);
    return send_payload(frame);
}

/* Control message with a parameter list built at pkt+15. */
typedef struct {
    uint8_t          id;
    uint8_t          len;
    const uint8_t   *data;
} iap2_kvp_t;

static bool send_ctrl_msg(uint16_t type, const iap2_kvp_t *kv, uint8_t count) {
    iap2_ctx_t *c = &iap2_ctx;
    uint16_t used = 0;

    for (uint8_t i = 0; i < count; ++i) {
        const uint16_t entry = build_param(c->pkt + 15 + used,
                                           (uint16_t)(IAP2_PKT_MAX - 15 - used),
                                           kv[i].id, kv[i].data, kv[i].len);
        if (entry == 0) return false;
        used = (uint16_t)(used + entry);
    }
    return finish_frame(build_msg(c->pkt + 9, (uint16_t)(IAP2_PKT_MAX - 9), type, used));
}

/* Stock 0x569e: reply 0xAA03 with the auth-chip response to the challenge. */
static bool op_auth_reply(void) {
    iap2_ctx_t *c = &iap2_ctx;
    uint8_t *const pkt = c->pkt;
    uint8_t lenb[2] = {0, 0};

    if (!iap2_auth_read(c->auth_addr,IAP2_AUTH_SEL_LEN, lenb, 2)) return false;
    uint16_t n = (uint16_t)((lenb[0] << 8) | lenb[1]);
    if (n > (uint16_t)(IAP2_PKT_MAX - 0x14)) n = (uint16_t)(IAP2_PKT_MAX - 0x14);
    if (!iap2_auth_read(c->auth_addr,IAP2_AUTH_SEL_DATA, pkt + 0x13, n)) return false;

    (void)build_param(pkt + 0x0f, (uint16_t)(IAP2_PKT_MAX - 0x0f), 0, NULL, n);
    return finish_frame(build_msg(pkt + 9, (uint16_t)(IAP2_PKT_MAX - 9), 0xaa03,
                                  (uint16_t)(n + 4u)));
}

/* Stock 0x5734: Identify 0x1D01 with the image parameter table. */
static bool op_identify(void) {
    iap2_ctx_t *c = &iap2_ctx;
    uint8_t *const pkt = c->pkt;
    uint16_t used = 0;

    for (unsigned i = 0; i < IAP2_IDENT_PARAM_COUNT; ++i) {
        const iap2_ident_param_t *p = &iap2_ident_params[i];
        const uint16_t entry = build_param(pkt + 15 + used,
                                           (uint16_t)(IAP2_PKT_MAX - 15 - used),
                                           p->id, p->data, p->len);
        if (entry == 0) return false;
        used = (uint16_t)(used + entry);
    }

    return finish_frame(build_msg(pkt + 9, (uint16_t)(IAP2_PKT_MAX - 9), 0x1d01, used));
}

/* Stock 0x59b4: status report 0xAA03 with three runtime bytes. */
static bool op_status(void) {
    static const iap2_kvp_t kv[] = {
        {0, sizeof(ident_status0), ident_status0},
        {1, sizeof(ident_status1), ident_status1},
    };
    return send_ctrl_msg(0xaa03, kv, 2);
}

/* Stock 0x5a6e: EA announce 0xEA02 carrying the app id string. */
static bool op_ea_announce(void) {
    static const uint8_t app_id[24] = {
        'c', 'o', 'm', '.', 'c', 'l', 'i', 'c', 'k', 's', 'c', 'o', 'm',
        'p', 'a', 'n', 'i', 'o', 'n', '.', 'a', 'p', 'p', 0};
    static const iap2_kvp_t kv[] = {{0, sizeof(app_id), app_id}};
    return send_ctrl_msg(0xea02, kv, 1);
}

/* Stock 0x5aea: StartPowerUpdates 0xAE00 with an empty parameter 4. */
static bool op_power_start(void) {
    const iap2_kvp_t kv = {4, 0, NULL};
    return send_ctrl_msg(0xae00, &kv, 1);
}

/* Stock 0x5b62: StopPowerUpdates 0xAE02, empty body. */
static bool op_power_stop(void) { return send_ctrl_msg(0xae02, NULL, 0); }

/* Stock 0x5bc0: bare ACK link packet. */
static bool op_bare_ack(void) { return send_payload(0); }

/* --- receive path ------------------------------------------------ */

/* Transport re-init (stock 0x5f18). Stock stores p0/p1 into flag bits 4/5. */
static void transport_init(bool p0, bool p1) {
    iap2_ctx_t *c = &iap2_ctx;

    c->flags = (uint16_t)((p0 ? 0x10u : 0u) | (p1 ? 0x20u : 0u));
    c->tstate = 1;
    c->tnext = 0;
    c->tsaved = 0;
    c->tretry = 0;
    c->seq_tx = 0xff; /* stock ctx+0x13 init 0xff: first data packet is seq 0 */
    c->ack_rx = 0;
    c->their_ack = 0xff;
    c->resend_count = 0;
    c->ring_read = c->ring_write = 0;
    c->pending_op = op_send_sync;
    c->msg_cursor = 0;
    c->msg_chunks = 0;
    c->msg_total = 0;
    c->auth_len = 0;
    c->auth_addr = 0x22;
    c->peek_fail = 0;
    ack_sent = 0;
}

/* Challenge delivery (stock 0x5e2c..0x5e88 + the 0x5e18 queue): walks the
 * KVP list, copies param 0's value (stock's shifted window, capped at 32)
 * into the session object, then ACKs and enters the auth relay at state 5. */
static void deliver_challenge(iap2_ctx_t *c, const uint8_t *body, uint16_t body_len) {
    uint16_t off = 0;
    while (off + 4u <= body_len) {
        const uint16_t entry = (uint16_t)((body[off] << 8) | body[off + 1]);
        const uint16_t id = (uint16_t)((body[off + 2] << 8) | body[off + 3]);
        if (id == 0) {
            uint16_t n = 0;
            const uint16_t value_len = (uint16_t)(entry - 4u);
            /* Stock copies from KVP header byte 3 (value shifted by one,
             * including the id low byte) and stops after 33 copies. The
             * off+3 bound keeps reads inside the received buffer. */
            while (n < value_len && n != 33 &&
                   off + n + 3u < body_len && n + 1u < sizeof(c->auth_buf)) {
                c->auth_buf[n] = body[off + n + 3];
                n++;
            }
            c->auth_len = (n == 33) ? 32 : (uint8_t)n;
            queue_req(5, op_bare_ack); /* ACK the challenge, then auth at 5 */
            return;
        }
        if (entry < 4) return;
        off = (uint16_t)(off + entry);
    }
}

static void dispatch_msg(iap2_ctx_t *c, uint16_t type, const uint8_t *body,
                         uint16_t body_len) {
    switch (type) {
    case 0xaa00: /* certificate request: ACK, then stream the chip message */
        c->msg_cursor = 0;
        c->msg_chunks = 0;
        queue_req(4, op_bare_ack);
        break;
    case 0x1d00: /* RequestIdentify */
        queue_req(0, op_identify);
        break;
    case 0xaa02: /* MFi challenge */
        deliver_challenge(c, body, body_len);
        break;
    case 0x1d02: /* session open */
        c->flags = (uint16_t)((c->flags & (uint16_t)~0x409u) | 0x8u);
        queue_req(8, op_power_start);
        break;
    case 0xae01: /* PowerUpdate from the phone: stock answers stop */
        queue_req(0, op_power_stop);
        break;
    case 0xaa04: /* authentication accepted */
        c->flags |= 0x4u;
        break;
    case 0xea00: /* EA session open */
        c->flags = (uint16_t)((c->flags & (uint16_t)~0x1900u) | 0x800u);
        break;
    case 0xea01: /* session end */
        c->flags &= (uint16_t)~0x7ffu;
        break;
    default:
        break;
    }
}

/* Link parser (stock 0x5bec). */
static void link_rx(iap2_ctx_t *c, const uint8_t *buf, uint32_t len) {
    if (!buf || len <= 8 || buf[0] != 0xff || buf[1] != 0x5a) return;

    const uint16_t pkt_len = (uint16_t)((buf[2] << 8) | buf[3]);
    const uint8_t control = buf[4];
    c->ack_rx = buf[5];
    if (control & 0x40u) c->their_ack = buf[6];

    if (control & 0x80u) { /* SYN: validate their echo of our parameters */
        const uint8_t *pl = buf + 9;
        bool match = pkt_len >= 21;
        if (match) {
            match = ((pl[4] << 8) | pl[5]) == L_RETRANSMIT_MS &&
                    ((pl[6] << 8) | pl[7]) == L_ACK_TIMEOUT_MS &&
                    pl[8] == L_MAX_RETRANSMIT && pl[9] == L_MAX_ACK &&
                    pl[10] == L_SESSION_ID;
        }
        if (match) {
            queue_req(0, op_bare_ack);
        } else {
            c->flags |= 1u; /* restate our sync as SYN|ACK */
            queue_req(0, op_send_sync);
        }
        return;
    }
    if (control & 0x20u) return; /* EAK: out-of-sequence notice */
    if (control & 0x10u) {      /* RST: restart the link */
        transport_init(false, c->connected);
        queue_req(0, op_send_sync);
        return;
    }
    if (control & 0x08u) {
        c->flags |= 2u;
        return;
    }
    if (!(control & 0x40u)) return; /* pure ACK of our sync */

    /* ACK carrying control-session data. */
    if (pkt_len <= 9 || buf[7] != c->session_id) return;
    const uint8_t *pl = buf + 9;
    if (pl[0] != 0x40 || pl[1] != 0x40) return;
    const uint16_t msg_len = (uint16_t)((pl[2] << 8) | pl[3]);
    const uint16_t msg_type = (uint16_t)((pl[4] << 8) | pl[5]);
    if (msg_len < 6) return;
    /* Stock trusts the declared length (a known stock parser defect);
     * clamp to the received bytes so no read can pass the buffer. An empty
     * body still dispatches: stock gates on payload > 6, not body > 0. */
    uint16_t body_len = (uint16_t)(msg_len - 6u);
    const uint16_t avail = (uint16_t)((len > 15u) ? (len - 15u) : 0u);
    if (body_len > avail) body_len = avail;
    dispatch_msg(c, msg_type, pl + 6, body_len);
}

/* Pre-session parser (stock 0x6178/0x61ac). */
static void presession_rx(iap2_ctx_t *c, const uint8_t *buf, uint32_t len) {
    if (len >= 6 && buf[2] == 0x02 && buf[3] == 0x00 && buf[4] == 0xee &&
        buf[5] == 0x10) {
        transport_init(false, c->connected);
        c->state = 3;
        c->watchdog = WATCHDOG_TICKS;
        return;
    }
    (void)ep2_send(reject_pkt, sizeof(reject_pkt));
    c->state = 0;
}

/* --- state machines ----------------------------------------------- */

static void enter_fail(iap2_ctx_t *c) {
    c->tsaved = c->tstate;
    c->tstate = 2;
    c->tretry = FAIL_BACKOFF_TICKS;
}

static void step_inner(iap2_ctx_t *c) {
    switch (c->tstate) {
    case 1: {
        bool (*op)(void) = c->pending_op;
        if (!op) break;
        if (op()) {
            /* Stock clears the op slot only on success; a failed op stays
             * queued and is retried after the backoff (stock 0x5fcc/0x5fd2). */
            c->pending_op = NULL;
            c->tstate = c->tnext;
        } else {
            enter_fail(c);
        }
        break;
    }
    case 2:
        if (c->tretry == 0) c->tstate = c->tsaved;
        break;
    case 3:
        /* Sync sent, awaiting their ACK (stock 0x5ff0). */
        if (c->tretry != 0) {
            if (c->their_ack == c->seq_tx) c->tstate = 0;
            break;
        }
        c->resend_count++;
        if (c->resend_count >= L_SYNC_RESEND_MAX) {
            c->tstate = 0;
            break;
        }
        c->tretry = L_SYNC_RESEND_MS;
        (void)ep2_send(c->pkt, (uint16_t)((c->pkt[2] << 8) | c->pkt[3]));
        break;
    case 4:
        if (c->msg_chunks == 0 || c->msg_cursor < c->msg_chunks) {
            c->tnext = 4;
            c->pending_op = op_pump;
            c->tstate = 1;
        }
        break;
    case 5:
        if (iap2_auth_write(c->auth_addr,IAP2_AUTH_SEL_CHALLENGE, c->auth_buf, c->auth_len)) {
            c->tstate = 6;
        } else {
            enter_fail(c);
        }
        break;
    case 6: {
        const uint8_t cmd = 1;
        if (iap2_auth_write(c->auth_addr,IAP2_AUTH_SEL_CMD, &cmd, 1)) {
            c->tstate = 7;
        } else {
            enter_fail(c);
        }
        break;
    }
    case 7: {
        uint8_t status = 0;
        if (iap2_auth_read(c->auth_addr,IAP2_AUTH_SEL_CMD, &status, 1) && (status & 0x70u) != 0) {
            c->tnext = 0;
            c->pending_op = op_auth_reply;
            c->tstate = 1;
        } else {
            enter_fail(c);
        }
        break;
    }
    case 8:
        c->tnext = 9;
        c->pending_op = op_status;
        c->tstate = 1;
        break;
    case 9:
        /* bit5 latches into bit6; bit4 schedules the EA announce. */
        c->flags = (uint16_t)((c->flags & ~0x40u) |
                              ((c->flags & 0x20u) ? 0x40u : 0u));
        if (c->flags & 0x10u) {
            c->tnext = 10;
            c->pending_op = op_ea_announce;
            c->tstate = 1;
        } else {
            c->tstate = c->pending_op ? 1 : 0;
        }
        break;
    case 10:
        c->flags &= (uint16_t)~0x10u;
        c->tstate = 0;
        break;
    default:
        /* State 0 (stock 0x60da): pump the ring, then flag-driven work. */
        if (c->ring_read != c->ring_write) {
            c->tnext = c->ring[c->ring_read].next;
            c->pending_op = c->ring[c->ring_read].op;
            c->ring_read = (uint8_t)((c->ring_read + 1u) & 7u);
            c->tstate = 1;
            break;
        }
        if ((c->flags & 0x8u) && (c->flags & 0x10u)) {
            c->tnext = 10;
            c->pending_op = op_ea_announce;
            c->tstate = 1;
            break;
        }
        if ((c->flags & 0x8u) && (c->flags & 0x40u) && !(c->flags & 0x20u)) {
            c->tnext = 9;
            c->pending_op = op_status;
            c->tstate = 1;
            break;
        }
        /* Acknowledge any unacknowledged received packet. */
        if (c->ack_rx != ack_sent && !c->pending_op) {
            c->tnext = 0;
            c->pending_op = op_bare_ack;
            c->tstate = 1;
        }
        break;
    }
}

/* --- public API --------------------------------------------------- */

void iap2_init(void) {
    memset(&iap2_ctx, 0, sizeof(iap2_ctx));
    iap2_auth_init();
    iap2_ctx.session_id = L_SESSION_ID;
    iap2_ctx.auth_addr = 0x22;
}

void iap2_set_connected(bool connected) {
    iap2_ctx_t *c = &iap2_ctx;
    c->connected = connected;
    if (connected) {
        /* Stock USB glue starts the session with params (0, 1). */
        c->state = 0;
        c->attach_backoff = 0;
        c->watchdog = 0;
        transport_init(false, true);
        c->state = 1; /* attach phase */
    } else {
        c->state = 0;
    }
}

void iap2_tick_1ms(void) {
    iap2_ctx_t *c = &iap2_ctx;
    if (c->attach_backoff) c->attach_backoff--;
    if (c->watchdog) c->watchdog--;
    if (c->tretry) c->tretry--;
}

void iap2_task(void) {
    iap2_ctx_t *c = &iap2_ctx;
    if (!c->connected) return;

    switch (c->state) {
    case 1:
        if (c->attach_backoff != 0) break;
        if (ep2_send(attach_pkt, sizeof(attach_pkt))) {
            c->attach_backoff = ATTACH_RETRY_OK;
        } else {
            c->attach_backoff = ATTACH_RETRY_FAIL;
        }
        break;
    case 3:
        step_inner(c);
        break;
    default:
        c->state = 1; /* stock 0x6292: any other state restarts the attach */
        break;
    }
}

void iap2_ep2_rx(const uint8_t *buf, uint32_t len) {
    iap2_ctx_t *c = &iap2_ctx;
    if (c->state == 3) {
        link_rx(c, buf, len);
        c->watchdog = WATCHDOG_TICKS;
    } else {
        presession_rx(c, buf, len);
    }
}

bool iap2_session_active(void) {
    const iap2_ctx_t *c = &iap2_ctx;
    return c->connected && c->state == 3;
}

void iap2_activity(void) { iap2_ctx.watchdog = WATCHDOG_TICKS; }
