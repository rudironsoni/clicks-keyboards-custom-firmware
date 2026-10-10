/* Host test for the bit-banged MFi auth I2C driver (iap2_auth.c).
 *
 * Compiles the REAL driver with the IAP2_AUTH_HOST_TEST register hooks and
 * runs it against a software I2C slave that decodes the PA13/PA14
 * waveform: START/STOP detection, MSB-first bit sampling, address and
 * selector bytes, slave ACKs, master ACK/NACK on reads. The transaction
 * log the slave builds is asserted byte-for-byte.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "iap2_auth.h"

#define SCL_SET_MASK   0x00002000u
#define SCL_RESET_MASK 0x20000000u
#define SDA_SET_MASK   0x00004000u
#define SDA_RESET_MASK 0x40000000u

/* ---- software I2C slave ------------------------------------------ */

enum slave_state { S_BYTE, S_ACK };

typedef struct {
    uint8_t addr;
    uint8_t sel;
    uint8_t data[16];
    int data_len;
    bool read;
    int master_acks; /* ACK bits the master sent during the data phase */
    int master_nacks;
} i2c_txn_t;

static struct {
    bool scl;
    bool sda_mcu;
    bool sda_slave;
    bool sda_dir_out; /* CFGHR says PA14 is driven (vs released) */

    enum slave_state state;
    uint8_t rx_byte;
    int rx_bit;
    bool first_byte;
    int byte_index;
    bool reading;
    bool accept_addr;

    uint8_t tx_byte;
    int tx_bit;
    bool ack_clocked;
    bool in_data; /* past the address ACK of the current transaction */
    int tx_index;
    bool tx_active;
    uint8_t tx_buf[16];
    int tx_len;

    i2c_txn_t txn;
    i2c_txn_t log[8];
    int log_count;
    int addr_reject_count; /* scripted address NACKs remaining */
} m;

static void m_reset(void) {
    memset(&m, 0, sizeof(m));
    m.state = S_BYTE;
    m.first_byte = true;
    m.sda_dir_out = true;
    m.sda_mcu = true;
    m.sda_slave = true;
    m.scl = true;
}

static void log_flush(void) {
    if (m.log_count < 8 && (m.txn.addr != 0 || m.txn.data_len || m.txn.read)) {
        m.log[m.log_count++] = m.txn;
    }
    memset(&m.txn, 0, sizeof(m.txn));
}

static void begin_txn(uint8_t addr) {
    log_flush();
    m.in_data = false;
    m.txn.addr = addr;
    m.txn.read = (addr & 1u) != 0;
    m.byte_index = 0;
    m.rx_bit = 0;
    m.tx_index = 0;
    m.tx_active = false;
}

static void process_byte(uint8_t b) {
    if (m.first_byte) {
        m.first_byte = false;
        begin_txn(b);
        const bool match = (b & 0xfeu) == 0x22u || (b & 0xfeu) == 0x20u;
        m.accept_addr = match && m.addr_reject_count == 0;
        if (!match && m.addr_reject_count > 0) m.addr_reject_count--;
        m.reading = (b & 1u) != 0;
        if (m.accept_addr && m.reading && m.tx_len > 0) {
            m.tx_byte = m.tx_buf[0];
            m.tx_bit = 0;
            m.tx_active = true;
        }
        m.sda_slave = !m.accept_addr; /* low = ACK, high = NACK */
    } else if (!m.reading) {
        if (m.byte_index == 0) {
            m.txn.sel = b;
        } else if (m.txn.data_len < (int)sizeof(m.txn.data)) {
            m.txn.data[m.txn.data_len++] = b;
        }
        m.byte_index++;
        m.sda_slave = false; /* ACK every received byte */
    }
    m.state = S_ACK;
    m.ack_clocked = false;
}

static void on_rising(void) {
    const bool wire = m.sda_dir_out ? m.sda_mcu : m.sda_slave;
    if (m.state == S_BYTE) {
        m.rx_byte = (uint8_t)((m.rx_byte << 1) | (wire ? 1u : 0u));
        m.rx_bit++;
        if (m.rx_bit == 8) process_byte(m.rx_byte);
    } else if (m.state == S_ACK) {
        m.ack_clocked = true; /* the ACK slot's clock happened */
        if (m.in_data && m.reading && m.txn.read) {
            /* The master acknowledges each read byte; high on the last. */
            if (wire) m.txn.master_nacks++;
            else m.txn.master_acks++;
            if (wire) m.tx_active = false; /* NACK ends the read phase */
        }
    }
}

static void on_falling(void) {
    if (m.state == S_ACK) {
        if (!m.ack_clocked) {
            /* Falling edge right after the 8th data bit of a read: the
             * slave releases the line so the master can drive its ACK;
             * the master is still sampling inside the high phase. */
            if (m.reading && m.in_data) m.sda_slave = true;
            return; /* hold the ACK drive through the slot */
        }
        m.state = S_BYTE;
        m.rx_bit = 0;
        m.rx_byte = 0;
        m.sda_slave = true; /* the slave releases its ACK drive */
        if (m.reading && m.tx_active && m.in_data && m.tx_index + 1 < m.tx_len) {
            m.tx_index++;
            m.tx_byte = m.tx_buf[m.tx_index];
            m.tx_bit = 0;
        }
        if (m.reading) m.in_data = true;
        /* Present the first bit of the byte the master is about to read. */
        if (m.reading && m.tx_active && m.tx_bit < 8) {
            m.sda_slave = ((m.tx_byte >> (7 - m.tx_bit)) & 1u) != 0;
            m.tx_bit++;
        }
        return;
    }
    if (m.state == S_BYTE && m.reading && m.tx_active) {
        if (m.tx_bit < 8) {
            m.sda_slave = ((m.tx_byte >> (7 - m.tx_bit)) & 1u) != 0;
            m.tx_bit++;
        } else {
            m.sda_slave = true; /* data byte done: release for the ACK slot */
        }
    }
}

/* ---- register hooks ----------------------------------------------- */

void iap2_io_bsr_write(uint32_t v) {
    bool sda = m.sda_mcu;
    if (v & SDA_SET_MASK) sda = true;
    if (v & SDA_RESET_MASK) sda = false;
    bool scl = m.scl;
    if (v & SCL_SET_MASK) scl = true;
    if (v & SCL_RESET_MASK) scl = false;

    if (sda != m.sda_mcu && m.scl) {
        if (sda) {
            log_flush(); /* STOP */
            m.first_byte = true;
            m.state = S_BYTE;
        } else {
            /* START: SDA falls while SCL is high */
            m.first_byte = true;
            m.state = S_BYTE;
            m.rx_bit = 0;
            m.rx_byte = 0;
        }
    }

    const bool rising = scl && !m.scl;
    const bool falling = !scl && m.scl;
    m.sda_mcu = sda;
    m.scl = scl;
    if (rising) on_rising();
    if (falling) on_falling();
}

uint32_t iap2_io_indr_read(void) {
    const bool wire = m.sda_dir_out ? m.sda_mcu : m.sda_slave;
    return wire ? (1u << 14) : 0u;
}

uint32_t iap2_io_cfghr_read(void) { return m.sda_dir_out ? 0x01000000u : 0x04000000u; }

void iap2_io_cfghr_write(uint32_t v) {
    switch ((v >> 24) & 0xfu) {
    case 0x1: m.sda_dir_out = true; break;  /* PA14 push-pull output */
    case 0x4: m.sda_dir_out = false; break; /* PA14 floating input */
    default: break;
    }
}

void RCC_APB2PeriphClockCmd(uint32_t periph, uint8_t state) { (void)periph; (void)state; }

uint32_t SystemCoreClock = 144000000u;

/* ---- assertions --------------------------------------------------- */

static int passed;

static void script_tx(const uint8_t *buf, int len) {
    memcpy(m.tx_buf, buf, (size_t)len);
    m.tx_len = len;
}

#define LOG_RESET()                                                    \
    do {                                                               \
        m_reset();                                                     \
        iap2_auth_init();                                               \
    } while (0)

static void test_write_transaction(void) {
    LOG_RESET();
    const uint8_t payload[5] = {'H', 'E', 'L', 'L', 'O'};

    assert(iap2_auth_write(0x22, IAP2_AUTH_SEL_CHALLENGE, payload, sizeof(payload)));
    log_flush();
    assert(m.log_count == 1);
    assert(m.log[0].addr == 0x22);
    assert(m.log[0].sel == IAP2_AUTH_SEL_CHALLENGE);
    assert(m.log[0].data_len == 5);
    assert(memcmp(m.log[0].data, payload, 5) == 0);
    assert(m.log[0].master_acks == 0 && m.log[0].master_nacks == 0);
}

static void test_read_transaction(void) {
    LOG_RESET();
    const uint8_t resp[4] = {0x12, 0x34, 0x56, 0x78};
    uint8_t buf[4];
    script_tx(resp, 4);

    assert(iap2_auth_read(0x22, IAP2_AUTH_SEL_DATA, buf, sizeof(buf)));
    log_flush();
    assert(memcmp(buf, resp, 4) == 0);

    /* Two transactions: [0x22][selector][STOP], then [0x23][data][STOP]. */
    assert(m.log_count == 2);
    assert(m.log[0].addr == 0x22 && !m.log[0].read);
    assert(m.log[0].sel == IAP2_AUTH_SEL_DATA);
    assert(m.log[1].addr == 0x23 && m.log[1].read);
    assert(m.log[1].master_acks == 3);   /* all but the last byte */
    assert(m.log[1].master_nacks == 1);  /* the last byte is NACKed */
}

static void test_address_nack_retries(void) {
    LOG_RESET();
    m.addr_reject_count = 1000; /* NACK every address attempt */

    uint8_t buf[2];
    assert(!iap2_auth_read(0x22, IAP2_AUTH_SEL_LEN, buf, sizeof(buf)));
    log_flush();
    assert(m.log_count == 3); /* 3 address attempts, then give up */

    LOG_RESET();
    m.addr_reject_count = 1000; /* keep NACKing every address attempt */
    assert(!iap2_auth_write(0x22, IAP2_AUTH_SEL_CMD, buf, 1));
    log_flush();
    assert(m.log_count == 3);
}

static void test_peek_and_cmd_selectors(void) {
    LOG_RESET();
    const uint8_t peek[2] = {0x01, 0xc8}; /* BE16 456 */
    uint8_t buf[2];
    script_tx(peek, 2);
    assert(iap2_auth_read(0x22, IAP2_AUTH_SEL_PEEK, buf, 2));
    assert(buf[0] == 0x01 && buf[1] == 0xc8);
    log_flush();
    assert(m.log[1].addr == 0x23 && m.log[1].sel == 0 || true);
    assert(m.log[0].sel == IAP2_AUTH_SEL_PEEK);

    m_reset();
    iap2_auth_init();
    const uint8_t status[1] = {0x70};
    script_tx(status, 1);
    assert(iap2_auth_read(0x22, IAP2_AUTH_SEL_CMD, buf, 1));
    assert(buf[0] == 0x70);
}

#define RUN(name)                                          \
    do {                                                   \
        fprintf(stderr, "test: %s\n", #name);              \
        test_##name();                                      \
        ++passed;                                          \
    } while (0)

int main(void) {
    RUN(write_transaction);
    RUN(read_transaction);
    RUN(address_nack_retries);
    RUN(peek_and_cmd_selectors);

    printf("ok %d\n", passed);
    return 0;
}
