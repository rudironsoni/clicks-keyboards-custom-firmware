#include <string.h>
#include "tusb.h"
#include "config.h"

#define EPNUM_HID_OUT    0x01
#define EPNUM_HID_IN     0x81
#define EPNUM_UPDATE_OUT 0x02
#define EPNUM_UPDATE_IN  0x82

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

enum {
    ITF_NUM_UPDATE = 0,
    ITF_NUM_HID,
    ITF_NUM_TOTAL,
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
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

static uint8_t const configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),

    9, TUSB_DESC_INTERFACE, ITF_NUM_UPDATE, 0, 2,
       TUSB_CLASS_VENDOR_SPECIFIC, 0xF0, 0x00, 0,
    7, TUSB_DESC_ENDPOINT, EPNUM_UPDATE_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,
    7, TUSB_DESC_ENDPOINT, EPNUM_UPDATE_IN,  TUSB_XFER_BULK, U16_TO_U8S_LE(64), 0,

    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_KEYBOARD,
                             sizeof(hid_report_descriptor), EPNUM_HID_OUT,
                             EPNUM_HID_IN, 64, 1),
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
    (const char[]){0x09, 0x04},
    "Clicks Technology Limited",
    "CK-5200 QMK bringup",
    "CK5200QMK",
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
