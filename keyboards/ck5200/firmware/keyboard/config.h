#pragma once

#define VENDOR_ID    0x352E
#define PRODUCT_ID   0x2306
#define DEVICE_VER   0x9001
#define MANUFACTURER Clicks_Technology_Limited
#define PRODUCT      CK5200_QMK_POC

#define MATRIX_ROWS 6
#define MATRIX_COLS 6
#define DEBOUNCE 5

/* The first target uses a custom scanner matching the stock electrical scan. */
#define CUSTOM_MATRIX

/* Keep the first image small. Add VIA/NKRO/etc. only after basic bring-up.
 * One-shot must stay enabled: the SYM key is OSL(1), and with
 * NO_ACTION_ONESHOT it compiled to a no-op and locked out layer 1
 * entirely (review 2026-10-11, F3). */
#define NO_MUSIC_MODE

/* QMK dynamic keymap over a RUNTIME-ONLY (RAM) EEPROM backend (review
 * 2026-10-12, requirement 5): all flash-backed persistence is disabled
 * until a valid storage partition has been demonstrated. The CH32X035 has
 * no writable region available to this firmware: the application region
 * 0x2000..0x8A00 is replaced by the installer on every update, staging
 * occupies 0x8A00..0xF500, the serial page 0xF600 and stock config 0xF700
 * are device data, and 0xF800 upward is RESERVED (reference manual ch.
 * 20: code flash ends at 0x0800F7FF). Keymap edits apply live and are lost
 * on reboot; the stock layout is the compiled default. */
#define DYNAMIC_KEYMAP_ENABLE
#define DYNAMIC_KEYMAP_LAYER_COUNT 4
#define TRANSIENT_EEPROM_SIZE 512u
#define TOTAL_EEPROM_BYTE_COUNT (TRANSIENT_EEPROM_SIZE)
