/**
 * @file ui_replay_part.h
 * @ingroup ui_replay
 * @brief What the Replay screen and the MP4 export share. Only they include it.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>
#include "moves.h"
#include "stats_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ui_replay Gameplays and Replay screens
 * @ingroup topic_gameplays
 * @brief Gameplays (recorded solves, a page at a time), the Replay screen (a move log played back)
 *        and a replay exported as an MP4.
 *
 * | File                 | What's in it |
 * |----------------------|--------------|
 * | ui_gameplays.c       | Gameplays: the pages of recorded solves, Newer / Older, a row's replay and MP4 buttons |
 * | ui_replay.c          | the Replay screen: the board at any step, start / back / play / forward / end, the speed, the slider |
 * | ui_replay_export.c   | an MP4 export: the Replay screen on an invisible display, frame by frame, with a progress dialog |
 *
 * What they show and do, apart from drawing it: @ref gameplays_core.
 * @{
 */

/**
 * @brief Loads a log for a puzzle into the timeline (replay_timeline.h): its givens and board (a
 *        Jigsaw's regions kept here).
 * @param[in] id     the puzzle
 * @param[in] moves  the log
 * @param[in] n      its length
 * @return false if the puzzle can't be loaded, or out of memory
 */
bool replay_screen_load(uint16_t id, const game_move_t *moves, int n);
/**
 * @brief Builds the Replay screen (on the default display) for the log loaded.
 * @param[in] id      the puzzle
 * @param[in] sub     the line under the title
 * @param[in] player  whose game (their avatar goes first)
 * @return the screen
 */
lv_obj_t *replay_screen_build(uint16_t id, const char *sub, const char *player);
/** @brief Draws the step shown: the board (the last move highlighted, rule breaks red), the move and time, the slider, play / pause. */
void replay_screen_render(void);
/**
 * @brief The Replay screen's camera button (hidden in exported videos).
 * @return it
 */
lv_obj_t *replay_screen_camera(void);
/**
 * @brief Exports a solve as an MP4 in replays/ on the card, with a progress dialog.
 * @param[in] r  the solve with its moves
 */
void replay_export_start(const stats_replay_t *r);

/** @} */

#ifdef __cplusplus
}
#endif
