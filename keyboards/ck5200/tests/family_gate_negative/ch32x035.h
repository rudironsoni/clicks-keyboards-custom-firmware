#pragma once
/* Deliberately WRONG-family stub for the family-gate negative test: a
 * build against these constants MUST fail to compile. Used only by
 * scripts/test.sh with -I pointing here before the real SDK. */
#define USBFS_BASE 0x50000000u
#define HSI_VALUE  48000000u
typedef enum { USBFS_IRQn = 59 } fake_irqn_t;
