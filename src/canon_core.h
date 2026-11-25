/**
 * @file canon_core.h
 * @ingroup canon_core
 * @brief A puzzle's canonical hash: the same for the puzzle turned, mirrored or with its digits renumbered,
 *        so a puzzle played before is known again under another name (Puzzles, generated, imported).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup canon_core Canonical puzzle hash
 * @ingroup topic_puzzles
 * @brief The same hash for a puzzle turned, mirrored or renumbered.
 *
 * Every variant has one. The givens are taken through each of the board's symmetries (of the 8 turns and
 * mirrors, those that keep the puzzle the same puzzle: canon_symmetries()), each copy renumbered as far as the
 * rules allow (canon_digits()), and the smallest copy is hashed (FNV-1a, 64 bits) with the variant.
 *
 * Without a layout of their own (canon_hash()):
 * - Classic, X-Sudoku, Mini 6x6, Mini 8x8, Kids 4x4, Hyper, Anti-Knight, Anti-King, Disjoint Groups, Asterisk, Center Dot,
 *   Argyle, Girandola, Chess Sudoku and Anti-Diagonal: renumbered in reading order (the first digit met becomes
 *   1, the next new one 2 ...);
 * - Point Symmetry: the pairs that make 10 kept together, 5 kept;
 * - Non-Consecutive, Diagonal Non-Consecutive, Magic Square: each digit d kept, or every one turned into 10 - d;
 * - Dutch Flatmates: only the turns that keep up up or down (its rule is about the cells above and below); 5
 *   kept, 1 and 9 swapped where the board is turned upside down, the rest any way;
 * - 159 Sudoku: no quarter turns, and every digit d to 10 - d exactly when the columns are mirrored (its
 *   columns 1, 5 and 9 hold column numbers);
 * - Quadro: odd digits renumbered among the odd, even ones among the even;
 * - Anti-Queen 9s: 9 kept, the rest any way.
 *
 * With their layout turned with the givens (canon_hash_geom(); digits as they are but where said):
 * - Jigsaw, Jigsaw 6x6, 7x7, 8x8: its regions, relabelled in reading order (digits any way);
 * - Killer, Product Killer, Calcudoku Killer: their cages relabelled in reading order, with their sums or products
 *   (Calcudoku's: each cage's operation and number); Sumless
 *   Killer, Extra Regions: their cages or shaded regions (digits any way);
 * - the marks: Kropki, XV, Consecutive (between side-by-side cells), Greater-Than (its signs turned round with
 *   their cells), Even-Odd, Fortress (on cells), Diagonal Consecutive (at corners);
 * - Quadruple, Mathrax: their circles moved with their four cells, sorted;
 * - the 21 line variants: their lines sorted, each from its start where its way matters (a bulb, a circle, a
 *   pill), else whichever way round is smaller; Pill Arrows only as it is or mirrored about the main diagonal
 *   (its pill reads left to right or top to bottom);
 * - Sandwich, Next to Nine, Battlefield: each clue with its row or column (the same from either end: all 8);
 * - Clone Sudoku: its areas, each pair numbered in reading order (digits any way);
 * - Battenburg: its marks, each with the block of four turned onto it (digits as they are: odd and even matter);
 * - Search Nine: its arrows, turned with their cells (digits as they are: an arrow's digit is a distance);
 * - the clues left of the rows and above the columns (Skyscraper, X-Sums, Frame, Outside Sudoku, Rossini,
 *   Numbered Rooms, Sum Skyscrapers, Odd/Even Skyscrapers, Before Nine; Little Killer's diagonals, as their
 *   cells and sums): only as it is or mirrored about the main diagonal, which keeps them left and above.
 *
 * Plain C: the PC tests build it.
 * @{
 */

/**
 * @brief The board's symmetries a variant's puzzles are compared under.
 * @param[in] variant  sudoku_variant_t
 * @return a bit per transform (canon_cell()), 0 if the variant has no canonical hash
 */
uint8_t canon_symmetries(int variant);

/** @brief How a variant's puzzles may be renumbered and still be the same puzzle (canon_digits()). */
enum {
    CANON_ANY = 0,                      /**< any way: renumbered in reading order */
    CANON_PAIRS,                        /**< keeping the pairs that make 10 together, 5 as it is (Point Symmetry) */
    CANON_FLIP,                         /**< not at all, or every digit d to 10 - d (1 apart stays 1 apart, a sum of
                                         *   three that's 15 stays 15) */
    CANON_FLATMATES,                    /**< 5 as it is; 1 and 9 as they are, swapped where the board is turned upside
                                         *   down (a 1 above a 5 becomes one below it: a 9 there); the rest any way */
    CANON_159,                          /**< as they are, or every digit d to 10 - d where the columns are mirrored
                                         *   (159 Sudoku: its digits in columns 1, 5 and 9 are column numbers) */
    CANON_PARITY,                       /**< odd to odd, even to even, each in reading order (Quadro) */
    CANON_FIX9                          /**< 9 as it is, the rest any way (Anti-Queen 9s) */
};

/**
 * @brief How a variant's puzzles are renumbered for the hash.
 * @param[in] variant  sudoku_variant_t
 * @return CANON_ANY, CANON_PAIRS, CANON_FLIP, CANON_FLATMATES, CANON_159, CANON_PARITY or CANON_FIX9
 */
int canon_digits(int variant);

/**
 * @brief Where a cell goes under one of the 8 turns and mirrors of an n x n board (cells as in the
 * firmware: row * 9 + column, the board in the top-left n x n).
 * @param[in] t  0 none, 1 a quarter turn, 2 a half turn, 3 three quarters, 4 mirrored left-right,
 *               5 top-bottom, 6 about the main diagonal, 7 about the other
 * @param[in] n  the board's size: 4, 6 or 9
 * @param[in] cell  the cell
 * @return the cell it goes to
 */
int canon_cell(int t, int n, int cell);

/**
 * @brief A puzzle's canonical hash: equal for the same givens turned, mirrored (where the board allows)
 * or renumbered (as far as the variant allows: canon_digits()), and (almost surely) different otherwise.
 * @param[in] variant  sudoku_variant_t
 * @param[in] givens  the givens, 0 = empty
 * @return the hash, never 0; 0 if the variant has none, or needs its layout (is_canon_layout(): canon_hash_geom())
 */
uint64_t canon_hash(int variant, const uint8_t givens[81]);
/**
 * @brief Does a variant's hash need the puzzle's layout (Jigsaw's regions; the cages of Killer, Sumless Killer,
 * Product Killer and Extra Regions; Kropki's and XV's marks; the line variants' lines)?
 * @param[in] variant  sudoku_variant_t
 * @return true: hash it with canon_hash_geom()
 */
bool is_canon_layout(int variant);
/**
 * @brief A puzzle's canonical hash with its layout: canon_hash() for the variants without one; for Jigsaw,
 * Killer, Sumless Killer, Product Killer, Extra Regions, Kropki, XV and the line variants, the smallest of the 8
 * turned and mirrored copies of the givens and the layout together (regions or cages numbered in reading order, the
 * sums or products in that order; the marks between cells as the turned board has them; the lines sorted), so the
 * same puzzle turned, mirrored or (Jigsaw, Sumless Killer, Extra Regions) renumbered hashes the same.
 * @param[in] G  the puzzle's board (its variant, regions and cages)
 * @param[in] givens  the givens, 0 = empty
 * @return the hash, never 0; 0 if the variant has none
 */
uint64_t canon_hash_geom(const sudoku_geom_t *G, const uint8_t givens[81]);

/** @} */

#ifdef __cplusplus
}
#endif
