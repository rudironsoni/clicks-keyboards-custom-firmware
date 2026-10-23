/* Production descriptors with the pinned TinyUSB types/macros. No USB I/O. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/usb/usb_descriptors.c"

uint32_t ck5200_test_serial_word;

static void serial(uint32_t word, const char *expected) {
    ck5200_test_serial_word = word;
    const uint16_t *s = tud_descriptor_string_cb(3, 0x0409);
    assert(s[0] == 0x0316);
    for (unsigned i = 0; i < 10; ++i) assert(s[i + 1] == (uint8_t)expected[i]);
    assert(ck5200_test_serial_word == word);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *stock = fopen(argv[1], "rb");
    assert(stock && fseek(stock, 0x6500 - 0x2000, SEEK_SET) == 0);
    uint8_t expected[96];
    assert(fread(expected, 1, sizeof(expected), stock) == sizeof(expected));
    fclose(stock);
    /* HID 1.11 instead of stock 1.01, and the custom report length. */
    expected[20] = 0x11;
    expected[25] = sizeof(hid_report_descriptor);
    expected[26] = 0;
    assert(memcmp(expected, tud_descriptor_configuration_cb(0), 96) == 0);
    unsigned offset = 0, interfaces = 0, endpoints = 0;
    while (offset < sizeof(configuration_descriptor)) {
        const uint8_t *d = configuration_descriptor + offset;
        assert(d[0] >= 2 && offset + d[0] <= sizeof(configuration_descriptor));
        if (d[1] == TUSB_DESC_INTERFACE) ++interfaces;
        if (d[1] == TUSB_DESC_ENDPOINT) ++endpoints;
        offset += d[0];
    }
    assert(offset == 96 && interfaces == 4 && endpoints == 6);
    assert(device_descriptor.idVendor == 0x352e && device_descriptor.idProduct == 0x2306);
    assert(device_descriptor.bcdDevice == 0x9001);

    /* Interpret short HID items. Verify actual field semantics, not text. */
    unsigned page = 0, size = 0, count = 0, minimum = 0, maximum = 0;
    unsigned logical_max = 0, inputs = 0, outputs = 0, input_bits = 0, output_bits = 0;
    for (unsigned at = 0; at < sizeof(hid_report_descriptor); ) {
        uint8_t tag = hid_report_descriptor[at++];
        unsigned n = tag & 3u, value = 0;
        if (n == 3) n = 4;
        assert(at + n <= sizeof(hid_report_descriptor));
        for (unsigned i = 0; i < n; ++i) value |= hid_report_descriptor[at++] << (8 * i);
        switch (tag & 0xfcu) {
        case 0x04: page = value; break;
        case 0x74: size = value; break;
        case 0x94: count = value; break;
        case 0x18: minimum = value; break;
        case 0x28: maximum = value; break;
        case 0x24: logical_max = value; break;
        case 0x80:
            if (inputs == 0) assert(page == 7 && size == 1 && count == 8 && minimum == 224 && maximum == 231 && value == 2);
            if (inputs == 1) assert(size == 8 && count == 1 && value == 3);
            if (inputs == 2) assert(page == 7 && size == 8 && count == 6 && minimum == 0 && maximum == 255 && value == 0);
            if (inputs == 3) assert(page == 12 && size == 16 && count == 1 && minimum == 0 && maximum == 0x29d && logical_max == 0x29d && value == 0);
            input_bits += size * count;
            ++inputs;
            break;
        case 0x90:
            if (outputs == 0) assert(page == 8 && minimum == 1 && maximum == 5 && value == 2);
            output_bits += size * count;
            ++outputs;
            break;
        }
    }
    assert(inputs == 4 && input_bits == 80 && outputs == 2 && output_bits == 8);
    serial(42000007u, "0042000007");
    serial(0, "0000000000");
    serial(UINT32_MAX, "4294967295");
    assert(tud_descriptor_string_cb(6, 0x0409) == NULL);
    puts("ok: production USB descriptor walk, HID fields, read-only serial");
}
