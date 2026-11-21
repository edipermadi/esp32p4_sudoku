/**
 * @file gameplays_list.h
 * @ingroup gameplays_list
 * @brief The Gameplays list's data: its pages of recorded solves, and opening one.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "stats_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup gameplays_list Gameplays list
 * @ingroup gameplays_core
 * @brief Every solve with a move log, newest first, a page of STATS_MAX_DAY_ROWS at a time from
 *        sudoku.db; a row opens its replay (or exports it as a video).
 * @{
 */

/**
 * @brief Asks sudoku.db for a page.
 * @param offset  its first row (\<0: 0)
 * @return false without sudoku.db (the list then has no rows)
 */
bool gameplays_ask(int offset);
/**
 * @brief The page sudoku.db answered with; the list's place and rows follow it.
 * @return it, or NULL if none arrived since the last call
 */
const stats_plays_t *gameplays_take(void);
/**
 * @brief The page shown's first row.
 * @return its place, 0..
 */
int gameplays_offset(void);
/**
 * @brief Whether there's a newer page (before this one).
 * @return true: there is
 */
bool is_gameplays_newer(void);
/**
 * @brief Whether there's an older page (after this one).
 * @return true: there is
 */
bool is_gameplays_older(void);
/**
 * @brief Opens a row's solve: its replay, or a video of it. Nothing while a video is being
 *        made, or for a row without a solve.
 * @param i         the row on the page shown
 * @param to_video  true: export it as an MP4 instead of showing it
 */
void gameplays_open(int i, bool to_video);

/** @} */

#ifdef __cplusplus
}
#endif
