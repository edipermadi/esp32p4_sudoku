#pragma once
/* Sudoku solver for the Fill button (every embedded puzzle has one solution). */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Solves `cells` (81, row-major, 0 = empty) into `out`. False if the givens
 * break a rule or there is no solution. Milliseconds on the ESP32-P4. */
bool sudoku_solve(const uint8_t cells[81], uint8_t out[81]);

#ifdef __cplusplus
}
#endif
