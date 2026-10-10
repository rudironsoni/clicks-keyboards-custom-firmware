/* Host test for the backlight driver (ck5200_backlight.c).
 *
 * Compiles the real driver with the CK5200_BACKLIGHT_HOST_TEST register
 * hooks and asserts every TIM1 register value against the stock V122
 * decode (stock 0x306a init, 0x3010 brightness formula).
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ck5200_backlight.h"

/* TIM1 register offsets, mirroring the driver. */
#define CR1   0x00u
#define CCMR1 0x18u
#define CCMR2 0x1Cu
#define CCER  0x20u
#define PSC   0x28u
#define ARR   0x2Cu
#define CCR1  0x34u
#define CCR2  0x38u
#define CCR3  0x3Cu
#define BDTR  0x44u

static uint32_t regs[0x50 / 4];

void bl_io_write(size_t offset, uint32_t value) {
    assert((offset & 3u) == 0);
    assert(offset < sizeof(regs) * 4);
    regs[offset / 4] = value;
}

uint32_t bl_io_read(size_t offset) {
    assert((offset & 3u) == 0);
    assert(offset < sizeof(regs) * 4);
    return regs[offset / 4];
}

static uint16_t expected_duty(uint8_t raw) {
    return (uint16_t)(((uint32_t)(0x100u - raw) * 6000u) >> 8);
}

int main(void) {
    memset(regs, 0, sizeof(regs));

    ck5200_backlight_init();

    /* Init writes the stock timebase and mode registers exactly. */
    assert(regs[PSC / 4] == 0);
    assert(regs[ARR / 4] == 6000);
    /* PWM mode 1 + preload on OC1/OC2 and OC3. */
    assert(regs[CCMR1 / 4] == ((0x6u << 4) | (1u << 3) | (0x6u << 12) | (1u << 11)));
    assert(regs[CCMR2 / 4] == ((0x6u << 4) | (1u << 3)));
    assert(regs[BDTR / 4] == 0x8000); /* MOE */
    assert(regs[CR1 / 4] == 0x81);    /* ARPE | CEN */
    assert(regs[CCER / 4] == 0x111);  /* CC1E | CC2E | CC3E */

    /* The default raw byte lands in all three CCRs via the stock formula. */
    const uint16_t d0 = expected_duty(0x20);
    assert(regs[CCR1 / 4] == d0 && regs[CCR2 / 4] == d0 && regs[CCR3 / 4] == d0);

    /* Brightness control: raw 0 is brightest (duty 6000), raw 0xFF nearly
     * off, matching the stock inverted byte. */
    ck5200_backlight_set(0x00);
    assert(regs[CCR1 / 4] == 6000 && regs[CCR2 / 4] == 6000 && regs[CCR3 / 4] == 6000);
    assert(ck5200_backlight_get() == 0x00);

    ck5200_backlight_set(0xFF);
    assert(regs[CCR1 / 4] == expected_duty(0xFF));
    assert(expected_duty(0xFF) < 30); /* nearly off */

    /* Channel gates flip only the three CCER enable bits. */
    ck5200_backlight_enable(false);
    assert(regs[CCER / 4] == 0);
    ck5200_backlight_enable(true);
    assert(regs[CCER / 4] == 0x111);

    puts("ok 1");
    return 0;
}
