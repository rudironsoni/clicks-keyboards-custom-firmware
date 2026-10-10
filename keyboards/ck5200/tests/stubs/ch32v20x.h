/* Host-test stub for the WCH peripheral surface used by iap2_auth.c. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RCC_APB2Periph_GPIOA 0x00000004u
#define ENABLE 1

void RCC_APB2PeriphClockCmd(uint32_t periph, uint8_t state);
extern uint32_t SystemCoreClock;
