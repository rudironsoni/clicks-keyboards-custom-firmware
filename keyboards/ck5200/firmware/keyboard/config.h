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

/* Keep the first image small. Add VIA/NKRO/etc. only after basic bring-up. */
#define NO_ACTION_ONESHOT
#define NO_MUSIC_MODE

/* QMK dynamic keymap, persistent through wear-leveled EEPROM over internal
 * flash. Backing store: 0x0800F800, inside the smallest plausible part and
 * clear of stock staging (<= 0x0800F400) and config (0x0800F600/0x0800F700). */
#define DYNAMIC_KEYMAP_ENABLE
#define DYNAMIC_KEYMAP_LAYER_COUNT 4
#define EEPROM_WEAR_LEVELING
#define WEAR_LEVELING_ENABLE
#define WEAR_LEVELING_LOGICAL_SIZE 512u
#define WEAR_LEVELING_BACKING_SIZE 1024u
#define BACKING_STORE_WRITE_SIZE 4u
#define TOTAL_EEPROM_BYTE_COUNT (WEAR_LEVELING_LOGICAL_SIZE)
