#include <stdint.h>
#include "ck5200_watchdog.h"
#ifndef CK5200_WATCHDOG_HOST_TEST
#include "ch32x035.h"
#include "ch32x035_rcc.h"
#include "ch32x035_gpio.h"
#include "timer.h"
#endif

volatile uint32_t timer_count = 0;
static uint32_t watchdog_last_tick;
static uint8_t watchdog_progress;

#ifndef CK5200_WATCHDOG_HOST_TEST
/* Reset body follows pinned WCH startup_ch32x035.S V1.0.1, copyright 2021
 * Nanjing Qinheng Microelectronics Co., Ltd.; for WCH MCUs. Replace its
 * weak handle_reset and arm before data/BSS and SystemInit. _einit is the
 * vector start because linker.ld places .vector immediately after .init. */
__asm__(
    ".pushsection .text.handle_reset,\"ax\",@progbits\n"
    ".global handle_reset\n"
    ".align 1\n"
    "handle_reset:\n"
    "li t0, 0x40003000\n"
    "li t1, 0x5555\n"
    "sw t1, 0(t0)\n"
    "li t1, 3\n"
    "sw t1, 4(t0)\n"
    "li t1, 4000\n"
    "sw t1, 8(t0)\n"
    "li t1, 0xCCCC\n"
    "sw t1, 0(t0)\n"
    "3:\n"
    "lw t2, 12(t0)\n"
    "andi t2, t2, 3\n"
    "bnez t2, 3b\n"
    "li t1, 0xAAAA\n"
    "sw t1, 0(t0)\n"
    ".option push\n"
    ".option norelax\n"
    "la gp, __global_pointer$\n"
    ".option pop\n"
    "1:\n"
    "la sp, _eusrstack\n"
    "2:\n"
    "la a0, _data_lma\n"
    "la a1, _data_vma\n"
    "la a2, _edata\n"
    "bgeu a1, a2, 2f\n"
    "1:\n"
    "lw t0, (a0)\n"
    "sw t0, (a1)\n"
    "addi a0, a0, 4\n"
    "addi a1, a1, 4\n"
    "bltu a1, a2, 1b\n"
    "2:\n"
    "la a0, _sbss\n"
    "la a1, _ebss\n"
    "bgeu a0, a1, 2f\n"
    "1:\n"
    "sw zero, (a0)\n"
    "addi a0, a0, 4\n"
    "bltu a0, a1, 1b\n"
    "2:\n"
    "li t0, 0x1f\n"
    "csrw 0xbc0, t0\n"
    "li t0, 0x3\n"
    "csrw 0x804, t0\n"
    "li t0, 0x88\n"
    "csrw mstatus, t0\n"
    "la t0, _einit\n"
    "ori t0, t0, 3\n"
    "csrw mtvec, t0\n"
    "jal SystemInit\n"
    "la t0, main\n"
    "csrw mepc, t0\n"
    "mret\n"
    ".popsection\n"
);
#endif

void ck5200_watchdog_note_scan(void) { watchdog_progress |= 1u; }
void ck5200_watchdog_note_transport(void) { watchdog_progress |= 2u; }
void ck5200_watchdog_begin(void) { watchdog_progress = 0; }

void ck5200_watchdog_finish(bool rescue) {
    const uint8_t progress = watchdog_progress;
    watchdog_progress = 0;
    const uint32_t tick = timer_count;
    if ((progress & (rescue ? 2u : 3u)) == (rescue ? 2u : 3u) &&
        tick != watchdog_last_tick) {
        IWDG->CTLR = 0xAAAAu;
        watchdog_last_tick = tick;
    }
}

#ifndef CK5200_WATCHDOG_HOST_TEST

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
     * 0x3f3e region), the pins are PC16 (D-) / PC17 (D+/BOOT), and the
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
#endif
