/* MFi authentication chip driver, bit-banged I2C on PA13/PA14.
 * Port of stock 0x4ff6 (start), 0x5048 (stop), 0x5090 (ack read),
 * 0x50ec (send byte), 0x51cc (read transaction), 0x515c (write transaction).
 */
#include "iap2_auth.h"
#include "ch32x035.h"

#ifdef IAP2_AUTH_HOST_TEST
/* Host-test hooks: the replay harness provides the register endpoints so
 * a simulated auth chip can observe the bus waveform. */
void     iap2_io_bsr_write(uint32_t v);
uint32_t iap2_io_indr_read(void);
uint32_t iap2_io_cfghr_read(void);
void     iap2_io_cfghr_write(uint32_t v);
#else
#define GPIOA_BASE_ADDR   0x40010800u
#define GPIOA_CFGHR_REG   (*(volatile uint32_t *)(GPIOA_BASE_ADDR + 0x04u))
#define GPIOA_INDR_REG    (*(volatile uint32_t *)(GPIOA_BASE_ADDR + 0x08u))
#define GPIOA_BSR_REG     (*(volatile uint32_t *)(GPIOA_BASE_ADDR + 0x10u))
static inline void     iap2_io_bsr_write(uint32_t v) { GPIOA_BSR_REG = v; }
static inline uint32_t iap2_io_indr_read(void) { return GPIOA_INDR_REG; }
static inline uint32_t iap2_io_cfghr_read(void) { return GPIOA_CFGHR_REG; }
static inline void     iap2_io_cfghr_write(uint32_t v) { GPIOA_CFGHR_REG = v; }
#endif

#define SCL_SET           0x00002000u /* PA13 high */
#define SCL_RESET         0x20000000u /* PA13 low */
#define SDA_SET           0x00004000u /* PA14 high */
#define SDA_RESET         0x40000000u /* PA14 low */

/* CFGHR bits 24..27 configure PA14. The X035 has no open-drain GPIO mode,
 * so the auth driver releases SDA by switching PA14 to a FLOATING input
 * (mode nibble 4 = GPIO_Mode_IN_FLOATING, ch32x035_gpio.h:32), exactly as
 * the stock driver does at 0x5090 -- NOT to an internal pull-up. This is
 * safe only if the board has an external pull-up on SDA; the stock
 * firmware's identical sequence is the evidence that it does, but the
 * electrical fact is hardware-unverified (review 2026-10-12, finding 8).
 * If the board lacked the pull-up, SDA would float and the auth chip
 * could see clock glitches: a session-level failure, not a brick risk.
 * The host waveform harness (tests/iap2_auth_test.c) verifies the
 * drive/release sequence's bit structure, not the pull-up's existence. */
#define CFGHR_SDA_MASK     0xF0FFFFFFu
#define CFGHR_SDA_OUTPUT   0x01000000u
#define CFGHR_SDA_INPUT   0x04000000u
#define CFGHR_SCL_OUTPUT   0x00100000u /* PA13 push-pull output */

#define AUTH_RETRIES        3u
#define AUTH_RETRY_DELAY    10000u

static uint32_t auth_bit_delay; /* half-bit nop-loop iterations */

static void delay_loops(uint32_t n) {
    /* Stock 0x4fec: n iterations of a 3-instruction loop. */
    while (n--) __asm volatile ("nop");
}

static void sda_drive(bool high) {
    uint32_t cfghr = iap2_io_cfghr_read();
    cfghr &= CFGHR_SDA_MASK;
    cfghr |= high ? CFGHR_SDA_INPUT : CFGHR_SDA_OUTPUT;
    iap2_io_cfghr_write(cfghr);
    iap2_io_bsr_write(high ? SDA_SET : SDA_RESET);
}

static void scl_set(bool high) { iap2_io_bsr_write(high ? SCL_SET : SCL_RESET); }

void iap2_auth_init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    iap2_io_cfghr_write((iap2_io_cfghr_read() & CFGHR_SDA_MASK) | CFGHR_SCL_OUTPUT | CFGHR_SDA_OUTPUT);
    iap2_io_bsr_write(SCL_SET | SDA_SET);
    /* Stock drives a ~1.5 us half-bit at 48 MHz: 0x5350 computes
     * 60000000 / (16 * 50000) = 75 into gp-0x7e0, and 0x501a uses
     * 75 >> 2 = 18 iterations of a ~4-cycle loop = 72 cycles. This
     * loop body is ~3 cycles per iteration, so scale the iteration
     * count with the core clock to hold the same wall-clock half-bit:
     * at 144 MHz, 72 iterations give 216-288 cycles (1.5-2.0 us,
     * ~330-250 kHz SCL). Never faster than stock. The old
     * SystemCoreClock/4000000 constant (36 iterations) ran the bus
     * ~2x faster than stock on the least-documented chip in the
     * system, and its "100 kHz" comment was false (review 2026-10-11,
     * F-4). */
    auth_bit_delay = SystemCoreClock / 2000000u;
    if (auth_bit_delay == 0) auth_bit_delay = 1;
}

#ifdef IAP2_AUTH_HOST_TEST
uint32_t iap2_auth_host_bit_delay(void) { return auth_bit_delay; }
#endif

static void i2c_start(void) {
    /* Stock 0x4ff6: SDA high, SCL high, SDA falls while SCL high, SCL low. */
    sda_drive(true);
    scl_set(true);
    delay_loops(auth_bit_delay);
    sda_drive(false);
    delay_loops(auth_bit_delay);
    scl_set(false);
    delay_loops(auth_bit_delay);
}

static void i2c_stop(void) {
    /* Stock 0x5048: SDA low, SCL high, SDA high while SCL high. */
    sda_drive(false);
    scl_set(true);
    delay_loops(auth_bit_delay);
    sda_drive(true);
    delay_loops(auth_bit_delay);
}

static bool i2c_read_ack(void) {
    /* Stock 0x5090: release SDA, SCL high, sample, SCL low.
     * Returns true when the chip pulls SDA low (ACK). */
    sda_drive(true);
    scl_set(true);
    delay_loops(auth_bit_delay);
    const bool ack = ((iap2_io_indr_read() >> 14) & 1u) == 0u;
    scl_set(false);
    delay_loops(auth_bit_delay);
    return ack;
}

static void i2c_send_byte(uint8_t byte) {
    /* Stock 0x50ec: MSB first. */
    for (int8_t i = 7; i >= 0; --i) {
        sda_drive((byte >> i) & 1u);
        delay_loops(auth_bit_delay);
        scl_set(true);
        delay_loops(auth_bit_delay);
        scl_set(false);
        delay_loops(auth_bit_delay);
    }
}

static bool i2c_read_byte(uint8_t *out, bool master_ack) {
    uint8_t byte = 0;
    sda_drive(true);
    for (int8_t i = 7; i >= 0; --i) {
        scl_set(true);
        delay_loops(auth_bit_delay);
        byte = (uint8_t)((byte << 1) | ((iap2_io_indr_read() >> 14) & 1u));
        scl_set(false);
        delay_loops(auth_bit_delay);
    }
    sda_drive(!master_ack); /* low = ACK, high = NACK on the last byte */
    delay_loops(auth_bit_delay);
    scl_set(true);
    delay_loops(auth_bit_delay);
    scl_set(false);
    sda_drive(true);
    delay_loops(auth_bit_delay);
    *out = byte;
    return true;
}

bool iap2_auth_write(uint8_t addr, uint8_t selector, const uint8_t *buf, size_t len) {
    if (!buf || len == 0) return false;

    for (uint32_t attempt = 0; attempt < AUTH_RETRIES; ++attempt) {
        i2c_start();
        i2c_send_byte(addr);
        if (i2c_read_ack()) {
            i2c_send_byte(selector);
            if (i2c_read_ack()) {
                bool ok = true;
                for (size_t i = 0; i < len && ok; ++i) {
                    i2c_send_byte(buf[i]);
                    ok = i2c_read_ack();
                }
                i2c_stop();
                if (ok) return true;
                return false;
            }
            i2c_stop();
            return false;
        }
        delay_loops(AUTH_RETRY_DELAY);
    }
    return false;
}

bool iap2_auth_read(uint8_t addr, uint8_t selector, uint8_t *buf, size_t len) {
    if (!buf || len == 0) return false;

    bool addr_ok = false;
    for (uint32_t attempt = 0; attempt < AUTH_RETRIES && !addr_ok; ++attempt) {
        i2c_start();
        i2c_send_byte(addr);
        if (!i2c_read_ack()) {
            delay_loops(AUTH_RETRY_DELAY);
            continue;
        }
        i2c_send_byte(selector);
        if (!i2c_read_ack()) {
            i2c_stop();
            return false;
        }
        i2c_stop();
        addr_ok = true;
    }
    if (!addr_ok) return false; /* stock 0x51cc: failed address phase bails */

    for (uint32_t attempt = 0; attempt < AUTH_RETRIES; ++attempt) {
        i2c_start();
        i2c_send_byte((uint8_t)(addr | 1u));
        if (!i2c_read_ack()) {
            delay_loops(AUTH_RETRY_DELAY);
            continue;
        }
        for (size_t i = 0; i < len; ++i) {
            (void)i2c_read_byte(&buf[i], i + 1 < len);
        }
        i2c_stop();
        return true;
    }
    return false;
}
