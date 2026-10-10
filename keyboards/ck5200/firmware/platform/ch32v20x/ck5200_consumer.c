/* CK-5200 consumer-key state machine. See ck5200_consumer.h. */
#include "ck5200_consumer.h"

#include <string.h>

static uint16_t consumer_usage;
static bool consumer_dirty_state;

void ck5200_consumer_key(uint16_t usage, bool pressed) {
    if (pressed) {
        if (consumer_usage != usage) {
            consumer_usage = usage;
            consumer_dirty_state = true;
        }
        return;
    }
    if (consumer_usage == usage) {
        consumer_usage = 0;
        consumer_dirty_state = true;
    }
}

uint16_t ck5200_consumer_usage(void) { return consumer_usage; }

bool ck5200_consumer_dirty(void) { return consumer_dirty_state; }

void ck5200_consumer_clear_dirty(void) { consumer_dirty_state = false; }

void ck5200_consumer_pack(uint8_t out[10], const uint8_t keyboard_report[8]) {
    memcpy(out, keyboard_report, 8);
    out[8] = (uint8_t)(consumer_usage >> 8);
    out[9] = (uint8_t)consumer_usage;
}
