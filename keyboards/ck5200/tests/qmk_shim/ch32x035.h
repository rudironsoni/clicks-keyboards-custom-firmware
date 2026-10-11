/* Host shim for the QMK core + protocol harnesses: the minimal CH32X035
 * surface the host-compiled production files need. The X035 re-target
 * (2026-10-12) removed the wear-leveling flash backend entirely (persistence
 * is runtime-only via QMK's transient EEPROM), so no flash model remains:
 * what is left is the core clock the wait/timing code reads and the GPIO
 * type the custom gpio.h parses. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* CH32X035: 48 MHz HSI, no PLL (SDK ch32x035.h HSI_VALUE; the family gate
 * asserts the same constant in the real build). */
#define SystemCoreClock 48000000u

/* Minimal GPIO surface so the custom gpio.h parses on the host. */
typedef struct { int _unused; } GPIO_TypeDef;
extern GPIO_TypeDef *GPIOA, *GPIOB;
