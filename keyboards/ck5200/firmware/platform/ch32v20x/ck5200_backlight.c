/* CK-5200 backlight driver: TIM1 CH1/2/3 PWM on PA8/PA9/PA10.
 * Faithful port of the stock V122 decode (stock 0x306a init, 0x3010
 * brightness, 0x3050/0x4ef8 channel gates, 0x4eba MOE, 0x4ed0 ARPE,
 * 0x4ea2 CEN): ARR 6000, prescaler 0, duty (0x100 - raw) * 6000 >> 8,
 * raw byte inverted (0 = brightest).
 *
 * CK5200_BACKLIGHT_HOST_TEST swaps the raw register accesses for harness
 * hooks and replaces the peripheral init calls with no-ops, so the host
 * test can assert every register value against the decode.
 */
#include "ck5200_backlight.h"

#include <stddef.h>
#include <stdint.h>

/* TIM1 register offsets (CH32V203). */
#define TIM1_CR1_OFF   0x00u
#define TIM1_CCMR1_OFF 0x18u
#define TIM1_CCMR2_OFF 0x1Cu
#define TIM1_CCER_OFF  0x20u
#define TIM1_PSC_OFF   0x28u
#define TIM1_ARR_OFF   0x2Cu
#define TIM1_CCR1_OFF  0x34u
#define TIM1_CCR2_OFF  0x38u
#define TIM1_CCR3_OFF  0x3Cu
#define TIM1_BDTR_OFF  0x44u

#define CK_BL_ARR 6000u         /* stock 0x2300: ARR = 6000 */
#define CK_BL_CCER_MASK 0x111u  /* CC1E | CC2E | CC3E */
#define CK_BL_DEFAULT_RAW 0x20u

static uint8_t backlight_raw = CK_BL_DEFAULT_RAW;

#ifdef CK5200_BACKLIGHT_HOST_TEST

void     bl_io_write(size_t offset, uint32_t value);
uint32_t bl_io_read(size_t offset);
static void     bl_write(size_t off, uint32_t v) { bl_io_write(off, v); }
static uint32_t bl_read(size_t off) { return bl_io_read(off); }
static void bl_peripheral_init(void) { /* asserted by the harness stubs */ }

#else

#define TIM1_BASE_ADDR 0x40012C00u
static void     bl_write(size_t off, uint32_t v) { *(volatile uint32_t *)(TIM1_BASE_ADDR + off) = v; }
static uint32_t bl_read(size_t off) { return *(volatile uint32_t *)(TIM1_BASE_ADDR + off); }

#include "ch32v20x.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"

static void bl_peripheral_init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);
}

#endif

static uint16_t duty_from_raw(uint8_t raw) {
    /* Stock 0x3010: (0x100 - raw) * 6000 >> 8, all three channels. */
    return (uint16_t)(((uint32_t)(0x100u - raw) * CK_BL_ARR) >> 8);
}

void ck5200_backlight_set(uint8_t raw) {
    backlight_raw = raw;
    const uint16_t duty = duty_from_raw(raw);
    bl_write(TIM1_CCR1_OFF, duty);
    bl_write(TIM1_CCR2_OFF, duty);
    bl_write(TIM1_CCR3_OFF, duty);
}

uint8_t ck5200_backlight_get(void) { return backlight_raw; }

void ck5200_backlight_enable(bool on) {
    /* Stock 0x3050 -> 0x4ef8: gate the three channels in CCER. */
    const uint32_t ccer = bl_read(TIM1_CCER_OFF);
    bl_write(TIM1_CCER_OFF, on ? (ccer | CK_BL_CCER_MASK) : (ccer & ~CK_BL_CCER_MASK));
}

void ck5200_backlight_init(void) {
    bl_peripheral_init();

    bl_write(TIM1_PSC_OFF, 0); /* stock: prescaler 0 */
    bl_write(TIM1_ARR_OFF, CK_BL_ARR);
    /* PWM mode 1 (110) with output-compare preload on all channels
     * (stock 0x4cf6/0x4d68/0x4e06). */
    bl_write(TIM1_CCMR1_OFF, (0x6u << 4) | (1u << 3) | (0x6u << 12) | (1u << 11));
    bl_write(TIM1_CCMR2_OFF, (0x6u << 4) | (1u << 3));
    bl_write(TIM1_BDTR_OFF, 0x8000u);               /* MOE, stock 0x4eba */
    bl_write(TIM1_CR1_OFF, (1u << 7) | (1u << 0));  /* ARPE (0x4ed0) + CEN (0x4ea2) */
    ck5200_backlight_set(backlight_raw);
    bl_write(TIM1_CCER_OFF, CK_BL_CCER_MASK);
}
