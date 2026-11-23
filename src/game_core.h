/**
 * @file game_core.h
 * @ingroup game_core
 * @brief The game's rules check and undo history, without LVGL (the game screen: ui_game.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup game_core Game core
 * @ingroup topic_game
 * @brief What the game screen works out apart from drawing it: which digits break the puzzle's rules, and
 *        the undo history.
 * @{
 */

/** @brief The board in play: digits, pencil marks and cell colours (also each undo / redo step). */
typedef struct {
    uint8_t  val[81];            /**< digits, 0 = empty (givens included) */
    uint16_t notes[81];          /**< pencil marks: bit d = d */
    uint8_t  colors[81];         /**< cell colours, 0 = none, 1..CELL_COLORS (long press a cell) */
} board_t;

#define UNDO_MAX 128                    /**< undo steps kept */

/** @brief Undo and redo: a ring of the boards before each edit, and the boards undone (until the next edit). */
typedef struct {
    board_t *undo;                      /**< UNDO_MAX boards (the caller's, PSRAM on the board) */
    int head;                           /**< where the next one goes */
    int len;                            /**< undo steps kept */
    board_t *redo;                      /**< UNDO_MAX boards, newest last */
    int redo_len;                       /**< redo steps */
} game_history_t;

/**
 * @brief Forgets every undo and redo step (a game opened or restarted).
 * @param[in,out] h  the history
 */
void game_history_clear(game_history_t *h);
/**
 * @brief Keeps a board before an edit (the oldest goes when full).
 * @param[in,out] h        the history
 * @param[in] b            the board
 * @param[in] keep_redo    false (an edit): the redo steps go; true (a redo): they stay
 */
void game_history_push(game_history_t *h, const board_t *b, bool keep_redo);
/**
 * @brief Undo: the board goes back one step (it is kept for Redo).
 * @param[in,out] h  the history
 * @param[in,out] b  the board
 * @return false: nothing to undo
 */
bool game_history_undo(game_history_t *h, board_t *b);
/**
 * @brief Redo: puts back the last board undone (the board now is kept for Undo).
 * @param[in,out] h  the history
 * @param[in,out] b  the board
 * @return false: nothing to redo
 */
bool game_history_redo(game_history_t *h, board_t *b);

/**
 * @brief Marks every filled cell whose digit breaks a rule of the puzzle: a repeat in a unit, and each variant's
 *        own rules (marks, cages, lines, outside clues) as far as the digits already in can tell.
 * @param[in] geo   the puzzle's geometry (sudoku_geom(), or a Jigsaw's)
 * @param[in] val   81 digits, 0 = empty
 * @param[out] out  81 flags: true = the digit breaks a rule
 * @return how many cells are marked
 */
int game_conflicts(const sudoku_geom_t *geo, const uint8_t *val, bool *out);

/** @} */

#ifdef __cplusplus
}
#endif
