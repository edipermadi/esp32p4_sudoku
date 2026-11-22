/**
 * @file ui_continue_part.h
 * @ingroup ui_continue
 * @brief Continue's files: what they share. Only they include it.
 *
 * The rest of the UI reaches them through ui_internal.h (continue_cb(), continue_line_update(), open_puzzle());
 * the unfinished games themselves come from continue_core.h.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ui_continue Continue
 * @ingroup topic_continue
 * @brief Getting back to an unfinished game: Home's Continue, the Unfinished games dialog, and opening a puzzle
 *        while another is in progress.
 *
 * | File                  | What's in it |
 * |-----------------------|--------------|
 * | ui_continue.c         | Home's Continue button: its line (the game played last, how many more), its tap; a game resumed |
 * | ui_continue_games.c   | the Unfinished games dialog (Continue with more than one): each game, to resume or discard |
 * | ui_continue_open.c    | open_puzzle(): the game in progress, an unfinished one resumed, or a new start; without a card, the "Unfinished game" dialog first |
 * @{
 */

/**
 * @brief Resumes one of the current player's unfinished games on the game screen (continue_load(), then
 *        start_game()) (ui_continue.c).
 * @param[in] id  the puzzle
 * @return false if it isn't there any more
 */
bool continue_resume(uint16_t id);
/** @brief Opens the Unfinished games dialog, each game to resume or discard (ui_continue_games.c). */
void continue_games_open(void);

/** @} */

#ifdef __cplusplus
}
#endif
