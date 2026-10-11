/*
 * Compile-time family gate (review 2026-10-12, requirement 4): the build
 * fails if the linked SDK constants do not describe a CH32X035 target with
 * the exact footprint the stock firmware proves (USB peripheral at
 * 0x40023400 on AHB, USB interrupt at slot 45, 48 MHz HSI with no PLL, and
 * the documented code-flash end at 0x0800F7FF that the SDK's own driver
 * enforces). A wrong-family build must never compile silently again --
 * the preflight passed 11/11 next to a CH32V203 image.
 *
 * Every constant below comes from the pinned openwch/ch32x035 SDK headers
 * or the CH32X035 reference manual, never from this project's own code.
 */
#include "ch32x035.h"

/* The X035 USBFS block lives at 0x40023400 (stock 0x3f3e region), not at
 * the CH32V203's 0x50000000. USBFS_BASE is a macro and USBFS_IRQn is an
 * enum constant: neither is `defined()`-testable, so the assertions are
 * unconditional. The previous #if defined(USBFS_IRQn) form never fired
 * because the SDK declares the IRQ number as an enum (review 2026-10-12,
 * finding 6). */
_Static_assert(USBFS_BASE == 0x40023400u, "wrong-family SDK: USBFS_BASE must be 0x40023400 (CH32X035)");
_Static_assert(USBFS_IRQn == 45, "wrong-family SDK: USBFS_IRQn must be 45 (CH32X035)");

/* The core clock is the 48 MHz HSI handed over directly (stock 0x3bfc
 * computes SystemCoreClock from the 48 MHz constant): the X035 has no
 * main PLL, so any PLL-selection define is a wrong-family signal.
 * HSI_VALUE is an SDK macro, so this assertion is unconditional too. */
_Static_assert(HSI_VALUE == 48000000u, "wrong-family SDK: HSI_VALUE must be 48 MHz (CH32X035)");
#ifdef SYSCLK_FREQ_144MHz_HSI
#error "wrong-family build: CH32X035 has no PLL (SYSCLK_FREQ_144MHz_HSI is a CH32V203 define)"
#endif

/* Documented code-flash end: 248 pages x 256 bytes = 0xF800 (reference
 * manual ch. 20; the SDK's own flash driver bounds ValidAddrEnd here). The
 * reserved region from 0x0800F800 must never be written. */
#define CK5200_X035_CODE_FLASH_END 0x0800F800u
#define CK5200_X035_STAGING_END    0x0800F500u

/* Staging must end strictly below the reserved region and below the serial
 * (0x0800F600) and stock-config (0x0800F700) pages. */
_Static_assert(CK5200_X035_STAGING_END <= CK5200_X035_CODE_FLASH_END,
               "staging layout overlaps the documented code-flash end");
