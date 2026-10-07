#include "gpio.h"

static void ensure_clock(pin_t pin) {
    RCC_APB2PeriphClockCmd((pin & 0x80u) ? RCC_APB2Periph_GPIOB : RCC_APB2Periph_GPIOA, ENABLE);
}

static void configure(pin_t pin, GPIOMode_TypeDef mode) {
    ensure_clock(pin);
    GPIO_InitTypeDef cfg = {
        .GPIO_Pin = ck_gpio_mask(pin),
        .GPIO_Speed = GPIO_Speed_10MHz,
        .GPIO_Mode = mode,
    };
    GPIO_Init(ck_gpio_port(pin), &cfg);
}

void gpio_set_pin_input(pin_t pin) { configure(pin, GPIO_Mode_IN_FLOATING); }
void gpio_set_pin_input_high(pin_t pin) { configure(pin, GPIO_Mode_IPU); }
void gpio_set_pin_input_low(pin_t pin) { configure(pin, GPIO_Mode_IPD); }
void gpio_set_pin_output(pin_t pin) { configure(pin, GPIO_Mode_Out_PP); }
void gpio_set_pin_output_push_pull(pin_t pin) { configure(pin, GPIO_Mode_Out_PP); }
void gpio_set_pin_output_open_drain(pin_t pin) { configure(pin, GPIO_Mode_Out_OD); }
void gpio_write_pin_high(pin_t pin) { GPIO_SetBits(ck_gpio_port(pin), ck_gpio_mask(pin)); }
void gpio_write_pin_low(pin_t pin) { GPIO_ResetBits(ck_gpio_port(pin), ck_gpio_mask(pin)); }
void gpio_write_pin(pin_t pin, bool level) { level ? gpio_write_pin_high(pin) : gpio_write_pin_low(pin); }
bool gpio_read_pin(pin_t pin) { return GPIO_ReadInputDataBit(ck_gpio_port(pin), ck_gpio_mask(pin)) != Bit_RESET; }
void gpio_toggle_pin(pin_t pin) {
    GPIO_TypeDef *port = ck_gpio_port(pin);
    uint16_t mask = ck_gpio_mask(pin);
    gpio_write_pin(pin, (GPIO_ReadOutputData(port) & mask) == 0);
}
