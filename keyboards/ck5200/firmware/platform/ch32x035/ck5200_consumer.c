/* CK-5200 consumer-key state machine. See ck5200_consumer.h. */
#include "ck5200_consumer.h"
#include "action_util.h"
#include "host.h"

#include <string.h>

static uint16_t consumer_usage;

void ck5200_consumer_key(uint16_t usage, bool pressed) {
    if (pressed) {
        if (consumer_usage != usage) {
            consumer_usage = usage;
            host_keyboard_send(keyboard_report);
        }
        return;
    }
    if (consumer_usage == usage) {
        consumer_usage = 0;
        host_keyboard_send(keyboard_report);
    }
}

uint16_t ck5200_consumer_usage(void) { return consumer_usage; }

void ck5200_consumer_pack(uint8_t out[10], const uint8_t keyboard_report[8]) {
    memcpy(out, keyboard_report, 8);
    /* HID report fields are little-endian: byte 8 is the low half of the
     * 16-bit consumer usage, byte 9 the high half, per the descriptor.
     * The old big-endian packing made hosts decode 0xB800 instead of
     * 0x00B8, so every consumer key was dead (review 2026-10-11, F2). */
    out[8] = (uint8_t)consumer_usage;
    out[9] = (uint8_t)(consumer_usage >> 8);
}
