/*
 * CK-5200 electrical matrix recovered from stock 1.2.2 static analysis.
 *
 * PA0..PA5: inputs with pull-down
 * PB0..PB5: push-pull outputs
 * Stock scan selects one PB line HIGH, keeps the other PB lines LOW, then
 * samples PA0..PA5. A set PA bit therefore means a pressed key.
 */
#include <stdbool.h>
#include <string.h>

#include "matrix.h"
#include "wait.h"
#include "ch32v20x.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"

#define MATRIX_MASK ((uint16_t)0x003Fu)

static matrix_row_t previous[MATRIX_ROWS];

void matrix_init_custom(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin = MATRIX_MASK;
    gpio.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = MATRIX_MASK;
    gpio.GPIO_Speed = GPIO_Speed_10MHz;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, MATRIX_MASK);

    memset(previous, 0, sizeof(previous));
}

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    bool changed = false;

    for (uint8_t row = 0; row < MATRIX_ROWS; ++row) {
        GPIO_ResetBits(GPIOB, MATRIX_MASK);
        GPIO_SetBits(GPIOB, (uint16_t)(1u << row));
        wait_us(2);

        const matrix_row_t value = (matrix_row_t)(GPIO_ReadInputData(GPIOA) & MATRIX_MASK);
        current_matrix[row] = value;
        changed |= value != previous[row];
        previous[row] = value;
    }

    GPIO_ResetBits(GPIOB, MATRIX_MASK);
    return changed;
}
