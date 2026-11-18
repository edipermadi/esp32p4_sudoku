#pragma once
/*
 * Puzzle generator: a random complete grid, then clues taken away in
 * symmetric pairs (180-degree rotation) as long as exactly one solution
 * remains. Graded by a solver that works like a person:
 *
 *   GEN_EASY    singles only (naked + hidden), 36 or more clues
 *   GEN_MEDIUM  singles only, fewer clues
 *   GEN_HARD    also needs locked candidates (pointing / claiming) or
 *               naked / hidden pairs
 *   GEN_EXPERT  those aren't enough (needs chains or trial and error)
 *
 * Runs in well under a second for Easy to Hard on the ESP32-P4; Expert can
 * take a few tries. Call from a worker task, not the LVGL task.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { GEN_EASY = 0, GEN_MEDIUM, GEN_HARD, GEN_EXPERT, GEN_LEVELS } gen_level_t;

extern const char *const GEN_LEVEL_NAMES[GEN_LEVELS];   /* "Easy", "Medium", "Hard", "Expert" */

/* Fills givens (81, 0 = empty) with a new puzzle of that level. seed: any
 * random number. Gives up after a number of tries and returns the closest
 * one found (*got_level says what it is). False only if out of tries for
 * anything at all (shouldn't happen). */
bool sudoku_generate(gen_level_t level, uint32_t seed, uint8_t givens[81], gen_level_t *got_level);

/* Level of a puzzle by the techniques it needs; -1 if it doesn't have
 * exactly one solution. */
int sudoku_grade(const uint8_t givens[81]);

/* Number of solutions, counting up to limit. */
int sudoku_count_solutions(const uint8_t cells[81], int limit);

/* ---------------------------------------------------------------- hints */
/* Units: 0..8 rows, 9..17 columns, 18..26 boxes (left to right, top to bottom). */
int sudoku_unit_cell(int unit, int k);          /* the k-th cell (0..8) of a unit */

typedef enum { TECH_NONE = 0, TECH_POINTING, TECH_CLAIMING, TECH_NAKED_PAIR, TECH_HIDDEN_PAIR } tech_kind_t;
typedef struct {
    int kind;                /* tech_kind_t */
    int unit, unit2;         /* pointing: box, line; claiming: line, box; pairs: the unit */
    int digit, digit2;       /* pointing/claiming: digit; pairs: the two digits */
    int cell1, cell2;        /* pairs: the two cells */
} sudoku_tech_t;

typedef enum {
    STEP_NONE = 0,           /* no simple step: cell/digit is just the answer for the easiest cell */
    STEP_WRONG,              /* cell holds a wrong digit */
    STEP_HIDDEN_SINGLE,      /* digit fits only cell within unit */
    STEP_NAKED_SINGLE,       /* cell can only take digit */
} step_kind_t;
typedef struct {
    int kind;                /* step_kind_t */
    int cell, digit;
    int unit;                /* hidden single: the unit */
    sudoku_tech_t tech;      /* before the single, if a technique was needed (kind != TECH_NONE) */
} sudoku_step_t;

/* The next step a person would take on the board vals (81, 0 = empty;
 * givens included), knowing the solution. False if the board is full. */
bool sudoku_next_step(const uint8_t vals[81], const uint8_t solution[81], sudoku_step_t *out);

#ifdef __cplusplus
}
#endif
