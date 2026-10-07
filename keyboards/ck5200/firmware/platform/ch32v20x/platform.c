#include <stdint.h>
#include "ch32v20x.h"
#include "ch32v20x_rcc.h"
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

    /* Use the internal oscillator so bring-up does not depend on an unverified board crystal. */
    systick_init_1khz();

    /* The stock CK-5200 uses the CH32V20x USBFS block at 0x50000000. */
    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div3);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_OTG_FS, ENABLE);

    __enable_irq();
}


/*
 * QMK expects these platform hooks even when the keymap does not expose a
 * bootloader key. We do not yet know a safe software jump into a CK-5200
 * bootloader, so both hooks perform the normal MCU reset for now.
 */
void mcu_reset(void) {
    NVIC_SystemReset();
    for (;;) {}
}

void bootloader_jump(void) {
    NVIC_SystemReset();
    for (;;) {}
}
