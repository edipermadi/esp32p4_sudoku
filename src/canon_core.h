/**
 * @file canon_core.h
 * @ingroup canon_core
 * @brief A puzzle's canonical hash: the same for the puzzle turned, mirrored or with its digits renumbered,
 *        so a puzzle played before is known again under another name (Puzzles, generated, imported).
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup canon_core Canonical puzzle hash
 * @ingroup topic_puzzles
 * @brief The same hash for a puzzle turned, mirrored or renumbered.
 *
 * The givens are taken through each of the board's symmetries (of the 8 turns and mirrors, those that map
 * its groups onto themselves: canon_symmetries()), each copy renumbered in reading order (the first digit
 * met becomes 1, the next new one 2 ...), and the smallest copy is hashed (FNV-1a, 64 bits) with the
 * variant. Only variants whose rules don't care which digit is which and that have no layout of their own
 * (cages, lines, marks, clues, regions) have one: Classic, X-Sudoku, Mini 6x6, Kids 4x4, Hyper,
 * Anti-Knight, Anti-King, Disjoint Groups, Asterisk, Center Dot, Argyle, Girandola, Chess Sudoku and
 * Anti-Diagonal. Plain C: the PC tests build it.
 * @{
 */

/**
 * @brief The board's symmetries a variant's puzzles are compared under.
 * @param[in] variant  sudoku_variant_t
 * @return a bit per transform (canon_cell()), 0 if the variant has no canonical hash
 */
uint8_t canon_symmetries(int variant);

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
 * or renumbered, and (almost surely) different otherwise.
 * @param[in] variant  sudoku_variant_t
 * @param[in] givens  the givens, 0 = empty
 * @return the hash, never 0; 0 if the variant has none
 */
uint64_t canon_hash(int variant, const uint8_t givens[81]);

/** @} */

#ifdef __cplusplus
}
#endif
