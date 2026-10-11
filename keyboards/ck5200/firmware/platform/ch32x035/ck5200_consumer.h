/* CK-5200 consumer-key state, shared by the keymap and the HID report.
 *
 * The wire report carries a single 16-bit consumer usage (bytes 8..9 of
 * the 10-byte report). Pressing a second consumer key replaces the
 * active usage; releasing a key that is not active changes nothing;
 * releasing the active key clears the field. Every change marks the
 * report dirty so the keyboard task resends it even when no ordinary
 * key event occurred.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void ck5200_consumer_key(uint16_t usage, bool pressed);
uint16_t ck5200_consumer_usage(void);
bool ck5200_consumer_dirty(void);
void ck5200_consumer_clear_dirty(void);

/* Appends the consumer usage to an 8-byte keyboard report, producing
 * the 10-byte wire report: out[8] = usage >> 8, out[9] = usage & 0xff. */
void ck5200_consumer_pack(uint8_t out[10], const uint8_t keyboard_report[8]);
