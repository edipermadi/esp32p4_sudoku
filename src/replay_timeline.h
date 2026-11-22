/**
 * @file replay_timeline.h
 * @ingroup replay_timeline
 * @brief A move log played back: the board at any step, the play times, the speed.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "moves.h"
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup replay_timeline Replay timeline
 * @ingroup gameplays_core
 * @brief A move log (moves.h) played back over the puzzle's givens.
 *
 * The board at step k is the givens plus moves 0..k-1, so any point can be shown at once: step
 * back and forth, jump to the start or end, or drag the slider. Playing follows the game clock
 * (pauses longer than RP_MAX_GAP_MS are shortened), at 1x, 2x, 4x or 8x.
 * @{
 */

#define RP_MAX_GAP_MS 2000                /**< a longer pause between two moves plays as this long */

/**
 * @brief Loads a log: the puzzle's givens and board, the moves and when each plays.
 * @param[in] given  the puzzle's givens (81 cells, copied)
 * @param[in] geo    its board (kept: it must stay valid while the log is played)
 * @param[in] moves  the moves (copied)
 * @param[in] n      how many (at most GAME_MOVES_MAX are kept)
 * @return false if out of memory
 */
bool replay_prepare(const uint8_t given[81], const sudoku_geom_t *geo, const game_move_t *moves, int n);
/**
 * @brief Goes to a step.
 * @param[in] k             0 .. replay_n() (clamped)
 * @param[in] keep_playing  false: stops
 */
void replay_seek(int k, bool keep_playing);
/**
 * @brief Plays on, by the time since the last call (at the speed chosen).
 * @param[in] dt  milliseconds
 * @return true if the board changed or playing stopped (draw it)
 */
bool replay_tick(uint32_t dt);
/** @brief Play or pause (at the end: plays from the start). */
void replay_toggle(void);
/** @brief The next speed: 1x, 2x, 4x, 8x, round. */
void replay_next_speed(void);
/**
 * @brief The speed.
 * @return 1, 2, 4 or 8
 */
int replay_speed_x(void);
/**
 * @brief Sets the step and whether it plays, as they are (a video's frame).
 * @param[in] k        the step
 * @param[in] playing  whether it plays
 */
void replay_set(int k, bool playing);
/**
 * @brief The step shown.
 * @return 0 .. replay_n()
 */
int replay_k(void);
/**
 * @brief The moves in the log.
 * @return how many
 */
int replay_n(void);
/**
 * @brief Whether it plays.
 * @return true: playing
 */
bool is_replay_playing(void);
/**
 * @brief The game clock at a step: when move k-1 was made.
 * @param[in] k  the step (0: 0)
 * @return milliseconds
 */
uint32_t replay_ms_at(int k);
/**
 * @brief When a step plays on the replay's timeline (pauses shortened).
 * @param[in] k  the step (0: 0)
 * @return milliseconds
 */
uint32_t replay_vt_at(int k);
/**
 * @brief The puzzle's board.
 * @return it
 */
const sudoku_geom_t *replay_geom(void);
/**
 * @brief Whether a cell is a given.
 * @param[in] i  the cell
 * @return its given digit, 0 if none
 */
uint8_t replay_given(int i);
/**
 * @brief The board at the step shown: each cell's digit and how it was put there, which break a
 *        rule, and the last move's cell.
 * @param[out] val   gets each cell's digit (0: empty)
 * @param[out] kind  gets each cell's MOVE_HAND / MOVE_FILL
 * @param[out] bad   gets whether each cell's digit (not a given) breaks a rule
 * @return the last move's cell, -1 at the start
 */
int replay_board(uint8_t val[81], uint8_t kind[81], bool bad[81]);

/** @} */

#ifdef __cplusplus
}
#endif
