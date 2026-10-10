/* Host-test stub for the TinyUSB surface used by iap2.c. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUSB_DESC_STRING 0x03

bool tud_mounted(void);
bool tud_suspended(void);
uint32_t tud_vendor_n_write(uint8_t itf, const void *buf, uint32_t len);
