/**
 * @file gen_internal.h
 * @ingroup generator
 * @brief What the puzzle generator's files share (private to them; the rest uses generator.h).
 *
 * | File            | What                                                                       |
 * |-----------------|----------------------------------------------------------------------------|
 * | gen_geometry.c  | the names, each variant's board, the variants' texts read and written      |
 * | gen_solver.c    | counting and filling: every variant's rules (grid_free()), the search       |
 * | gen_grade.c     | human-style grading: the techniques and the level they give                 |
 * | gen_make.c      | generating: digging to a level; Jigsaw regions, Killer cages, lines, ...    |
 * | gen_hints.c     | the next step a person would take                                           |
 *
 * The solver's checks and their tables stay in one file (gen_solver.c), so its inner loop keeps
 * them inlined; the small helpers every part uses are static inline here.
 */
#pragma once
#include "generator.h"

/** @addtogroup generator
 *  @{ */

/** @name Small helpers (static inline)
 *  @{ */
/**
 * @brief the cells of row / column lu (0..17) as seen from its clue (the left, the top)
 * @param lu  the row (0..8) or column (9..17)
 * @param cells  gets its 9 cells
 */
static inline void line_cells(int lu, int cells[9])
{
    for (int k = 0; k < 9; k++) {
        cells[k] = lu < 9 ? lu * 9 + k : k * 9 + (lu - 9);
    }
}

/**
 * @brief buildings seen along cells (the digits in vals, in order): a taller one hides those behind it
 * @param vals  the digits
 * @param cells  the 9 cells, from the clue
 * @return the buildings seen
 */
static inline int sky_seen(const uint8_t *vals, const int cells[9])
{
    int seen = 0, top = 0;
    for (int k = 0; k < 9; k++) {
        if (vals[cells[k]] > top) {
            top = vals[cells[k]];
            seen++;
        }
    }
    return seen;
}

/**
 * @brief the units of a cell (3, 4 or, at the centre of an X board, 5)
 * @param G  the board
 * @param cell  the cell
 * @param out  gets its units
 * @return how many
 */
static inline int cell_units(const sudoku_geom_t *G, int cell, int out[5])
{
    int n = G->ncu[cell];
    for (int i = 0; i < n; i++) {
        out[i] = G->cu[cell][i];
    }
    return n;
}

/**
 * @brief The cell is in the unit.
 * @param G  the board
 * @param cell  the cell
 * @param u  the unit
 * @return true if so
 */
static inline bool is_in_unit(const sudoku_geom_t *G, int cell, int u)
{
    int us[5], n = cell_units(G, cell, us);
    for (int i = 0; i < n; i++) {
        if (us[i] == u) {
            return true;
        }
    }
    return false;
}

/**
 * @brief A random number (xorshift32).
 * @param s  the state (0: a fixed seed)
 * @return the next number
 */
static inline uint32_t rnd(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
/** @} */

/** @brief The techniques' state: a board's digits and each empty cell's candidates. */
typedef struct {
    const sudoku_geom_t *G;             /**< the board */
    uint8_t v[81];                      /**< the digits, 0 = empty */
    uint16_t cand[81];                  /**< each empty cell's candidates (bit d = d) */
    int empty;                          /**< the empty cells */
    bool broken;                        /**< a cell has no candidate left (no solution this way) */
} logic_t;

/** @name gen_solver.c
 *  @{ */
/**
 * @brief Fills the Kropki / XV / Greater-Than tables (once, at sudoku_geom_init()).
 */
void gen_edge_tables(void);
/**
 * @brief Fills the Sandwich sum table (once; before any task uses it).
 */
void gen_bread_tables(void);
/**
 * @brief The digits a cell may hold beside its neighbours' digits (Kropki, XV, Greater-Than).
 * @param G          the board
 * @param i          the cell
 * @param v          the digits (0: empty)
 * @param cand       the candidates of the empty ones, or NULL (then empty neighbours allow anything)
 * @return a candidate mask (bit d = digit d)
 */
uint16_t gen_edge_allow(const sudoku_geom_t *G, int i, const uint8_t *v, const uint16_t *cand);
/**
 * @brief The digits the cell at a place of a line may hold, by the line's rule (Thermo, Arrow, Renban, Whispers,
 *        Palindrome, Region Sum Lines).
 * @param G          the board
 * @param t          the line
 * @param p          the place on it
 * @param v          the digits (0: empty)
 * @param cand       the candidates of the empty ones, or NULL
 * @return a candidate mask
 */
uint16_t gen_line_allow(const sudoku_geom_t *G, int t, int p, const uint8_t *v, const uint16_t *cand);
/**
 * @brief Whether a row or column's digits so far can still see as many buildings as its Skyscraper clue says.
 * @param x          the digits along it, from the clue (0: empty)
 * @param c          the clue
 * @return true: still possible
 */
bool is_gen_sky_line_ok(const uint8_t x[9], int c);
/**
 * @brief The digits the cell at a place of an X-Sums row or column may hold, by its clue.
 * @param G          the board
 * @param lu         the row (0..8) or column (9..17), with a clue
 * @param at         the cell's place along it, from the clue
 * @param v          the digits (0: empty)
 * @param cand       the candidates of the empty ones, or NULL (then any digit not on the line yet)
 * @return a candidate mask
 */
uint16_t gen_xsum_allow(const sudoku_geom_t *G, int lu, int at, const uint8_t *v, const uint16_t *cand);
/**
 * @brief Counts a puzzle's solutions, up to a limit, within a node budget.
 * @param G          the board
 * @param cells      the givens (0: empty)
 * @param limit      stop at this many
 * @param node_limit stop after this many search nodes (0: no limit)
 * @return the solutions found (up to limit); -1 if the budget ran out
 */
int gen_count_bounded(const sudoku_geom_t *G, const uint8_t cells[81], int limit, uint32_t node_limit);
/**
 * @brief A random full grid for a board.
 * @param G          the board
 * @param out        gets it
 * @param rng        the random state
 * @param node_limit the search's budget (0: none)
 * @return false if none was found within the budget
 */
bool gen_random_full_grid(const sudoku_geom_t *G, uint8_t out[81], uint32_t *rng, uint32_t node_limit);
/**
 * @brief The Sandwich sums k distinct digits from 2..8 can make.
 * @param k  how many digits, 0..7
 * @return bit s set if s can be made
 */
uint64_t gen_bread_sums(int k);
/** @} */

/** @name gen_grade.c
 *  @{ */
/**
 * @brief Fills the grading's tables (once, at sudoku_geom_init()).
 */
void gen_grade_init(void);
/**
 * @brief Where the techniques write the first step they find (hints use it).
 * @param t  the step's description, or NULL: nowhere
 */
void gen_set_trace(sudoku_tech_t *t);
/**
 * @brief The hardest technique a puzzle needs, solved the way a person would.
 * @param G          the board
 * @param givens     the givens
 * @return its tier (a gen_level_t), or -1 if the techniques don't solve it
 */
int gen_technique_tier(const sudoku_geom_t *G, const uint8_t givens[81]);
/**
 * @brief The fewest givens an Easy puzzle of a board keeps.
 * @param G          the board
 * @return the count
 */
int gen_easy_min_clues(const sudoku_geom_t *G);
/**
 * @brief Sets up the techniques' state for a board: the digits, and each empty cell's candidates.
 * @param L          gets it
 * @param G          the board
 * @param vals       the digits (0: empty)
 */
void gen_logic_load(logic_t *L, const sudoku_geom_t *G, const uint8_t *vals);
/**
 * @brief Places naked and hidden singles.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_singles(logic_t *L);
/**
 * @brief A technique: Killer: what each cage's sum leaves, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_cage_logic(logic_t *L);
/**
 * @brief A technique: Consecutive: the bars, and their absence, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_consec_logic(logic_t *L);
/**
 * @brief A technique: Kropki, XV, Greater-Than: what the marks leave, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_edge_logic(logic_t *L);
/**
 * @brief A technique: Quadruple: a clue's digits that must go in its cells, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_quad_logic(logic_t *L);
/**
 * @brief A technique: Skyscraper: what the clues leave, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_sky_logic(logic_t *L);
/**
 * @brief A technique: X-Sums: what the clues leave, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_xsum_logic(logic_t *L);
/**
 * @brief A technique: Little Killer: what the diagonals' sums leave, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_lk_logic(logic_t *L);
/**
 * @brief A technique: the lines (Thermo, Arrow, Renban, Whispers): what their rules leave, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_thermo_logic(logic_t *L);
/**
 * @brief A technique: Sandwich: what the sums leave, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_sandwich_logic(logic_t *L);
/**
 * @brief A technique: locked candidates (pointing, claiming), candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_locked_candidates(logic_t *L);
/**
 * @brief A technique: naked and hidden pairs, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_pairs(logic_t *L);
/**
 * @brief A technique: naked triples, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_naked_triple(logic_t *L);
/**
 * @brief A technique: XY-Wing, candidates taken out.
 * @param L          the state
 * @return true if it changed anything
 */
bool gen_xy_wing(logic_t *L);
/**
 * @brief A technique: X-Wing (size 2) or Swordfish (size 3), candidates taken out.
 * @param L          the state
 * @param size       2 or 3
 * @return true if it changed anything
 */
bool gen_fish(logic_t *L, int size);
/** @} */

/** @name gen_geometry.c
 *  @{ */
/**
 * @brief Builds a board from its decorations: Killer, a classic board plus cages. Cage-mates become
 *        peers.
 * @param cage_of    the cage of each cell (CAGE_NONE: none)
 * @param sums       each cage's sum
 * @param ncages     how many cages
 * @param out        gets the board
 * @return false if a cage is empty, has more than 9 cells, or its sum can't be made of distinct digits
 */
bool gen_geom_killer(const uint8_t cage_of[81], const uint8_t *sums, int ncages, sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: a classic board with lines of the variant's kind (Thermo,
 *        Arrow, Renban, Whispers).
 * @param variant    the variant
 * @param cells      each line's cells, in order
 * @param lens       each line's length
 * @param n          how many lines
 * @param out        gets the board
 * @return false if there are none or too many
 */
bool gen_geom_lines(int variant, const uint8_t cells[][9], const uint8_t *lens, int n, sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Quadruple, a classic board with these circles.
 * @param at         each circle's top-left cell
 * @param n          each one's digits, how many
 * @param dig        each one's digits
 * @param nq         how many circles
 * @param out        gets the board
 * @return false if a corner is used twice, or there are too many
 */
bool gen_geom_quad(const uint8_t *at, const uint8_t *n, const uint8_t (*dig)[4], int nq, sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Sandwich, a classic board with sums beside the rows and
 *        columns.
 * @param sums       the rows' sums, then the columns' (0: none)
 * @param out        gets the board
 * @return false if a sum can't be made
 */
bool gen_geom_sandwich(const uint8_t sums[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Little Killer, a board with these diagonals.
 * @param n          how many
 * @param top        each one starts above the board (else beside it)
 * @param idx        its column (from the top) or row
 * @param dir        its direction
 * @param sum        its sum
 * @param out        gets the board
 * @return false if there are none or too many
 */
bool gen_geom_lk(int n, const bool *top, const uint8_t *idx, const uint8_t *dir, const uint8_t *sum, sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: X-Sums, a classic board with clues beside the rows and
 *        columns.
 * @param clues      the rows' clues, then the columns' (0: none)
 * @param out        gets the board
 * @return false if a clue can't be met
 */
bool gen_geom_xsum(const uint8_t clues[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Skyscraper, a classic board with clues beside the rows and
 *        columns.
 * @param clues      the rows' clues, then the columns' (0: none)
 * @param out        gets the board
 * @return false if a clue can't be met
 */
bool gen_geom_sky(const uint8_t clues[18], sudoku_geom_t *out);
/** @} */
/** @} */
