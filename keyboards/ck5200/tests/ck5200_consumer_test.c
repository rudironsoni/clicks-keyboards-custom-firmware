/* Host test for the consumer-key state machine (ck5200_consumer.c).
 *
 * The wire report carries one 16-bit consumer usage; this harness pins
 * the press/release semantics and the 10-byte packing against the
 * descriptor contract: out[8] = usage >> 8, out[9] = usage & 0xff, the
 * keyboard report passes through untouched.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ck5200_consumer.h"

static void check_idle(void) {
    assert(ck5200_consumer_usage() == 0);
    assert(!ck5200_consumer_dirty());
}

static void check_pack(uint16_t usage) {
    uint8_t kb[8] = {0x05, 0x00, 'a', 'b', 'c', 0, 0, 0}; /* modifiers + keys */
    uint8_t out[10];
    ck5200_consumer_pack(out, kb);
    assert(memcmp(out, kb, 8) == 0 && "keyboard report changed");
    assert(out[8] == (uint8_t)(usage >> 8));
    assert(out[9] == (uint8_t)(usage & 0xff));
}

int main(void) {
    check_idle();
    check_pack(0);

    /* Press sets the usage and marks the report dirty. */
    ck5200_consumer_key(0x00B8, true); /* Eject: iOS on-screen keyboard */
    assert(ck5200_consumer_usage() == 0x00B8);
    assert(ck5200_consumer_dirty());
    check_pack(0x00B8);

    /* Clearing dirty is explicit; the usage survives it. */
    ck5200_consumer_clear_dirty();
    assert(!ck5200_consumer_dirty());
    assert(ck5200_consumer_usage() == 0x00B8);
    check_pack(0x00B8);

    /* Re-press of the active key (key repeat) changes nothing: no
     * redundant resend, usage unchanged. */
    ck5200_consumer_key(0x00B8, true);
    assert(ck5200_consumer_usage() == 0x00B8);
    assert(!ck5200_consumer_dirty());

    /* Pressing a different consumer key replaces the active one. */
    ck5200_consumer_key(0x01AE, true);
    assert(ck5200_consumer_usage() == 0x01AE);
    assert(ck5200_consumer_dirty());
    ck5200_consumer_clear_dirty();
    check_pack(0x01AE);

    /* Releasing a key that is not active changes nothing. */
    ck5200_consumer_key(0x029D, false);
    assert(ck5200_consumer_usage() == 0x01AE);
    assert(!ck5200_consumer_dirty());

    /* Releasing the active key clears the field and marks dirty. */
    ck5200_consumer_key(0x01AE, false);
    assert(ck5200_consumer_usage() == 0);
    assert(ck5200_consumer_dirty());
    ck5200_consumer_clear_dirty();
    check_pack(0);

    /* Release of an already-clear field is a no-op. */
    ck5200_consumer_key(0x00B8, false);
    assert(ck5200_consumer_usage() == 0);
    assert(!ck5200_consumer_dirty());

    /* All three stock usages plus Eject fit the 16-bit field. */
    const uint16_t usages[] = {0x00B8, 0x01AE, 0x029D, 0x00CF};
    for (size_t i = 0; i < sizeof(usages) / sizeof(usages[0]); ++i) {
        ck5200_consumer_key(usages[i], true);
        assert(ck5200_consumer_usage() == usages[i]);
        check_pack(usages[i]);
        ck5200_consumer_key(usages[i], false);
        assert(ck5200_consumer_usage() == 0);
        ck5200_consumer_clear_dirty();
    }

    puts("ok 7");
    return 0;
}
