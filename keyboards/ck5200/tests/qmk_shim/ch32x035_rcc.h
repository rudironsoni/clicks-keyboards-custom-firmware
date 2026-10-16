#pragma once
#include <stdint.h>
#define RCC_APB2Periph_GPIOA 4u
#define RCC_APB2Periph_GPIOB 8u
#define ENABLE 1
void RCC_APB2PeriphClockCmd(uint32_t peripherals, unsigned enable);
