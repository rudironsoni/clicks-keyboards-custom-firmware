#include <string.h>
#include "tusb.h"
#include "config.h"

/*
 * USB topology matches the stock CK-5200 V122 image (linked 0x6500..0x6560)
 * so the iPhone sees the same interfaces the stock firmware presents:
 *
 * - interface 2: HID boot keyboard, interrupt EP 0x81/0x01, 64 B, interval 1
 * - interface 0: FF/F0/00, bulk EP 0x82/0x02: the iAP2 link layer
 * - interface 1: FF/F0/01, bulk EP 0x83/0x03 at alternate setting 1 only:
 *   raw companion-protocol channel once the EA session opens
 *
 * Differences from stock, both deliberate:
 * - bcdDevice stays the custom-firmware routing marker (0x9001) so the Mac
 *   recovery path can tell this firmware from stock; the stock value moves
 *   between releases (0x0121, 0x0122), so no host can depend on it.
 * - The HID report descriptor is the standard 6KRO boot layout that the
 *   QMK host layer speaks, instead of stock's 4KRO-plus-consumer layout.
 */

#define EPNUM_HID_OUT 0x01
#define EPNUM_HID_IN  0x81
#define EPNUM_LINK_OUT 0x02
#define EPNUM_LINK_IN  0x82
#define EPNUM_EA_OUT   0x03
#define EPNUM_EA_IN    0x83

enum {
    ITF_NUM_LINK = 0, /* iAP2 link, vendor instance 0 */
    ITF_NUM_EA = 1,   /* companion data, vendor instance 1, alt 1 */
    ITF_NUM_HID = 2,
    ITF_NUM_TOTAL = 3,
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_IAP2,    /* stock string index 4, image 0x65e3 */
    STRID_EA,      /* stock string index 5, image 0x6600 */
};

static tusb_desc_device_t const device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = VENDOR_ID,
    .idProduct = PRODUCT_ID,
    .bcdDevice = DEVICE_VER,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

static uint8_t const hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

/* 96 bytes, byte-exact with stock image 0x6500..0x6560. */
static uint8_t const configuration_descriptor[] = {
    /* Configuration: 3 interfaces, bus powered + remote wakeup, 100 mA. */
    0x09, TUSB_DESC_CONFIGURATION, 0x60, 0x00, ITF_NUM_TOTAL, 0x01, 0x00, 0xA0, 0x32,

    /* Interface 2: HID boot keyboard. */
    0x09, TUSB_DESC_INTERFACE, ITF_NUM_HID, 0x00, 0x02,
    0x03, 0x01, 0x01, 0x00,
    0x09, 0x21 /* TUSB_DESC_HID */, 0x11, 0x01, 0x00, 0x01, 0x22 /* report */,
    (uint8_t)(sizeof(hid_report_descriptor) & 0xff),
    (uint8_t)(sizeof(hid_report_descriptor) >> 8),
    0x07, TUSB_DESC_ENDPOINT, EPNUM_HID_IN, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(64), 0x01,
    0x07, TUSB_DESC_ENDPOINT, EPNUM_HID_OUT, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(64), 0x01,

    /* Interface 0: iAP2 link (FF/F0/00), bulk EP pair, iInterface 4. */
    0x09, TUSB_DESC_INTERFACE, ITF_NUM_LINK, 0x00, 0x02,
    TUSB_CLASS_VENDOR_SPECIFIC, 0xF0, 0x00, STRID_IAP2,
    0x07, TUSB_DESC_ENDPOINT, EPNUM_LINK_IN, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0x00,
    0x07, TUSB_DESC_ENDPOINT, EPNUM_LINK_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0x00,

    /* Interface 1: companion data (FF/F0/01). Alt 0 has no endpoints;
     * alt 1 carries the bulk EP pair, iInterface 5. */
    0x09, TUSB_DESC_INTERFACE, ITF_NUM_EA, 0x00, 0x00,
    TUSB_CLASS_VENDOR_SPECIFIC, 0xF0, 0x01, STRID_EA,
    0x09, TUSB_DESC_INTERFACE, ITF_NUM_EA, 0x01, 0x02,
    TUSB_CLASS_VENDOR_SPECIFIC, 0xF0, 0x01, STRID_EA,
    0x07, TUSB_DESC_ENDPOINT, EPNUM_EA_IN, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0x00,
    0x07, TUSB_DESC_ENDPOINT, EPNUM_EA_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0x00,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&device_descriptor;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return hid_report_descriptor;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return configuration_descriptor;
}

static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},           /* 0: LANGID US English */
    "Clicks Technology Limited",          /* 1: stock image 0x663b */
    "Clicks Creator Keyboard",            /* 2: stock image 0x6671 */
    "190200001",                          /* 3: stock image 0x66a0 serial */
    "iAP2 Interface",                     /* 4: stock image 0x65e3 */
    "com.clickscompanion.protocol",       /* 5: stock image 0x6600 */
};

static uint16_t string_desc[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t count;

    if (index == STRID_LANGID) {
        memcpy(&string_desc[1], string_desc_arr[0], 2);
        count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *str = string_desc_arr[index];
        count = (uint8_t)strlen(str);
        if (count > 31) count = 31;
        for (uint8_t i = 0; i < count; ++i) string_desc[1 + i] = (uint8_t)str[i];
    }

    string_desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return string_desc;
}
