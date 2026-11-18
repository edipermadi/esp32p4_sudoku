#pragma once
/*
 * Puzzle generator, solver and hints for five variants:
 *
 *   VAR_CLASSIC  9x9, rows, columns and 3x3 boxes hold 1-9 once each
 *   VAR_X        X-Sudoku: also both main diagonals hold 1-9 once each
 *   VAR_MINI     6x6 with 2x3 boxes (2 rows, 3 columns), digits 1-6
 *   VAR_HYPER    Hyper (Windoku): also four 3x3 windows, at rows and
 *                columns 2-4 and 6-8, hold 1-9 once each
 *   VAR_JIGSAW   9x9 with nine irregular regions of 9 connected cells in
 *                place of the boxes; every puzzle has its own regions
 *   VAR_KILLER   Classic rules, plus cages: groups of 2-5 connected cells
 *                whose digits add up to the cage's sum, none repeated;
 *                few or no givens; every puzzle has its own cages
 *   VAR_KNIGHT   Anti-Knight: Classic rules, and two cells a chess knight's
 *                move apart never hold the same digit
 *   VAR_EVENODD  Even-Odd: Classic rules, and some cells are marked even
 *                or odd; every puzzle has its own marks
 *   VAR_CONSEC   Consecutive: Classic rules, and every pair of side-by-side
 *                cells whose digits differ by 1 has a bar between them (no
 *                bar: they don't); every puzzle has its own bars
 *   VAR_THERMO   Thermo: Classic rules, and along each thermometer the
 *                digits rise from its bulb; every puzzle has its own
 *
 * The functions take the board's geometry (sudoku_geom_t): sudoku_geom()
 * for the fixed variants, sudoku_geom_jigsaw() for a Jigsaw region map,
 * sudoku_geom_killer_text() for a Killer puzzle's cages,
 * sudoku_geom_marks() for an Even-Odd or Consecutive puzzle's marks,
 * sudoku_geom_thermo_text() for a Thermo puzzle's thermometers.
 *
 * Every board is kept in 81 cells, row by row (cell = row * 9 + column); a
 * 6x6 board uses the top-left 6x6 of them and leaves the rest 0. Saved games,
 * move logs and the database then work the same for every variant.
 *
 * Units have fixed numbers whatever the variant: 0..8 rows, 9..17 columns,
 * 18..26 boxes (left to right, top to bottom; on Jigsaw the regions), 27 the
 * main diagonal (top left to bottom right), 28 the other diagonal, 29..32
 * the Hyper windows (top left, top right, bottom left, bottom right). A 6x6
 * board has rows 0..5, columns 9..14 and boxes 18..23; each unit has n cells.
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

/* the numbers are stored in sudoku.db (generated.variant): don't reorder */
typedef enum { VAR_CLASSIC = 0, VAR_X, VAR_MINI, VAR_HYPER, VAR_JIGSAW, VAR_KILLER, VAR_KNIGHT, VAR_EVENODD,
               VAR_CONSEC, VAR_THERMO, VAR_COUNT } sudoku_variant_t;

/* "Classic", "X-Sudoku", "Mini 6x6", "Hyper", "Jigsaw", "Killer", "Anti-Knight", "Even-Odd", "Consecutive", "Thermo" */
extern const char *const VARIANT_NAMES[VAR_COUNT];

/* ---------------------------------------------------------------- geometry */
#define UNIT_DIAG_MAIN 27
#define UNIT_DIAG_ANTI 28
#define UNIT_WINDOW    29               /* 29..32 */
#define UNITS_MAX      33
#define CAGES_MAX      81               /* Killer: cages per board (1 cell each at most) */
#define CAGE_NONE      0xFF
#define THERMOS_MAX    16
#define THERMO_NONE    0xFF

typedef struct {
    uint8_t variant;                    /* sudoku_variant_t */
    uint8_t n;                          /* digits and side: 9 or 6 */
    uint8_t box_h, box_w;               /* box size: 3x3, or 2 rows x 3 columns */
    bool diag;                          /* the two diagonals are units too */
    bool windows;                       /* the four Hyper windows are units too */
    uint8_t region[81];                 /* box (Jigsaw: region) of each cell, 0..n-1 */
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
    /* Killer: cages (their cells are peers too: no digit twice in a cage) */
    uint8_t ncages;
    uint8_t cage_of[81];                /* CAGE_NONE = not in a cage */
    uint8_t cage_sum[CAGES_MAX];
    uint8_t cage_n[CAGES_MAX];
    uint8_t cage_cell[CAGES_MAX][9];    /* cells of each cage, the top-left one first */
    /* digits each cell may hold (candidate bits; Even-Odd narrows its marked cells) */
    uint16_t allow[81];
    uint8_t parity[81];                 /* Even-Odd: PARITY_NONE / _EVEN / _ODD */
    /* Consecutive: bars, bit CONSEC_RIGHT = to the cell on the right, CONSEC_DOWN = below */
    bool consec_on;
    uint8_t consec[81];
    /* peers that share no unit (Anti-Knight: the knight's moves) */
    uint8_t nxpeers[81];
    uint8_t xpeers[81][8];
    /* Thermo: thermometers, each a path of cells from its bulb (digits rise along it) */
    uint8_t nthermos;
    uint8_t thermo_n[THERMOS_MAX];
    uint8_t thermo_cell[THERMOS_MAX][9];
    uint8_t thermo_of[81];              /* THERMO_NONE = on none (a cell is on one at most) */
    uint8_t thermo_pos[81];             /* place along it, 0 = the bulb */
} sudoku_geom_t;

enum { PARITY_NONE = 0, PARITY_EVEN, PARITY_ODD };
#define CONSEC_RIGHT 1
#define CONSEC_DOWN  2

/* Builds the tables (once); call early, before tasks use them. */
void sudoku_geom_init(void);
/* The fixed variants; unknown ones, and VAR_JIGSAW (which needs its regions), give classic. */
const sudoku_geom_t *sudoku_geom(int variant);
/* A Jigsaw board from its region map (81 cells, region 0..8 each, 9 cells
 * per region) into out; false if the map isn't one. */
bool sudoku_geom_jigsaw(const uint8_t regions[81], sudoku_geom_t *out);
/* A Killer board from its cages as text: "sum:cell,cell,...;sum:cell,..."
 * (cells 0..80 row by row, each in one cage at most), into out; false if
 * the text isn't one. sudoku_cages_text() writes it (false: buf too small). */
bool sudoku_geom_killer_text(const char *text, sudoku_geom_t *out);
bool sudoku_cages_text(const sudoku_geom_t *g, char *buf, int len);
/* Even-Odd / Consecutive boards from their marks as text, 81 characters row
 * by row: Even-Odd '.', 'e' (even) or 'o' (odd); Consecutive '0'-'3' (bit 1
 * = a bar to the right, bit 2 = a bar below). sudoku_marks_text() writes it
 * (buf: at least 82). */
bool sudoku_geom_marks(int variant, const char *text, sudoku_geom_t *out);
bool sudoku_marks_text(const sudoku_geom_t *g, char *buf, int len);
/* A Thermo board from its thermometers as text: "cell,cell,...;cell,..."
 * (each from its bulb; cells 0..80, side by side or diagonal neighbours, on
 * one thermometer at most); sudoku_thermos_text() writes it. */
bool sudoku_geom_thermo_text(const char *text, sudoku_geom_t *out);
bool sudoku_thermos_text(const sudoku_geom_t *g, char *buf, int len);

static inline bool sudoku_geom_peer(const sudoku_geom_t *g, int a, int b)
{
    return (g->peer_bits[a][b >> 5] >> (b & 31)) & 1u;
}

static inline bool sudoku_geom_on_diag(const sudoku_geom_t *g, int cell)
{
    int r = cell / 9, c = cell % 9;
    return g->diag && (r == c || r + c == g->n - 1);
}

/* in a Hyper window: -1, or the window 0..3 */
static inline int sudoku_geom_window(const sudoku_geom_t *g, int cell)
{
    int r = cell / 9, c = cell % 9;
    if (!g->windows || r % 4 == 0 || c % 4 == 0) return -1;
    return (r > 4) * 2 + (c > 4);
}

/* the cells drawn tinted: X diagonals, Hyper windows */
static inline bool sudoku_geom_tinted(const sudoku_geom_t *g, int cell)
{
    return sudoku_geom_on_diag(g, cell) || sudoku_geom_window(g, cell) >= 0;
}

/* ---------------------------------------------------------------- generating */

/* Fills givens (81, 0 = empty) with a new puzzle of that variant and level.
 * seed: any random number. board (may be NULL) gets the puzzle's geometry:
 * for Jigsaw its regions (board->region), for Killer its cages
 * (sudoku_cages_text()). Gives up after a number of tries and returns the
 * closest one found (*got_level says what it is). False only if out of
 * tries for anything at all (shouldn't happen). Not reentrant: one
 * generator at a time. */
bool sudoku_generate(int variant, gen_level_t level, uint32_t seed, uint8_t givens[81], sudoku_geom_t *board,
                     gen_level_t *got_level);

/* Level of a puzzle by the techniques it needs; -1 if it doesn't have
 * exactly one solution. (Generator task.) */
int sudoku_grade(const sudoku_geom_t *g, const uint8_t givens[81]);

/* Number of solutions, counting up to limit. (Generator task.) */
int sudoku_count_solutions(const sudoku_geom_t *g, const uint8_t cells[81], int limit);

/* Solves `cells` (81, row-major, 0 = empty) into `out`. False if the givens
 * break a rule or there is no solution. Milliseconds on the ESP32-P4. (UI task.) */
bool sudoku_solve(const sudoku_geom_t *g, const uint8_t cells[81], uint8_t out[81]);

/* ---------------------------------------------------------------- hints */
int sudoku_unit_cell(const sudoku_geom_t *g, int unit, int k);   /* the k-th cell (0..n-1) of a unit, or -1 */

typedef enum { TECH_NONE = 0, TECH_POINTING, TECH_CLAIMING, TECH_NAKED_PAIR, TECH_HIDDEN_PAIR,
               TECH_CAGE, TECH_CONSEC, TECH_THERMO } tech_kind_t;
typedef struct {
    int kind;                /* tech_kind_t */
    int unit, unit2;         /* pointing: box, line; claiming: line, box; pairs: the unit; cage: the cage;
                                consecutive: the cell whose candidates its neighbours narrowed;
                                thermo: the thermometer */
    int digit, digit2;       /* pointing/claiming: digit; pairs: the two digits; cage: bit mask of the
                                digits its sum still allows in its empty cells */
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
bool sudoku_next_step(const sudoku_geom_t *g, const uint8_t vals[81], const uint8_t solution[81], sudoku_step_t *out);

#ifdef __cplusplus
}
#endif
