/* CK-5200 backlight driver, faithful port of the stock V122 decode.
 *
 * Stock drives three TIM1 PWM channels (CH1/2/3) on PA8/PA9/PA10:
 * init at stock 0x306a, brightness at stock 0x3010, channel gates at
 * stock 0x3050/0x4ef8, MOE at 0x4eba, ARPE+CEN at 0x4ed0/0x4ea2.
 * ARR is 6000 with prescaler 0; the duty for a raw config byte is
 * (0x100 - raw) * 6000 >> 8, written to all three CCRs, so the raw byte
 * is inverted (0 = brightest).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void ck5200_backlight_init(void);
void ck5200_backlight_set(uint8_t raw); /* raw config byte, stock formula */
uint8_t ck5200_backlight_get(void);
void ck5200_backlight_enable(bool on);
