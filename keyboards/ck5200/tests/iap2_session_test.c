/* Host replay test for the CK-5200 Apple session stack (iap2.c).
 *
 * Executes the REAL firmware state machine against a scripted iPhone and a
 * fake MFi auth chip, asserting every outgoing EP2 packet byte-for-byte
 * against the stock V122 decode (see docs/STOCK_DECODE.md). The fake
 * auth chip replaces iap2_auth.c at link time; the waveform-level driver
 * itself is covered by iap2_auth_test.c.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "iap2.h"
#include "iap2_auth.h"

/* ---- fake USB ---------------------------------------------------- */

#define SENT_MAX 512
#define SENT_BUF 400

static struct {
    uint8_t buf[SENT_BUF];
    uint16_t len;
} sent_log[SENT_MAX];
static int sent_count, sent_cursor;
static int usb_write_attempts;
static bool usb_write_fail;

bool tud_mounted(void) { return true; }
bool tud_suspended(void) { return false; }

uint32_t tud_vendor_n_write(uint8_t itf, const void *buf, uint32_t len) {
    (void)itf;
    usb_write_attempts++;
    if (usb_write_fail) return 0;
    assert(len <= SENT_BUF);
    assert(sent_count < SENT_MAX);
    memcpy(sent_log[sent_count].buf, buf, len);
    sent_log[sent_count].len = (uint16_t)len;
    sent_count++;
    return len;
}

static int sent_total(void) { return sent_count; }
static void sent_reset(void) { sent_count = sent_cursor = 0; }

static const uint8_t *sent_next(uint16_t *len) {
    if (sent_cursor >= sent_count) {
        fprintf(stderr, "FAIL: no captured packet left (cursor %d, count %d)\n",
                sent_cursor, sent_count);
        assert(false);
    }
    *len = sent_log[sent_cursor].len;
    return sent_log[sent_cursor++].buf;
}

/* ---- fake auth chip ---------------------------------------------- */

#define CHIP_PENDING_MAX 512

static struct {
    bool fail_reads;
    unsigned peek_calls, chunk_calls;
    uint8_t pending[CHIP_PENDING_MAX];
    uint16_t pending_len;
    uint16_t resp_len;
    uint8_t resp[64];
    uint8_t status;
    uint8_t challenge[64];
    size_t challenge_len;
    uint8_t last_addr;
} chip;

static void chip_reset(void) { memset(&chip, 0, sizeof(chip)); chip.status = 0x70; }

void iap2_auth_init(void) { /* the host test drives the chip model directly */ }

bool iap2_auth_write(uint8_t addr, uint8_t selector, const uint8_t *buf, size_t len) {
    chip.last_addr = addr;
    if (selector == IAP2_AUTH_SEL_CHALLENGE) {
        assert(len <= sizeof(chip.challenge));
        memcpy(chip.challenge, buf, len);
        chip.challenge_len = len;
        return true;
    }
    return true;
}

bool iap2_auth_read(uint8_t addr, uint8_t selector, uint8_t *buf, size_t len) {
    chip.last_addr = addr;
    const bool peek = selector == IAP2_AUTH_SEL_PEEK;
    if (peek) chip.peek_calls++;
    if (chip.fail_reads) return false;
    if (peek) {
        if (chip.pending_len == 0) return false;
        assert(len == 2);
        buf[0] = (uint8_t)(chip.pending_len >> 8);
        buf[1] = (uint8_t)chip.pending_len;
        return true;
    }
    if (selector >= IAP2_AUTH_SEL_CHUNK_BASE) {
        chip.chunk_calls++;
        const size_t offset = (size_t)(selector - IAP2_AUTH_SEL_CHUNK_BASE) * 128u;
        if (offset >= chip.pending_len) return false;
        const size_t n = (len < chip.pending_len - offset) ? len : (chip.pending_len - offset);
        memcpy(buf, chip.pending + offset, n);
        return n == len;
    }
    if (selector == IAP2_AUTH_SEL_LEN) {
        assert(len == 2);
        buf[0] = (uint8_t)(chip.resp_len >> 8);
        buf[1] = (uint8_t)chip.resp_len;
        return true;
    }
    if (selector == IAP2_AUTH_SEL_DATA) {
        assert(len <= chip.resp_len);
        memcpy(buf, chip.resp, len);
        return true;
    }
    if (selector == IAP2_AUTH_SEL_CMD) {
        assert(len == 1);
        buf[0] = chip.status;
        return true;
    }
    return false;
}

/* ---- helpers ----------------------------------------------------- */

static uint8_t cksum8(const uint8_t *p, size_t n) {
    uint8_t s = 0;
    while (n--) s = (uint8_t)(s + *p++);
    return (uint8_t)(0u - s);
}

static void settle(unsigned ticks) {
    for (unsigned i = 0; i < ticks; ++i) {
        iap2_tick_1ms();
        iap2_task();
    }
}

static void start_session(void) {
    memset(&iap2_ctx, 0, sizeof(iap2_ctx));
    chip_reset();
    sent_reset();
    iap2_init();
    iap2_set_connected(true);
    settle(3);
    sent_reset(); /* drop the attach packet */
    static const uint8_t echo[6] = {0xff, 0x55, 0x02, 0x00, 0xee, 0x10};
    iap2_ep2_rx(echo, sizeof(echo));
    settle(3);
    sent_reset(); /* drop the initial sync request */
}

/* Builds a link packet the way the phone does and feeds it. */
static void phone(uint8_t control, uint8_t seq, uint8_t ack, uint8_t session,
                  const uint8_t *payload, uint16_t payload_len) {
    uint8_t pkt[SENT_BUF];
    const uint16_t total = (uint16_t)(payload_len + 9u);
    pkt[0] = 0xff; pkt[1] = 0x5a;
    pkt[2] = (uint8_t)(total >> 8); pkt[3] = (uint8_t)total;
    pkt[4] = control; pkt[5] = seq; pkt[6] = ack; pkt[7] = session;
    pkt[8] = cksum8(pkt, 8);
    if (payload_len) memcpy(pkt + 9, payload, payload_len);
    iap2_ep2_rx(pkt, total);
}

/* Wraps a control message into an ACK data packet on session 10. */
static void phone_msg(uint8_t type_hi, uint8_t type_lo,
                      const uint8_t *body, uint16_t body_len) {
    uint8_t payload[SENT_BUF];
    const uint16_t frame = (uint16_t)(body_len + 7u);
    payload[0] = 0x40; payload[1] = 0x40;
    payload[2] = (uint8_t)((body_len + 6u) >> 8);
    payload[3] = (uint8_t)(body_len + 6u);
    payload[4] = type_hi; payload[5] = type_lo;
    if (body_len) memcpy(payload + 6, body, body_len);
    payload[frame - 1] = cksum8(payload, frame - 1);
    phone(0x40, 0x38, iap2_ctx.seq_tx, 10, payload, frame);
}

static void expect_bytes(const char *what, const uint8_t *expected, size_t len) {
    uint16_t got_len = 0;
    const uint8_t *got = sent_next(&got_len);
    if (got_len != len || memcmp(got, expected, len) != 0) {
        fprintf(stderr, "FAIL: %s\n  expected (%zu):", what, len);
        for (size_t i = 0; i < len; ++i) fprintf(stderr, " %02x", expected[i]);
        fprintf(stderr, "\n  got      (%u):", got_len);
        for (size_t i = 0; i < got_len; ++i) fprintf(stderr, " %02x", got[i]);
        fprintf(stderr, "\n");
        assert(false && "packet mismatch");
    }
}

static void expect_link(const char *what, uint8_t control, uint8_t seq,
                        uint8_t ack, uint8_t session,
                        const uint8_t *payload, uint16_t payload_len) {
    uint8_t expected[SENT_BUF];
    const uint16_t total = (uint16_t)(payload_len + 9u);
    expected[0] = 0xff; expected[1] = 0x5a;
    expected[2] = (uint8_t)(total >> 8); expected[3] = (uint8_t)total;
    expected[4] = control; expected[5] = seq; expected[6] = ack; expected[7] = session;
    expected[8] = cksum8(expected, 8);
    if (payload_len) memcpy(expected + 9, payload, payload_len);
    expect_bytes(what, expected, total);
}

static uint8_t sync_payload[14] =
    {0x01, 0x05, 0x10, 0x00, 0x07, 0xd0, 0x01, 0xf4, 0x1e, 0x03, 0x0a, 0x00, 0x01, 0x00};
/* sync_payload[13] is the payload checksum, fixed up in main(). */

static int passed;

#define RUN(name)                                                                     \
    do {                                                                              \
        fprintf(stderr, "test: %s\n", #name);                                         \
        test_##name();                                                                 \
        ++passed;                                                                     \
    } while (0)

/* ---- tests -------------------------------------------------------- */

static void test_attach_cadence(void) {
    static const uint8_t attach[6] = {0xff, 0x55, 0x02, 0x00, 0xee, 0x10};
    memset(&iap2_ctx, 0, sizeof(iap2_ctx));
    chip_reset();
    sent_reset();
    usb_write_fail = false;
    iap2_init();
    iap2_set_connected(true);

    settle(1);
    expect_bytes("attach", attach, sizeof(attach));
    settle(474);
    assert(sent_total() == 1);
    settle(1);
    expect_bytes("attach resend at 475 ms", attach, sizeof(attach));

    /* Send failures back off 50 ms instead of 475 ms. */
    memset(&iap2_ctx, 0, sizeof(iap2_ctx));
    sent_reset();
    usb_write_attempts = 0;
    usb_write_fail = true;
    iap2_init();
    iap2_set_connected(true);
    settle(151);
    usb_write_fail = false;
    assert(sent_total() == 0);       /* failed sends capture nothing */
    assert(usb_write_attempts == 4); /* attempts at t=0, 50, 100, 150 */
}

static void test_reject_and_echo(void) {
    static const uint8_t reject[18] =
        {0xff, 0x55, 0x0e, 0x00, 0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xeb, 0x00};
    memset(&iap2_ctx, 0, sizeof(iap2_ctx));
    chip_reset();
    sent_reset();
    iap2_init();
    iap2_set_connected(true);
    settle(3);
    sent_reset();

    const uint8_t garbage[8] = {0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04};
    iap2_ep2_rx(garbage, sizeof(garbage));
    settle(2);
    expect_bytes("pre-session reject", reject, sizeof(reject));

    /* After the reject the outer machine restarts the attach phase; the
     * leftover backoff must expire first. Drain it, then expect exactly
     * one attach inside the next 475 ms window. */
    settle(1000);
    sent_reset();
    settle(476);
    uint16_t len = 0;
    const uint8_t *p = sent_next(&len);
    (void)p;
    assert(len == 6);
    assert(sent_total() == 1);

    /* The attach echo starts the link and the sync goes out. */
    static const uint8_t echo[6] = {0xff, 0x55, 0x02, 0x00, 0xee, 0x10};
    uint8_t sync[23] = {0xff, 0x5a, 0x00, 0x17, 0x80, 0x00, 0x00, 0x00, 0x00,
                        0x01, 0x05, 0x10, 0x00, 0x07, 0xd0, 0x01, 0xf4,
                        0x1e, 0x03, 0x0a, 0x00, 0x01, 0x00};
    sync[8] = cksum8(sync, 8);
    sync[22] = cksum8(sync + 9, 13);

    sent_reset();
    iap2_ep2_rx(echo, sizeof(echo));
    settle(2);
    assert(iap2_ctx.state == 3);
    expect_bytes("link sync request", sync, sizeof(sync));
}

static void test_sync_resend_and_ack(void) {
    start_session();
    assert(iap2_ctx.tstate == 3);

    /* Unanswered sync resends every 1000 ms with the same bytes. */
    settle(1000);
    assert(sent_total() == 1); /* exactly one resend inside the window */
    expect_link("sync resend", 0x80, 0x00, 0x00, 0, sync_payload, 14);
    assert(iap2_ctx.seq_tx == 0x00);
    sent_reset();

    /* Their SYN (own parameters: mismatch) queues our restatement as
     * SYN|ACK, but the sync-wait state hands over only after their ACK. */
    const uint8_t their_lsp[14] =
        {0x01, 0x1e, 0xff, 0xff, 0x0f, 0xa0, 0x01, 0xf4, 0x04, 0x03,
         0x0b, 0x00, 0x01, 0x00};
    phone(0x80, 0x37, 0x00, 0, their_lsp, sizeof(their_lsp));
    settle(3);
    assert(sent_total() == 0); /* the queued op waits in the ring */
    assert(iap2_ctx.tstate == 3);

    phone(0x40, 0x39, 0x00, 0, NULL, 0); /* their ACK of our sync */
    settle(3);
    expect_link("SYN|ACK restating our parameters", 0xc0, 0x01, 0x39, 0,
                sync_payload, 14);
    assert(iap2_ctx.seq_tx == 0x01);
    assert(iap2_ctx.tstate == 3); /* waits for their ACK of the restatement */

    phone(0x40, 0x3a, 0x01, 0, NULL, 0); /* their ACK of our SYN|ACK */
    settle(3);
    assert(iap2_ctx.tstate == 0);

    /* Their latest sequence (0x3a) is still unacknowledged by our
     * packets, so the machine owes a bare ACK. */
    expect_link("bare ACK for their latest packet", 0x40, 0x02, 0x3a, 10, NULL, 0);
}

static void test_sync_resend_budget(void) {
    start_session();
    settle(31u * 1000u);
    /* start_session already consumed the initial send; the machine sends
     * 29 resends (counts 1..29) and gives up on count 30. */
    assert(sent_total() == 29);
    settle(2000);
    assert(sent_total() == 29);
    assert(iap2_ctx.tstate == 0);
}

static void test_identify_reply(void) {
    start_session();
    settle(2);
    sent_reset();

    /* The data packet's ACK field also closes the sync wait. */
    phone_msg(0x1d, 0x00, NULL, 0);
    settle(3);

    uint16_t len = 0;
    const uint8_t *pkt = sent_next(&len);
    assert(len >= 9);
    assert(pkt[0] == 0xff && pkt[1] == 0x5a);
    assert(pkt[4] == 0x40 && pkt[7] == 10); /* data on the control session */
    assert(cksum8(pkt, 8) == pkt[8]);
    const uint16_t total = (uint16_t)((pkt[2] << 8) | pkt[3]);
    assert(total == len);
    assert(cksum8(pkt + 9, len - 10) == pkt[len - 1]);

    const uint8_t *frame = pkt + 9;
    assert(frame[0] == 0x40 && frame[1] == 0x40);
    const uint16_t frame_len = (uint16_t)(len - 9);
    const uint16_t declared = (uint16_t)((frame[2] << 8) | frame[3]);
    assert(declared == frame_len - 1);
    assert(frame[4] == 0x1d && frame[5] == 0x01); /* Identify reply */

    /* Walk the KVP list: ids and lengths must match the stock image,
     * including the serial parameter stock fills at runtime. */
    static const uint8_t ids[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                  0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
                                  0x10, 0x17, 0x22};
    static const uint8_t lens[] = {24, 8, 26, 10, 6, 6, 8, 2, 1, 2, 49, 17,
                                   3, 3, 21, 33, 17};
    uint16_t off = 6;
    for (size_t i = 0; i < sizeof(ids); ++i) {
        assert(off + 4u <= frame_len - 1);
        const uint16_t entry = (uint16_t)((frame[off] << 8) | frame[off + 1]);
        const uint16_t id = (uint16_t)((frame[off + 2] << 8) | frame[off + 3]);
        assert(id == ids[i]);
        assert(entry == (uint16_t)(lens[i] + 4u));
        off = (uint16_t)(off + entry);
    }
    assert(off == frame_len - 1); /* exactly consumed the declared body */

    /* Spot-check the strings at their cumulative offsets (entries:
     * 28, 12, 30, 14, 10, 10, ...). */
    const uint8_t *base = frame + 6;
    assert(memcmp(base + 0 + 4, "Clicks Creator Keyboard", 23) == 0);
    assert(base[0 + 4 + 23] == 0);
    assert(memcmp(base + 28 + 4, "CK-5200", 7) == 0);
    assert(memcmp(base + 40 + 4, "Clicks Technology Limited", 25) == 0);
    assert(memcmp(base + 70 + 4, "2311000001", 10) == 0); /* no trailing NUL */
    assert(memcmp(base + 84 + 4, "1.2.2", 5) == 0);
    assert(memcmp(base + 94 + 4, "1.2.0", 5) == 0);
    /* Param 0x0a (49 bytes) carries the EA protocol string. */
    assert(memcmp(base + 133 + 4 + 9, "com.clickscompanion.protocol", 28) == 0);
    /* Param 0x22 is the hex token from the image. */
    assert(memcmp(base + 283 + 4, "020c3648d17f4624", 16) == 0);
}

static void test_ack_owed(void) {
    start_session();
    settle(2);
    sent_reset();

    /* An unknown control message still gets its ACK on the link layer. */
    phone_msg(0x12, 0x34, NULL, 0);
    settle(3);
    expect_link("bare ACK for unknown message", 0x40, 0x01,
                0x38, 10, NULL, 0);
}

static void test_cert_stream(void) {
    start_session();
    settle(2);
    sent_reset();

    for (unsigned i = 0; i < 200; ++i) chip.pending[i] = (uint8_t)i;
    chip.pending_len = 200;

    phone_msg(0xaa, 0x00, NULL, 0);
    settle(8);

    expect_link("bare ACK for cert request", 0x40, 0x01, 0x38, 10, NULL, 0);

    /* First chunk: message header + KVP header + 128 bytes + checksum. */
    uint8_t first[139];
    first[0] = 0x40; first[1] = 0x40;
    first[2] = 0x00; first[3] = 0xd2; /* body 204 + 6 */
    first[4] = 0xaa; first[5] = 0x01;
    first[6] = 0x00; first[7] = 0xcc; /* entry 200 + 4 */
    first[8] = 0x00; first[9] = 0x00; /* param id 0 */
    for (unsigned i = 0; i < 128; ++i) first[10 + i] = (uint8_t)i;
    first[138] = cksum8(first, 138);
    expect_link("cert first chunk", 0x40, 0x02, 0x38, 10, first, sizeof(first));

    /* Second chunk: the remaining 72 bytes, raw. */
    uint8_t second[73];
    for (unsigned i = 0; i < 72; ++i) second[i] = (uint8_t)(128 + i);
    second[72] = cksum8(second, 72);
    expect_link("cert second chunk", 0x40, 0x03, 0x38, 10, second, sizeof(second));

    settle(5);
    assert(sent_total() == 3); /* bare ACK + both chunks, then idle */
    assert(chip.chunk_calls == 2);
}

static void test_auth_challenge(void) {
    start_session();
    settle(2);
    sent_reset();

    for (unsigned i = 0; i < 16; ++i) chip.resp[i] = (uint8_t)(0xa0 + i);
    chip.resp_len = 16;
    chip.status = 0x70;

    /* Phone challenge: KVP id 0 with a 20-byte value. */
    uint8_t body[24];
    body[0] = 0x00; body[1] = 24; /* entry = 20 + 4 */
    body[2] = 0x00; body[3] = 0x00; /* id 0 */
    for (unsigned i = 0; i < 20; ++i) body[4 + i] = (uint8_t)(0x60 + i);
    phone_msg(0xaa, 0x02, body, sizeof(body));
    settle(10);

    expect_link("bare ACK for challenge", 0x40, 0x01, 0x38, 10, NULL, 0);

    /* Stock copies from KVP header byte 3, so the chip sees the id low
     * byte followed by the first 19 value bytes. */
    assert(chip.challenge_len == 20);
    assert(chip.challenge[0] == 0x00);
    assert(chip.challenge[1] == 0x60);
    assert(chip.challenge[19] == 0x72);
    assert(iap2_ctx.auth_len == 20);

    uint8_t reply[27];
    reply[0] = 0x40; reply[1] = 0x40;
    reply[2] = 0x00; reply[3] = 0x1a; /* body 20 + 6 */
    reply[4] = 0xaa; reply[5] = 0x03;
    reply[6] = 0x00; reply[7] = 0x14; /* entry 16 + 4 */
    reply[8] = 0x00; reply[9] = 0x00;
    for (unsigned i = 0; i < 16; ++i) reply[10 + i] = (uint8_t)(0xa0 + i);
    reply[26] = cksum8(reply, 26);
    expect_link("auth reply 0xAA03", 0x40, 0x02, 0x38, 10, reply, sizeof(reply));
}

static void test_session_open_and_close(void) {
    start_session();
    settle(2);
    sent_reset();

    phone_msg(0xaa, 0x04, NULL, 0); /* auth accepted */
    settle(2);
    assert((iap2_ctx.flags & 0x4u) != 0);

    phone_msg(0x1d, 0x02, NULL, 0); /* session open */
    settle(8);

    /* Their 0xAA04 is owed an ACK, then the open sequence runs. */
    expect_link("bare ACK for 0xAA04", 0x40, 0x01, 0x38, 10, NULL, 0);

    uint8_t power_start[11] = {0x40, 0x40, 0x00, 0x0a, 0xae, 0x00,
                               0x00, 0x04, 0x00, 0x04, 0x00};
    power_start[10] = cksum8(power_start, 10);
    expect_link("StartPowerUpdates 0xAE00", 0x40, 0x02, 0x38, 10,
                power_start, sizeof(power_start));

    uint8_t status[18] = {0x40, 0x40, 0x00, 0x11, 0xaa, 0x03,
                           0x00, 0x06, 0x00, 0x00, 0x08, 0x34,
                           0x00, 0x05, 0x00, 0x01, 0x01, 0x00};
    status[17] = cksum8(status, 17);
    expect_link("status report 0xAA03", 0x40, 0x03, 0x38, 10,
                status, sizeof(status));

    settle(5);
    assert(sent_total() == 3); /* status does not repeat while bit5 is set */

    assert((iap2_ctx.flags & 0x8u) != 0); /* session open */
    assert(iap2_session_active());

    sent_reset();
    phone_msg(0xea, 0x00, NULL, 0); /* EA session open */
    settle(4);
    assert((iap2_ctx.flags & 0x800u) != 0);
    assert(sent_total() == 0); /* the last reply already acked their seq */

    sent_reset();
    phone_msg(0xea, 0x01, NULL, 0); /* session end */
    settle(4);
    assert((iap2_ctx.flags & 0x7ffu) == 0);
    assert((iap2_ctx.flags & 0x800u) != 0); /* the EA bit survives */
    assert(sent_total() == 0);
}

static void test_rst_restarts_link(void) {
    start_session();
    settle(2);
    sent_reset();

    phone(0x10, 0x40, 0x01, 0, NULL, 0);
    settle(3);
    expect_link("fresh sync after RST", 0x80, 0x00, 0x00, 0, sync_payload, 14);
    assert(iap2_ctx.seq_tx == 0x00); /* sequence restarted */
}

static void test_peek_failure_toggles_address(void) {
    start_session();
    settle(2);
    sent_reset();
    chip.fail_reads = true;
    chip.peek_calls = 0;

    phone_msg(0xaa, 0x00, NULL, 0);
    /* Each pump attempt peeks once; after 20 failed attempts the chip
     * address toggles from 0x22 to 0x20 (stock 0x62ea behavior). */
    for (unsigned i = 0; i < 21; ++i) settle(11);

    assert(chip.peek_calls >= 20);
    assert(iap2_ctx.auth_addr == 0x20);

    chip.fail_reads = false;
    for (unsigned i = 0; i < 10; ++i) chip.pending[i] = 0x55;
    chip.pending_len = 10;
    phone_msg(0xaa, 0x00, NULL, 0);
    settle(8);
    assert(chip.last_addr == 0x20); /* the toggled address stays in use */
}

static void test_ring_overflow_drops(void) {
    start_session();
    settle(2);
    sent_reset();

    /* Queue more identify requests than the ring can hold. The 8-slot
     * ring keeps 7 usable entries (one slot distinguishes full from
     * empty), so requests 8 and 9 drop, matching stock's drop-when-full. */
    for (int i = 0; i < 9; ++i) phone_msg(0x1d, 0x00, NULL, 0);
    settle(40);

    int identifies = 0;
    while (sent_cursor < sent_count) {
        uint16_t len = 0;
        const uint8_t *pkt = sent_next(&len);
        if (len > 14 && pkt[9 + 4] == 0x1d && pkt[9 + 5] == 0x01) identifies++;
    }
    assert(identifies == 7);
}

static void test_challenge_overread_clamped(void) {
    start_session();
    settle(2);
    sent_reset();

    /* A 512-byte staging area with canaries proves no read past the packet. */
    uint8_t stage[512];
    memset(stage, 0xa5, sizeof(stage));
    uint8_t pkt[64];
    const uint16_t body_len = 12;  /* KVP header 4 + 8 value bytes */
    const uint16_t frame = (uint16_t)(body_len + 7u);
    pkt[9] = 0x40; pkt[10] = 0x40;
    pkt[11] = (uint8_t)((body_len + 6u) >> 8);
    pkt[12] = (uint8_t)(body_len + 6u);
    pkt[13] = 0xaa; pkt[14] = 0x02; /* 0xAA02 */
    pkt[15] = 0x00; pkt[16] = 0xff;  /* KVP entry 255: value_len 251 */
    pkt[17] = 0x00; pkt[18] = 0x00; /* id 0 */
    for (int i = 0; i < 8; ++i) pkt[19 + i] = (uint8_t)(0x30 + i);
    pkt[9 + frame - 1] = cksum8(pkt + 9, frame - 1);

    const uint16_t total = (uint16_t)(9 + frame);
    pkt[0] = 0xff; pkt[1] = 0x5a;
    pkt[2] = (uint8_t)(total >> 8); pkt[3] = (uint8_t)total;
    pkt[4] = 0x40; pkt[5] = 0x38; pkt[6] = 0x00; pkt[7] = 10;
    pkt[8] = cksum8(pkt, 8);

    memcpy(stage + 256, pkt, total);
    iap2_ep2_rx(stage + 256, total);
    settle(4);

    for (size_t i = 256 + total; i < sizeof(stage); ++i) {
        if (stage[i] != 0xa5) {
            fprintf(stderr, "FAIL: canary clobbered at %zu: %02x\n", i, stage[i]);
            assert(false);
        }
    }
    assert(iap2_ctx.auth_len <= 33);
}

int main(void) {
    /* The sync payload's checksum is deterministic; compute it once. */
    ((uint8_t *)sync_payload)[13] = cksum8(sync_payload, 13);

    RUN(attach_cadence);
    RUN(reject_and_echo);
    RUN(sync_resend_and_ack);
    RUN(sync_resend_budget);
    RUN(identify_reply);
    RUN(ack_owed);
    RUN(cert_stream);
    RUN(auth_challenge);
    RUN(session_open_and_close);
    RUN(rst_restarts_link);
    RUN(peek_failure_toggles_address);
    RUN(ring_overflow_drops);
    RUN(challenge_overread_clamped);

    printf("ok %d\n", passed);
    return 0;
}
