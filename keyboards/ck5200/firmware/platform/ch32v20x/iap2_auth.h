/* MFi authentication chip driver, bit-banged I2C.
 *
 * Faithful port of the stock V122 driver (stock 0x4ff6..0x5350, see
 * .stock/audit/NOTES.md): GPIOA PA13 = SCL, PA14 = SDA, chip address byte
 * 0x22 (write) / 0x23 (read). PA13/PA14 are the SWD pins; the board
 * repurposes them, so this driver must run before any debug attach.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One-time bus setup: clocks, idle levels (both high). */
void iap2_auth_init(void);

/* Write transaction to the chip at address byte addr (stock 0x22/0x20):
 * [START][addr][selector][data...][STOP]. Returns true on ACKed transfer. */
bool iap2_auth_write(uint8_t addr, uint8_t selector, const uint8_t *buf, size_t len);

/* Read transaction: [START][addr][selector][STOP][START][addr|1][read len
 * bytes][STOP]. Master ACKs every byte except the last. */
bool iap2_auth_read(uint8_t addr, uint8_t selector, uint8_t *buf, size_t len);

/* Selectors used by the stock session stack. */
#define IAP2_AUTH_SEL_CMD       0x10u /* 1-byte command, also status read */
#define IAP2_AUTH_SEL_LEN      0x11u /* read BE16 response length */
#define IAP2_AUTH_SEL_DATA     0x12u /* read response bytes */
#define IAP2_AUTH_SEL_CHALLENGE 0x21u /* write challenge bytes */
#define IAP2_AUTH_SEL_PEEK     0x30u /* read BE16 pending message length */
#define IAP2_AUTH_SEL_CHUNK_BASE 0x31u /* + chunk cursor: read message chunk */
