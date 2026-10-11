#include <stdint.h>
#include "ch32x035.h"
#include "ch32x035_rcc.h"
#include "ch32x035_gpio.h"
#include "timer.h"

volatile uint32_t timer_count = 0;

__attribute__((interrupt)) void SysTick_Handler(void) {
    SysTick->SR = 0;
    ++timer_count;
}

static void systick_init_1khz(void) {
    NVIC_EnableIRQ(SysTicK_IRQn);
    SysTick->CTLR = 0;
    SysTick->SR = 0;
    SysTick->CNT = 0;
    SysTick->CMP = (SystemCoreClock / 1000u) - 1u;
    SysTick->CTLR = 0x0Fu;
}

void timer_init(void) {
    timer_count = 0;
}

void timer_clear(void) {
    timer_count = 0;
}

uint16_t timer_read(void) {
    return (uint16_t)timer_count;
}

uint32_t timer_read32(void) {
    return timer_count;
}

uint16_t timer_elapsed(uint16_t last) {
    return (uint16_t)(timer_read() - last);
}

uint32_t timer_elapsed32(uint32_t last) {
    return timer_read32() - last;
}

void platform_setup(void) {
    __disable_irq();

    /* CH32X035: the core clock is the 48 MHz HSI, handed over directly with
     * no PLL (SystemCoreClock is fixed by system_ch32x035.c; the V203 build
     * spun here on a PLLRDY bit that does not exist on this family --
     * review 2026-10-11, reviewer 2). */
    systick_init_1khz();

    /* USB: the X035 USBFS block lives at 0x40023400 on the AHB bus (stock
     * 0x3f3e region), the pins are PC16 (D+) / PC17 (D-), and the
     * integrated PHY is enabled through AFIO->CTLR, exactly as TinyUSB's
     * ch32x035 BSP and the stock USB init (stock 0x41d0) do. The USBPD
     * block is powered down like stock does at boot (stock 0x3b04). */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBPD, DISABLE);

    GPIO_InitTypeDef gpio = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    gpio.GPIO_Pin = GPIO_Pin_16;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_17;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOC, &gpio);

    AFIO->CTLR = (AFIO->CTLR & ~(AFIO_CTLR_UDP_PUE | AFIO_CTLR_UDM_PUE)) |
                 AFIO_CTLR_USB_PHY_V33 | AFIO_CTLR_UDP_PUE | AFIO_CTLR_USB_IOEN;

    __enable_irq();
}


/*
 * QMK expects these platform hooks even when the keymap does not expose a
 * bootloader key. We do not yet know a safe software jump into a CK-5200
 * recovery interface, so both hooks perform the normal MCU reset for now.
 * Independent recovery remains UNVERIFIED: see docs/recovery-research.md.
 */
void mcu_reset(void) {
    NVIC_SystemReset();
    for (;;) {}
}

void bootloader_jump(void) {
    NVIC_SystemReset();
    for (;;) {}
}
