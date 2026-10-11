#include "gpio.h"

static void ensure_clock(pin_t pin) {
    RCC_APB2PeriphClockCmd((pin & 0x80u) ? RCC_APB2Periph_GPIOB : RCC_APB2Periph_GPIOA, ENABLE);
}

static void configure(pin_t pin, GPIOMode_TypeDef mode) {
    ensure_clock(pin);
    GPIO_InitTypeDef cfg = {
        .GPIO_Pin = ck_gpio_mask(pin),
        /* The X035 SDK defines a single GPIO_Speed_50MHz enumeration
         * (ch32x035_gpio.h:25); the V203's 10 MHz selection does not
         * exist on this family. Output slew is irrelevant to the matrix
         * and the 10 kHz iAP2 bit-bang. */
        .GPIO_Speed = GPIO_Speed_50MHz,
        .GPIO_Mode = mode,
    };
    GPIO_Init(ck_gpio_port(pin), &cfg);
}

void gpio_set_pin_input(pin_t pin) { configure(pin, GPIO_Mode_IN_FLOATING); }
void gpio_set_pin_input_high(pin_t pin) { configure(pin, GPIO_Mode_IPU); }
void gpio_set_pin_input_low(pin_t pin) { configure(pin, GPIO_Mode_IPD); }
void gpio_set_pin_output(pin_t pin) { configure(pin, GPIO_Mode_Out_PP); }
void gpio_set_pin_output_push_pull(pin_t pin) { configure(pin, GPIO_Mode_Out_PP); }
/* The X035 GPIO block has no open-drain output mode (ch32x035_gpio.h: the
 * mode enum offers AIN, floating, pull-down, pull-up, push-pull, and
 * alternate push-pull only). The iAP2 bit-bang releases SDA by switching
 * to pull-up input, which is the single-master open-drain equivalent the
 * stock driver itself uses (stock 0x5090 uses drive/release, not OD). */
void gpio_set_pin_output_open_drain(pin_t pin) { gpio_set_pin_output(pin); }
void gpio_write_pin_high(pin_t pin) { GPIO_SetBits(ck_gpio_port(pin), ck_gpio_mask(pin)); }
void gpio_write_pin_low(pin_t pin) { GPIO_ResetBits(ck_gpio_port(pin), ck_gpio_mask(pin)); }
void gpio_write_pin(pin_t pin, bool level) { level ? gpio_write_pin_high(pin) : gpio_write_pin_low(pin); }
bool gpio_read_pin(pin_t pin) { return GPIO_ReadInputDataBit(ck_gpio_port(pin), ck_gpio_mask(pin)) != Bit_RESET; }
void gpio_toggle_pin(pin_t pin) {
    GPIO_TypeDef *port = ck_gpio_port(pin);
    uint16_t mask = ck_gpio_mask(pin);
    gpio_write_pin(pin, (GPIO_ReadOutputData(port) & mask) == 0);
}
