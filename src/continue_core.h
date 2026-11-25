/**
 * @file continue_core.h
 * @ingroup continue_core
 * @brief The current player's unfinished games, without LVGL (the screens: ui_continue.c and its parts).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "stats_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup continue_core Continue core
 * @ingroup topic_continue
 * @brief What Continue and the Unfinished games dialog work with, apart from drawing it: the current
 *        player's unfinished games in sudoku.db (up to STATS_MAX_GAMES, played last first) and the one in
 *        RAM (store_saved_game()), a game brought back into RAM to be played, a game discarded.
 *
 * Without a card there's only the game in RAM. The list is read again with continue_refresh(), when
 * Home or the Puzzles screen is drawn and before Continue opens anything.
 * @{
 */

/** @name The unfinished games
 *  @{ */
/** @brief Reads the current player's unfinished games from sudoku.db again (waits for it; none without it). */
void continue_refresh(void);
/**
 * @brief The unfinished games read last (continue_refresh()), played last first.
 * @return them (continue_ngames() of them)
 */
const stats_game_row_t *continue_games(void);
/**
 * @brief How many.
 * @return 0..STATS_MAX_GAMES
 */
int continue_ngames(void);
/**
 * @brief The game played last: the one in RAM if there is one, else sudoku.db's newest.
 * @param[out] id      gets its puzzle
 * @param[out] ms      gets its time so far, ms
 * @param[out] others  gets how many other unfinished games there are
 * @return false: none at all (*id STORE_NO_GAME)
 */
bool continue_last(uint16_t *id, uint32_t *ms, int *others);
/**
 * @brief Whether a puzzle is one of the current player's unfinished games (in RAM, or in the list read last).
 * @param[in] id  the puzzle
 * @return true: in progress
 */
bool is_continue_in_progress(uint16_t id);
/** @} */

/** @name One of them
 *  @{ */
/**
 * @brief Brings an unfinished game into RAM to be played (store_set_game(), with its move log), from sudoku.db
 *        unless it's the one there already.
 * @param[in] id  the puzzle
 * @return false: it isn't in sudoku.db any more (or there's no card)
 */
bool continue_load(uint16_t id);
/**
 * @brief Discards an unfinished game: the one in RAM, or one in sudoku.db.
 * @param[in] id  the puzzle
 */
void continue_discard(uint16_t id);
/**
 * @brief The cells on a puzzle's board (for "36 / 81 filled").
 * @param[in] id  the puzzle
 * @return 81, or 36 (Mini 6x6), 64 (Mini 8x8), 49 (Jigsaw 7x7), 36 / 64 (Jigsaw 6x6 / 8x8), 16 (Kids 4x4)
 */
int continue_cells(uint16_t id);
/** @} */

/** @} */

#ifdef __cplusplus
}
#endif
