/**
 * @file gen_internal.h
 * @ingroup generator
 * @brief What the puzzle generator's files share (private to them; the rest uses generator.h).
 *
 * | File            | What                                                                       |
 * |-----------------|----------------------------------------------------------------------------|
 * | gen_geometry.c  | the names, each variant's board                                            |
 * | gen_texts.c     | the variants' layouts as text, read and written                            |
 * | gen_solver.c    | counting and filling: every variant's rules (grid_free()), the search       |
 * | gen_grade.c     | human-style grading: the techniques and the level they give                 |
 * | gen_make.c      | generating: digging to a level, each variant's decorations asked for        |
 * | gen_regions.c   | Jigsaw regions and Killer cages made for a solution                         |
 * | gen_lines.c     | the line variants' lines made for a solution                                |
 * | gen_hints.c     | the next step a person would take                                           |
 *
 * The solver's checks and their tables stay in one file (gen_solver.c), so its inner loop keeps
 * them inlined; the small helpers every part uses are static inline here.
 */
#pragma once
#include "generator.h"
#include "gen_bits.h"

/** @addtogroup generator
 *  @{ */

/** @name Small helpers (static inline)
 *  @{ */
/**
 * @brief the cells of row / column lu (0..17) as seen from its clue (the left, the top)
 * @param[in] lu  the row (0..8) or column (9..17)
 * @param[out] cells  gets its 9 cells
 */
static inline void line_cells(int lu, int cells[9])
{
    for (int k = 0; k < 9; k++) {
        cells[k] = lu < 9 ? lu * 9 + k : k * 9 + (lu - 9);
    }
}

/**
 * @brief buildings seen along cells (the digits in vals, in order): a taller one hides those behind it
 * @param[in] vals  the digits
 * @param[in] cells  the 9 cells, from the clue
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
 * @param[in] G  the board
 * @param[in] cell  the cell
 * @param[out] out  gets its units
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
 * @param[in] G  the board
 * @param[in] cell  the cell
 * @param[in] u  the unit
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
 * @param[in,out] s  the state (0: a fixed seed)
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
 * @param[in] G          the board
 * @param[in] i          the cell
 * @param[in] v          the digits (0: empty)
 * @param[in] cand       the candidates of the empty ones, or NULL (then empty neighbours allow anything)
 * @return a candidate mask (bit d = digit d)
 */
uint16_t gen_edge_allow(const sudoku_geom_t *G, int i, const uint8_t *v, const uint16_t *cand);
/**
 * @brief The digits the cell at a place of a line may hold, by the line's rule (Thermo, Arrow, Renban, Whispers,
 *        Palindrome, Region Sum, Zipper, Between Lines).
 * @param[in] G          the board
 * @param[in] t          the line
 * @param[in] p          the place on it
 * @param[in] v          the digits (0: empty)
 * @param[in] cand       the candidates of the empty ones, or NULL
 * @return a candidate mask
 */
uint16_t gen_line_allow(const sudoku_geom_t *G, int t, int p, const uint8_t *v, const uint16_t *cand);
/**
 * @brief Whether a row or column's digits so far can still see as many buildings as its Skyscraper clue says.
 * @param[in] x          the digits along it, from the clue (0: empty)
 * @param[in] c          the clue
 * @return true: still possible
 */
bool is_gen_sky_line_ok(const uint8_t x[9], int c);
/**
 * @brief The digits the cell at a place of an X-Sums row or column may hold, by its clue.
 * @param[in] G          the board
 * @param[in] lu         the row (0..8) or column (9..17), with a clue
 * @param[in] at         the cell's place along it, from the clue
 * @param[in] v          the digits (0: empty)
 * @param[in] cand       the candidates of the empty ones, or NULL (then any digit not on the line yet)
 * @return a candidate mask
 */
uint16_t gen_xsum_allow(const sudoku_geom_t *G, int lu, int at, const uint8_t *v, const uint16_t *cand);
/**
 * @brief The digits the cell at a place of a Numbered Rooms row or column may hold, by its clue.
 * @param[in] G          the board
 * @param[in] lu         the row (0..8) or column (9..17), with a clue
 * @param[in] at         the cell's place along it, from the clue
 * @param[in] v          the digits (0: empty)
 * @param[in] cand       the candidates of the empty ones, or NULL (then any digit not on the line yet)
 * @return a candidate mask
 */
uint16_t gen_nroom_allow(const sudoku_geom_t *G, int lu, int at, const uint8_t *v, const uint16_t *cand);
/**
 * @brief Whether a row or column's digits so far can still add up to its Sum Skyscrapers clue (the buildings seen).
 * @param[in] x          the digits along it, from the clue (0: empty)
 * @param[in] c          the clue
 * @return true: still possible
 */
bool is_gen_skysum_line_ok(const uint8_t x[9], int c);
/**
 * @brief Before Nine: can a line's digits so far (in order from the clue, 0 = empty) still keep its clue
 *        (the digits before the 9 add up to c)? Exact for a full line (or one whose 9 and the cells before
 *        it are filled); otherwise bounds: the empty cells before the 9 take the smallest or largest digits
 *        missing from the line.
 * @param[in] x  the line's digits
 * @param[in] c  the clue
 * @return true: still possible
 */
bool is_gen_b9_line_ok(const uint8_t x[9], int c);
/**
 * @brief Next to Nine: can a line's digits so far (0 = empty) still keep its clue (the digits beside its 9 are c's)?
 *        Each place the 9 could be is tried: its neighbours must be able to be exactly the clue's digits. Exact for
 *        a full line.
 * @param[in] x  the line's digits
 * @param[in] c  the clue, as n9_set keeps it (bit d - 1)
 * @return true: still possible
 */
bool is_gen_n9_line_ok(const uint8_t x[9], int c);
/**
 * @brief Battlefield: can a line's digits so far (0 = empty) still keep its clue? Each first digit X and last Y it
 *        could have is tried: the filled cells of the overlap (or the gap) and what its empty ones could add must
 *        reach the sum. Exact for a full line.
 * @param[in] x  the line's digits
 * @param[in] c  the clue, as bf_clue keeps it (the sum + 1)
 * @return true: still possible
 */
bool is_gen_bf_line_ok(const uint8_t x[9], int c);
/**
 * @brief The digits the cell at a place of a row or column may hold, by its clue about the first three
 *        cells (Frame: their sum; Outside Sudoku: digits among them; Rossini: rising, falling or neither).
 * @param[in] G          the board
 * @param[in] lu         the row (0..8) or column (9..17), with a clue
 * @param[in] at         the cell's place along it, from the clue (0..2: else anything)
 * @param[in] v          the digits (0: empty)
 * @param[in] cand       the candidates of the empty ones, or NULL (then any digit not on the line yet)
 * @return a candidate mask
 */
uint16_t gen_first3_allow(const sudoku_geom_t *G, int lu, int at, const uint8_t *v, const uint16_t *cand);
/**
 * @brief Counts a puzzle's solutions, up to a limit, within a node budget.
 * @param[in] G          the board
 * @param[in] cells      the givens (0: empty)
 * @param[in] limit      stop at this many
 * @param[in] node_limit stop after this many search nodes (0: no limit)
 * @return the solutions found (up to limit); -1 if the budget ran out
 */
int gen_count_bounded(const sudoku_geom_t *G, const uint8_t cells[81], int limit, uint32_t node_limit);
/**
 * @brief A random full grid for a board.
 * @param[in] G          the board
 * @param[out] out        gets it
 * @param[in,out] rng        the random state
 * @param[in] node_limit the search's budget (0: none)
 * @return false if none was found within the budget
 */
bool gen_random_full_grid(const sudoku_geom_t *G, uint8_t out[81], uint32_t *rng, uint32_t node_limit);
/**
 * @brief The Sandwich sums k distinct digits from 2..8 can make.
 * @param[in] k  how many digits, 0..7
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
 * @param[out] t  the step's description, or NULL: nowhere
 */
void gen_set_trace(sudoku_tech_t *t);
/**
 * @brief The hardest technique a puzzle needs, solved the way a person would.
 * @param[in] G          the board
 * @param[in] givens     the givens
 * @return its tier (a gen_level_t), or -1 if the techniques don't solve it
 */
int gen_technique_tier(const sudoku_geom_t *G, const uint8_t givens[81]);
/**
 * @brief The fewest givens an Easy puzzle of a board keeps.
 * @param[in] G          the board
 * @return the count
 */
int gen_easy_min_clues(const sudoku_geom_t *G);
/**
 * @brief Sets up the techniques' state for a board: the digits, and each empty cell's candidates.
 * @param[out] L          gets it
 * @param[in] G          the board
 * @param[in] vals       the digits (0: empty)
 */
void gen_logic_load(logic_t *L, const sudoku_geom_t *G, const uint8_t *vals);
/**
 * @brief Places naked and hidden singles.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_singles(logic_t *L);
/**
 * @brief A technique: Killer: what each cage's sum leaves, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_cage_logic(logic_t *L);
/**
 * @brief A technique: Consecutive: the bars, and their absence, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_consec_logic(logic_t *L);
/**
 * @brief A technique: Kropki, XV, Greater-Than: what the marks leave, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_edge_logic(logic_t *L);
/**
 * @brief A technique: Quadruple: a clue's digits that must go in its cells, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_quad_logic(logic_t *L);
/**
 * @brief A technique: Skyscraper: what the clues leave, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_sky_logic(logic_t *L);
/**
 * @brief A technique: X-Sums: what the clues leave, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_xsum_logic(logic_t *L);
/**
 * @brief A technique: Numbered Rooms: what the clues leave, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_nroom_logic(logic_t *L);
/**
 * @brief A technique: Sum Skyscrapers: what the clues leave, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_skysum_logic(logic_t *L);
/**
 * @brief A technique: Before Nine: what the clues leave, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_b9_logic(logic_t *L);
/**
 * @brief Point Symmetry: a cell keeps only the digits that 10 minus some candidate of its mirror is
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_psym_logic(logic_t *L);
/**
 * @brief Quadro: a 2x2 block whose three other cells can only be odd (or even) takes the other parity in the fourth
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_quadro_logic(logic_t *L);
/**
 * @brief Anti-Queen 9s: a 9 rules 9 out along both its diagonals
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_aq9_logic(logic_t *L);
/**
 * @brief A technique: Next to Nine: what the clues leave, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_n9_logic(logic_t *L);
/**
 * @brief Clone Sudoku: a cell keeps only what its clone can still be
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_clone_logic(logic_t *L);
/**
 * @brief A technique: Search Nine: what the arrows leave. An arrow's cell keeps the distances that reach a cell
 *        that can be 9; a cell is no 9 when an arrow along its row or column can't point at it; the cell an arrow
 *        with a digit points to is the 9.
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_s9_logic(logic_t *L);
/**
 * @brief A technique: Battlefield: what the clues leave, candidates taken out.
 * @param[in,out] L  the state
 * @return true if it changed anything
 */
bool gen_bf_logic(logic_t *L);
/**
 * @brief A technique: Battenburg: a marked 2x2 block's cells take the parity their diagonal partner can have and the
 *        other one from their side neighbours; in an unmarked block whose other three are fixed in a checkerboard's
 *        way, the fourth takes the other parity.
 * @param[in,out] L  the state
 * @return true if a candidate went
 */
bool gen_batten_logic(logic_t *L);
/**
 * @brief A technique: Frame, Outside Sudoku, Rossini: what the clues about the first three cells leave,
 *        candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_first3_logic(logic_t *L);
/**
 * @brief A technique: Diagonal Non-Consecutive: a digit 1 away from all a corner neighbour can hold,
 *        taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_diagnc_logic(logic_t *L);
/**
 * @brief 159 Sudoku: a row's columns 1, 5 and 9 against the cells they point at
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_159_logic(logic_t *L);
/**
 * @brief Does the board have a clue about the first three cells of this row or column (Frame, Outside
 *        Sudoku, Rossini: every line)?
 * @param[in] G          the board
 * @param[in] lu         the row (0..8) or column (9..17)
 * @return true if so
 */
static inline bool is_gen_first3_clued(const sudoku_geom_t *G, int lu)
{
    return G->rossini || (G->frame && G->frame_sum[lu]) || (G->outside && G->outside_set[lu]);
}
/**
 * @brief A technique: Little Killer: what the diagonals' sums leave, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_lk_logic(logic_t *L);
/**
 * @brief A technique: the lines (Thermo, Arrow, Renban, Whispers): what their rules leave, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_thermo_logic(logic_t *L);
/**
 * @brief A technique: Sandwich: what the sums leave, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_sandwich_logic(logic_t *L);
/**
 * @brief A technique: locked candidates (pointing, claiming), candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_locked_candidates(logic_t *L);
/**
 * @brief A technique: naked and hidden pairs, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_pairs(logic_t *L);
/**
 * @brief A technique: naked triples, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_naked_triple(logic_t *L);
/**
 * @brief A technique: XY-Wing, candidates taken out.
 * @param[in,out] L          the state
 * @return true if it changed anything
 */
bool gen_xy_wing(logic_t *L);
/**
 * @brief A technique: X-Wing (size 2) or Swordfish (size 3), candidates taken out.
 * @param[in,out] L          the state
 * @param[in] size       2 or 3
 * @return true if it changed anything
 */
bool gen_fish(logic_t *L, int size);
/** @} */

/** @name gen_geometry.c
 *  @{ */
/**
 * @brief Builds a board from its decorations: Killer, a classic board plus cages. Cage-mates become
 *        peers.
 * @param[in] cage_of    the cage of each cell (CAGE_NONE: none)
 * @param[in] sums       each cage's sum; all 0: a Sumless Killer board (no sums)
 * @param[in] ncages     how many cages
 * @param[out] out        gets the board
 * @return false if a cage is empty, has more than 9 cells, or its sum can't be made of distinct digits
 *         (or only some sums are 0)
 */
bool gen_geom_killer(const uint8_t cage_of[81], const uint8_t *sums, int ncages, sudoku_geom_t *out);
/**
 * @brief Builds a Product Killer board: a classic board plus cages, each with the product of its digits.
 * @param[in] cage_of  the cage of each cell (CAGE_NONE: none)
 * @param[in] prods    each cage's product (none 0)
 * @param[in] ncages   how many cages
 * @param[out] out     gets the board
 * @return false if the cages aren't a board's (see gen_geom_killer())
 */
bool gen_geom_product(const uint8_t cage_of[81], const uint16_t *prods, int ncages, sudoku_geom_t *out);
/**
 * @brief Product Killer: the digits cell i may hold by its cage's product (with the other digits placed)
 * @param[in] G  the board
 * @param[in] v  the digits (0: empty; i's own is left out)
 * @param[in] c  i's cage
 * @param[in] i  the cell
 * @return a candidate mask
 */
uint16_t gen_product_allow(const sudoku_geom_t *G, const uint8_t *v, int c, int i);
/**
 * @brief Builds a board from its decorations: a classic board with lines of the variant's kind (Thermo,
 *        Arrow, Renban, Whispers).
 * @param[in] variant    the variant
 * @param[in] cells      each line's cells, in order
 * @param[in] lens       each line's length
 * @param[in] n          how many lines
 * @param[out] out        gets the board
 * @return false if there are none or too many
 */
bool gen_geom_lines(int variant, const uint8_t cells[][9], const uint8_t *lens, int n, sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Quadruple, a classic board with these circles.
 * @param[in] at         each circle's top-left cell
 * @param[in] n          each one's digits, how many
 * @param[in] dig        each one's digits
 * @param[in] nq         how many circles
 * @param[out] out        gets the board
 * @return false if a corner is used twice, or there are too many
 */
bool gen_geom_quad(const uint8_t *at, const uint8_t *n, const uint8_t (*dig)[4], int nq, sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Mathrax, a classic board with these circles.
 * @param[in] at         each circle's top-left cell
 * @param[in] op         each one's operation, MX_*
 * @param[in] val        each one's number (0 for MX_EVEN, MX_ODD)
 * @param[in] nc         how many circles
 * @param[out] out        gets the board
 * @return false if a corner is used twice, a clue isn't one, or there are too many
 */
bool gen_geom_mathrax(const uint8_t *at, const uint8_t *op, const uint8_t *val, int nc, sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Sandwich, a classic board with sums beside the rows and
 *        columns.
 * @param[in] sums       the rows' sums, then the columns' (0: none)
 * @param[out] out        gets the board
 * @return false if a sum can't be made
 */
bool gen_geom_sandwich(const uint8_t sums[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Little Killer, a board with these diagonals.
 * @param[in] n          how many
 * @param[in] top        each one starts above the board (else beside it)
 * @param[in] idx        its column (from the top) or row
 * @param[in] dir        its direction
 * @param[in] sum        its sum
 * @param[out] out        gets the board
 * @return false if there are none or too many
 */
bool gen_geom_lk(int n, const bool *top, const uint8_t *idx, const uint8_t *dir, const uint8_t *sum, sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Frame, a classic board with clues beside the rows and
 *        columns.
 * @param[in] clues      the rows' clues, then the columns' (0: none)
 * @param[out] out        gets the board
 * @return false if a clue can't be made by three different digits
 */
bool gen_geom_frame(const uint8_t clues[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Outside Sudoku, a classic board with clues beside the rows
 *        and columns.
 * @param[in] sets       the rows' clues, then the columns' (digit masks; 0: none)
 * @param[out] out        gets the board
 * @return false if a clue has more than 3 digits
 */
bool gen_geom_outside(const uint16_t sets[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Rossini, a classic board with an arrow (or none) by every
 *        row and column.
 * @param[in] dirs       the rows', then the columns' (ROSSINI_*)
 * @param[out] out        gets the board
 * @return false if one isn't ROSSINI_*
 */
bool gen_geom_rossini(const uint8_t dirs[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: X-Sums, a classic board with clues beside the rows and
 *        columns.
 * @param[in] clues      the rows' clues, then the columns' (0: none)
 * @param[out] out        gets the board
 * @return false if a clue can't be met
 */
bool gen_geom_xsum(const uint8_t clues[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Numbered Rooms, a classic board with clues beside the rows
 *        and columns.
 * @param[in] clues      the rows' clues, then the columns' (0: none)
 * @param[out] out        gets the board
 * @return false if a clue can't be met
 */
bool gen_geom_nroom(const uint8_t clues[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Sum Skyscrapers, a classic board with clues beside the rows
 *        and columns.
 * @param[in] clues      the rows' clues, then the columns' (0: none)
 * @param[out] out        gets the board
 * @return false if a clue can't be met
 */
bool gen_geom_skysum(const uint8_t clues[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Before Nine, a classic board with clues beside the rows and
 *        columns.
 * @param[in] clues      the rows' clues, then the columns' (0: none; else 1..36)
 * @param[out] out        gets the board
 * @return false if a clue isn't one
 */
bool gen_geom_b9(const uint8_t clues[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Next to Nine, a classic board with clues beside the rows and columns.
 * @param[in] sets       the rows' clues, then the columns' (bit d - 1: d is beside the 9; 0: none)
 * @param[out] out        gets the board
 * @return false if a clue isn't one (more than 2 digits)
 */
bool gen_geom_n9(const uint8_t sets[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Clone Sudoku, a classic board with these pairs of areas.
 * @param[in] n          the pairs
 * @param[in] cells      each pair's first area
 * @param[in] lens       its cells
 * @param[in] dr         how far its clone is moved: rows
 * @param[in] dc         columns
 * @param[out] out        gets the board
 * @return false if they aren't pairs (overlapping, off the board, a cell and its clone in one unit, too many)
 */
bool gen_geom_clone(int n, const uint8_t cells[][9], const uint8_t *lens, const int8_t *dr, const int8_t *dc,
                    sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Search Nine, a classic board with these arrows.
 * @param[in] dirs  each cell's arrow, S9_* (S9_NONE: none)
 * @param[out] out  gets the board
 * @return false if an arrow isn't one (pointing off the board, or not a way)
 */
bool gen_geom_s9(const uint8_t dirs[81], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Battlefield, a classic board with clues beside the rows and columns.
 * @param[in] clues  the rows' clues, then the columns' (the sum + 1; 0: none)
 * @param[out] out  gets the board
 * @return false if a clue isn't one (over 45)
 */
bool gen_geom_bf(const uint8_t clues[18], sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Battenburg, a classic board with these marks (every one shown).
 * @param[in] bat  per cell, 1: a mark at its bottom-right corner
 * @param[out] out  gets the board
 * @return false if a mark is on the last row or column
 */
bool gen_geom_batten(const uint8_t bat[81], sudoku_geom_t *out);
/**
 * @brief Builds an Odd/Even Skyscrapers board from its 18 clues (0 none; 1..5 odd; PSKY_EVEN | 0..4 even)
 * @param[in] clues  the clues
 * @param[out] out  gets the board
 * @return false if a clue can't be
 */
bool gen_geom_psky(const uint8_t clues[18], sudoku_geom_t *out);
/**
 * @brief Odd/Even Skyscrapers: can a line still keep its clue? x: its digits from the clue's side (0 empty;
 * only the run of filled ones from the start counts)
 * @param[in] x  the digits
 * @param[in] c  the clue (1..5 odd; PSKY_EVEN | 0..4 even)
 * @return true if still possible
 */
bool is_gen_psky_line_ok(const uint8_t x[9], int c);
/**
 * @brief Magic Square: the digits a cell of the centre box may hold: in each of its lines (its row and column of
 * the box, and a diagonal through it) two others, by their digits or candidates, must make 15 with it
 * @param[in] v  the digits (0: empty)
 * @param[in] cand  the candidates of the empty ones, or NULL (then any)
 * @param[in] i  the cell, in the centre box
 * @return a candidate mask
 */
uint16_t gen_magic_allow(const uint8_t *v, const uint16_t *cand, int i);
/**
 * @brief Magic Square: each cell of the centre box by its lines (gen_magic_allow() with the candidates)
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_magic_logic(logic_t *L);
/**
 * @brief Anti-Diagonal: a long diagonal with three different digits already: its empty cells take one of them
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_antidiag_logic(logic_t *L);
/**
 * @brief Dutch Flatmates: a 5 can't go where neither a 1 above nor a 9 below can; a 5 whose 1 above can't come
 *        takes the 9 below (and the other way round)
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_flatmates_logic(logic_t *L);
/**
 * @brief Mathrax: a circle's cell keeps only the digits some candidate of its diagonal partner makes the
 *        operation with
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_mathrax_logic(logic_t *L);
/**
 * @brief Odd/Even Skyscrapers: each clue's possible orders of the line's last missing digits
 * @param[in,out] L  the logic state
 * @return true if a candidate went
 */
bool gen_psky_logic(logic_t *L);
/**
 * @brief Pill Arrows: up to want arrows over the full grid: a pill of two cells side by side (or one above the
 * other) whose number is under 40, and a shaft from its second cell adding up to it, never crossing
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  arrows wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
bool gen_pill_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out);
/**
 * @brief Builds a board from its decorations: Skyscraper, a classic board with clues beside the rows and
 *        columns.
 * @param[in] clues      the rows' clues, then the columns' (0: none)
 * @param[out] out        gets the board
 * @return false if a clue can't be met
 */
bool gen_geom_sky(const uint8_t clues[18], sudoku_geom_t *out);
/* ---- gen_regions.c, gen_lines.c: what gen_make.c calls */
/**
 * @brief A random Jigsaw map: the 3x3 boxes, reshaped by trading cells across
 * region borders (a cell of A goes to B, a cell of B next to A goes to A)
 * while both regions stay in one piece.
 * @param[out] map  gets the region of each cell
 * @param[in,out] rng  the random state
 */
void gen_jigsaw_map(uint8_t map[81], uint32_t *rng);
/**
 * @brief A random Jigsaw 7x7 map: the seven rows, reshaped the same way (more trades: rows are far from boxes),
 * drawn again while a region keeps 6 or more cells in one row or column; the cells off the 7x7 get region 0.
 * @param[out] map  gets the region of each cell
 * @param[in,out] rng  the random state
 */
void gen_jigsaw7_map(uint8_t map[81], uint32_t *rng);
/**
 * @brief Random cages over a solved grid: from each cell not in a cage yet (in a
 * random order), a cage grows to 2..max_size cells through neighbours whose
 * digit isn't in it yet. A lone cell left over joins a neighbouring cage
 * that can take its digit, if there is one.
 * @param[in] full  the solution
 * @param[in] max_size  cells per cage at most
 * @param[in,out] rng  the random state
 * @param[out] out  gets the board
 * @return false if the cages don't make one
 */
bool gen_killer_cages(const uint8_t full[81], int max_size, uint32_t *rng, sudoku_geom_t *out);
/**
 * @brief Sumless Killer: cages of 2-5 cells drawn over the full grid as for Killer, kept where they reach
 *        over more than one row, column and box (one inside a box says nothing the box doesn't), no sums
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[out] out  gets the board
 * @return false: fewer than 4 cages left
 */
bool gen_sumless_cages(const uint8_t full[81], uint32_t *rng, sudoku_geom_t *out);
/**
 * @brief Product Killer: cages drawn over the full grid as for Killer, each with the product of its digits
 * @param[in] full  the solution
 * @param[in] max_size  the cage size aimed at, as gen_killer_cages()
 * @param[in,out] rng  the random state
 * @param[out] out  gets the board
 * @return false: no cages
 */
bool gen_product_cages(const uint8_t full[81], int max_size, uint32_t *rng, sudoku_geom_t *out);
/**
 * @brief Extra Regions: up to want regions of 5 to 9 connected cells over the full grid, each with
 *        different digits, reaching over a box, a row and a column, and never touching another (corners too)
 * @param[in] full  the solution
 * @param[in] want  regions wanted
 * @param[in,out] rng  the random state
 * @param[out] out  gets the board (it must be a classic board already: its boxes are read)
 * @return false: fewer than 3
 */
bool gen_extra_regions(const uint8_t full[81], int want, uint32_t *rng, sudoku_geom_t *out);
/**
 * @brief Between Lines: up to want lines over the full grid, from a circle, a random walk of up to 7
 * cells whose digits all lie on one side of it (above it, or below), kept at the longest length
 * whose last cell (the other circle) lies beyond all the cells between; a line never crosses itself
 * or another
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
bool gen_between_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out);
/**
 * @brief Lockout Lines, Double Arrow, 10 Lines: up to want lines over the full grid, each a random walk of
 * up to 6 cells (10 Lines: 8) kept at the longest length that keeps the rule (Lockout: the ends 4+ apart and
 * every cell between outside their range; Double Arrow: the ends add up to the cells between; 10 Lines:
 * runs of 10 to the end; Average Lines: the first cell the average of the rest); a line never crosses itself or
 * another
 * @param[in] variant  VAR_LOCKOUT, VAR_DARROW, VAR_TEN or VAR_AVG
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
bool gen_ends_paths(int variant, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out);
/**
 * @brief Entropic, Modular Lines: up to want lines over the full grid, random walks of 3 to 8 cells
 * through cells whose digit keeps the rule: each of the first three in a group of its own, every one
 * after in the group of the cell three back; a line never crosses itself or another
 * @param[in] variant  VAR_ENTROPIC or VAR_MODULAR
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
bool gen_group_paths(int variant, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out);
/**
 * @brief Palindrome: up to want lines (3-9 cells) over the full grid, grown from the middle out, a
 * cell at each end at a time, both holding the same digit (so they never see each other)
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
bool gen_palindrome_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out);
/**
 * @brief Region Sum Lines: up to want lines over the full grid, random walks of up to 8 cells kept at
 * the longest length (3 or more) where each box they pass through adds up to the same
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
bool gen_regionsum_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out);
/**
 * @brief Arrow, Renban, Whispers, Parity: up to want lines drawn over the full grid as random walks (side by
 * side or diagonal, not crossing), each kept only if the grid keeps its rule: an arrow's shaft
 * (1-3 cells) adds up to its circle, a Renban (3-5 cells) is a run of consecutive digits, along a
 * German (Dutch) Whispers line (3-6 cells) neighbours are 5 (4) or more apart, along a Parity line (3-8
 * cells) one odd, one even, on a Nabner line (3-5 cells) every two digits 2 or more apart
 * @param[in] variant  VAR_ARROW, VAR_RENBAN, VAR_WHISPER, VAR_DUTCH, VAR_PARITY, VAR_NABNER or VAR_SEQ (3-5 cells whose
 *                     digits go up or down by one step)
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
bool gen_rule_paths(int variant, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out);
/**
 * @brief Thermo: up to want thermometers (3-6 cells) drawn over the full grid as random walks whose digits rise from the bulb;
 *        Slow Thermo (3-7 cells): or stay the same (where a step can: half the time; a solved grid only repeats a
 *        digit in cells that don't see each other)
 * @param[in] slow  Slow Thermo
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  thermometers wanted
 * @param[out] out  gets the board
 * @return false: fewer than 3
 */
bool gen_thermo_paths(bool slow, const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out);
/**
 * @brief Zipper Lines: up to want lines (3-7 cells) over the full grid, grown from the middle out, a
 * cell at each end at a time, each pair adding up to the line's total (odd: its middle digit; even:
 * the middle pair's sum); a line never crosses itself or another
 * @param[in] full  the solution
 * @param[in,out] rng  the random state
 * @param[in] want  lines wanted
 * @param[out] out  gets the lines
 * @return false: fewer than 3
 */
bool gen_zipper_paths(const uint8_t full[81], uint32_t *rng, int want, sudoku_geom_t *out);
/** @} */
/** @} */
