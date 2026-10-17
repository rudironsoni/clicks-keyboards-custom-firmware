/*
 * CK-5200 symmetric per-key deferred debounce.
 *
 * Based on QMK sym_defer_pk.c (GPL-2.0-or-later); unlike that generic
 * implementation, this single-board port allocates counters statically.
 * A chattering crossing must not reset the debounce timer for an
 * unrelated, stable crossing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "debounce.h"
#include "timer.h"

#ifndef DEBOUNCE
#    define DEBOUNCE 5
#endif
#if DEBOUNCE > UINT8_MAX
#    error "CK-5200 debounce interval must fit an 8-bit countdown"
#endif

_Static_assert(MATRIX_ROWS == 6 && MATRIX_COLS == 6, "CK-5200 debounce assumes 6x6");

#if DEBOUNCE > 0
static uint8_t remaining[MATRIX_ROWS][MATRIX_COLS];
static matrix_row_t observed[MATRIX_ROWS];
static fast_timer_t last_time;

void debounce_init(uint8_t num_rows) {
    (void)num_rows;
    memset(remaining, 0, sizeof(remaining));
    memset(observed, 0, sizeof(observed));
    last_time = timer_read_fast();
}

void debounce_free(void) {}

bool debounce(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed) {
    (void)changed;
    /* Elapsed time advances all keys independently; no allocation and no
     * shared "last raw change" timer. Handle timer wrap via QMK helper. */
    const fast_timer_t now = timer_read_fast();
    fast_timer_t elapsed = TIMER_DIFF_FAST(now, last_time);
    last_time = now;
    if (elapsed > UINT8_MAX) elapsed = UINT8_MAX;

    bool cooked_changed = false;
    if (num_rows > MATRIX_ROWS) return false;
    for (uint8_t row = 0; row < num_rows; ++row) {
        const matrix_row_t transition = raw[row] ^ observed[row];
        observed[row] = raw[row];
        for (uint8_t col = 0; col < MATRIX_COLS; ++col) {
            const matrix_row_t mask = (matrix_row_t)1u << col;
            uint8_t *counter = &remaining[row][col];
            if (transition & mask) {
                *counter = DEBOUNCE;
            } else if (*counter != 0) {
                if (elapsed >= *counter) {
                    *counter = 0;
                    const matrix_row_t next = (cooked[row] & ~mask) | (raw[row] & mask);
                    cooked_changed |= cooked[row] != next;
                    cooked[row] = next;
                } else {
                    *counter -= (uint8_t)elapsed;
                }
            }
        }
    }
    return cooked_changed;
}
#else
#include "none.c"
#endif
