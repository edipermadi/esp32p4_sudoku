#pragma once
/*
 * Puzzle generator, solver and hints for three variants:
 *
 *   VAR_CLASSIC  9x9, rows, columns and 3x3 boxes hold 1-9 once each
 *   VAR_X        X-Sudoku: also both main diagonals hold 1-9 once each
 *   VAR_MINI     6x6 with 2x3 boxes (2 rows, 3 columns), digits 1-6
 *
 * Every board is kept in 81 cells, row by row (cell = row * 9 + column); a
 * 6x6 board uses the top-left 6x6 of them and leaves the rest 0. Saved games,
 * move logs and the database then work the same for every variant.
 *
 * Units have fixed numbers whatever the variant: 0..8 rows, 9..17 columns,
 * 18..26 boxes (left to right, top to bottom), 27 the main diagonal (top
 * left to bottom right), 28 the other diagonal. A 6x6 board has rows 0..5,
 * columns 9..14 and boxes 18..23; each unit has n cells.
 *
 * Generating: a random complete grid, then clues taken away in symmetric
 * pairs (180-degree rotation) as long as exactly one solution remains.
 * Graded by a solver that works like a person:
 *
 *   GEN_EASY    singles only (naked + hidden), 36 or more clues (6x6: 16)
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

typedef enum { VAR_CLASSIC = 0, VAR_X, VAR_MINI, VAR_COUNT } sudoku_variant_t;

extern const char *const VARIANT_NAMES[VAR_COUNT];      /* "Classic", "X-Sudoku", "Mini 6x6" */

/* ---------------------------------------------------------------- geometry */
#define UNIT_DIAG_MAIN 27
#define UNIT_DIAG_ANTI 28
#define UNITS_MAX      29

typedef struct {
    uint8_t n;                          /* digits and side: 9 or 6 */
    uint8_t box_h, box_w;               /* box size: 3x3, or 2 rows x 3 columns */
    bool diag;                          /* the two diagonals are units too */
    uint16_t all;                       /* candidate bits 1..n */
    uint8_t ncells;
    uint8_t cells[81];                  /* the cells in use */
    bool used[81];
    uint8_t nunits;
    uint8_t unit_ids[UNITS_MAX];        /* the units in use */
    bool active[UNITS_MAX];
    uint8_t unit[UNITS_MAX][9];         /* cells of each unit in use (n of them) */
    uint8_t ncu[81];
    uint8_t cu[81][5];                  /* units of each cell: row, column, box (+ diagonals) */
    uint8_t npeers[81];
    uint8_t peers[81][32];              /* cells sharing a unit */
    uint32_t peer_bits[81][3];          /* the same as bits (cell / 32, cell % 32) */
} sudoku_geom_t;

/* Builds the tables (once); call early, before tasks use them. */
void sudoku_geom_init(void);
const sudoku_geom_t *sudoku_geom(int variant);     /* unknown variants: classic */

static inline bool sudoku_geom_peer(const sudoku_geom_t *g, int a, int b)
{
    return (g->peer_bits[a][b >> 5] >> (b & 31)) & 1u;
}

static inline bool sudoku_geom_on_diag(const sudoku_geom_t *g, int cell)
{
    int r = cell / 9, c = cell % 9;
    return g->diag && (r == c || r + c == g->n - 1);
}

/* ---------------------------------------------------------------- generating */

/* Fills givens (81, 0 = empty) with a new puzzle of that variant and level.
 * seed: any random number. Gives up after a number of tries and returns the
 * closest one found (*got_level says what it is). False only if out of tries
 * for anything at all (shouldn't happen). */
bool sudoku_generate(int variant, gen_level_t level, uint32_t seed, uint8_t givens[81], gen_level_t *got_level);

/* Level of a puzzle by the techniques it needs; -1 if it doesn't have
 * exactly one solution. */
int sudoku_grade(int variant, const uint8_t givens[81]);

/* Number of solutions, counting up to limit. */
int sudoku_count_solutions(int variant, const uint8_t cells[81], int limit);

/* Solves `cells` (81, row-major, 0 = empty) into `out`. False if the givens
 * break a rule or there is no solution. Milliseconds on the ESP32-P4. */
bool sudoku_solve(int variant, const uint8_t cells[81], uint8_t out[81]);

/* ---------------------------------------------------------------- hints */
int sudoku_unit_cell(int variant, int unit, int k);      /* the k-th cell (0..n-1) of a unit */

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
bool sudoku_next_step(int variant, const uint8_t vals[81], const uint8_t solution[81], sudoku_step_t *out);

#ifdef __cplusplus
}
#endif
