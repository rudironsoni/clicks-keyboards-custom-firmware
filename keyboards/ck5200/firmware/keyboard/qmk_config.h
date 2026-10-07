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
