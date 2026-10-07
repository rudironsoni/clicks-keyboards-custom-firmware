#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ch32v20x.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"

typedef uint8_t pin_t;
#define NO_PIN 0xFFu
#define CK_PIN(port_b, n) ((pin_t)(((port_b) ? 0x80u : 0u) | ((n) & 0x0Fu)))
#define A0 CK_PIN(0,0)
#define A1 CK_PIN(0,1)
#define A2 CK_PIN(0,2)
#define A3 CK_PIN(0,3)
#define A4 CK_PIN(0,4)
#define A5 CK_PIN(0,5)
#define A6 CK_PIN(0,6)
#define A7 CK_PIN(0,7)
#define A8 CK_PIN(0,8)
#define A9 CK_PIN(0,9)
#define A10 CK_PIN(0,10)
#define A11 CK_PIN(0,11)
#define A12 CK_PIN(0,12)
#define A13 CK_PIN(0,13)
#define A14 CK_PIN(0,14)
#define A15 CK_PIN(0,15)
#define B0 CK_PIN(1,0)
#define B1 CK_PIN(1,1)
#define B2 CK_PIN(1,2)
#define B3 CK_PIN(1,3)
#define B4 CK_PIN(1,4)
#define B5 CK_PIN(1,5)
#define B6 CK_PIN(1,6)
#define B7 CK_PIN(1,7)
#define B8 CK_PIN(1,8)
#define B9 CK_PIN(1,9)
#define B10 CK_PIN(1,10)
#define B11 CK_PIN(1,11)
#define B12 CK_PIN(1,12)
#define B13 CK_PIN(1,13)
#define B14 CK_PIN(1,14)
#define B15 CK_PIN(1,15)

static inline GPIO_TypeDef *ck_gpio_port(pin_t pin) { return (pin & 0x80u) ? GPIOB : GPIOA; }
static inline uint16_t ck_gpio_mask(pin_t pin) { return (uint16_t)(1u << (pin & 0x0Fu)); }

void gpio_set_pin_input(pin_t pin);
void gpio_set_pin_input_high(pin_t pin);
void gpio_set_pin_input_low(pin_t pin);
void gpio_set_pin_output(pin_t pin);
void gpio_set_pin_output_push_pull(pin_t pin);
void gpio_set_pin_output_open_drain(pin_t pin);
void gpio_write_pin_high(pin_t pin);
void gpio_write_pin_low(pin_t pin);
void gpio_write_pin(pin_t pin, bool level);
bool gpio_read_pin(pin_t pin);
void gpio_toggle_pin(pin_t pin);
