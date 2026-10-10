/* CK-5200 backlight driver: TIM1 CH1/2/3 PWM on PA8/PA9/PA10. */
#include "ck5200_backlight.h"

#include "ch32v20x.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"

#define TIM1_BASE_ADDR 0x40012C00u
#define TIM1_CR1   (*(volatile uint32_t *)(TIM1_BASE_ADDR + 0x00u))
#define TIM1_CCMR1 (*(volatile uint32_t *)(TIM1_BASE_ADDR + 0x18u))
#define TIM1_CCMR2 (*(volatile uint32_t *)(TIM1_BASE_ADDR + 0x1Cu))
#define TIM1_CCER  (*(volatile uint32_t *)(TIM1_BASE_ADDR + 0x20u))
#define TIM1_PSC   (*(volatile uint32_t *)(TIM1_BASE_ADDR + 0x28u))
#define TIM1_ARR   (*(volatile uint32_t *)(TIM1_BASE_ADDR + 0x2Cu))
#define TIM1_CCR1  (*(volatile uint32_t *)(TIM1_BASE_ADDR + 0x34u))
#define TIM1_CCR2  (*(volatile uint32_t *)(TIM1_BASE_ADDR + 0x38u))
#define TIM1_CCR3  (*(volatile uint32_t *)(TIM1_BASE_ADDR + 0x3Cu))
#define TIM1_BDTR  (*(volatile uint32_t *)(TIM1_BASE_ADDR + 0x44u))

#define CK_BL_ARR 6000u        /* stock 0x2300: ARR = 6000 */
#define CK_BL_CCER_MASK 0x111u /* CC1E | CC2E | CC3E */
#define CK_BL_DEFAULT_RAW 0x20u

static uint8_t backlight_raw = CK_BL_DEFAULT_RAW;

static uint16_t duty_from_raw(uint8_t raw) {
    /* Stock 0x3010: (0x100 - raw) * 6000 >> 8, all three channels. */
    return (uint16_t)(((uint32_t)(0x100u - raw) * CK_BL_ARR) >> 8);
}

void ck5200_backlight_set(uint8_t raw) {
    backlight_raw = raw;
    const uint16_t duty = duty_from_raw(raw);
    TIM1_CCR1 = duty;
    TIM1_CCR2 = duty;
    TIM1_CCR3 = duty;
}

uint8_t ck5200_backlight_get(void) { return backlight_raw; }

void ck5200_backlight_enable(bool on) {
    /* Stock 0x3050 -> 0x4ef8: gate the three channels in CCER. */
    if (on) {
        TIM1_CCER |= CK_BL_CCER_MASK;
    } else {
        TIM1_CCER &= ~CK_BL_CCER_MASK;
    }
}

void ck5200_backlight_init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    TIM1_PSC = 0;           /* stock: prescaler 0 */
    TIM1_ARR = CK_BL_ARR;
    /* PWM mode 1 (110) with output-compare preload on all channels
     * (stock 0x4cf6/0x4d68/0x4e06). */
    TIM1_CCMR1 = (0x6u << 4) | (1u << 3) | (0x6u << 12) | (1u << 11);
    TIM1_CCMR2 = (0x6u << 4) | (1u << 3);
    TIM1_BDTR = 0x8000u;    /* MOE, stock 0x4eba */
    TIM1_CR1 = (1u << 7) | (1u << 0); /* ARPE (stock 0x4ed0) + CEN (0x4ea2) */
    ck5200_backlight_set(backlight_raw);
    TIM1_CCER = CK_BL_CCER_MASK;
}
