#pragma once

#include <stdint.h>
#include "ch32v20x.h"

static inline void ck5200_wait_us(uint32_t us) {
    /* Bring-up delay. The loop is intentionally conservative. Matrix timing is not critical. */
    uint32_t loops = (SystemCoreClock / 4000000u) * us;
    while (loops--) {
        __asm volatile ("nop");
    }
}

static inline void ck5200_wait_ms(uint32_t ms) {
    while (ms--) ck5200_wait_us(1000u);
}

#define wait_us(us) ck5200_wait_us((uint32_t)(us))
#define wait_ms(ms) ck5200_wait_ms((uint32_t)(ms))
#define waitInputPinDelay() ck5200_wait_us(1u)

#ifndef MATRIX_IO_DELAY
#define MATRIX_IO_DELAY 2
#endif
