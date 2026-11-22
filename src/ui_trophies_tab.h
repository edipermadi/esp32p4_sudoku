/**
 * @file ui_trophies_tab.h
 * @ingroup ui_trophies
 * @brief The Trophies screen's files: what they share. Only they include it.
 *
 * The rest of the UI reaches the screen through ui_internal.h (trophy_cb(), trophy_poll(),
 * tr_ask(), build_trophies(), trophy_toast_poll()); the data comes from trophies_core.h.
 */
#pragma once
#include <stdbool.h>
#include <lvgl.h>
#include "stats_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ui_trophies Trophies screen
 * @ingroup topic_trophies
 * @brief Trophies (Home > Trophies): a tab bar over four tabs, a file per tab.
 *
 * | File                        | What's in it |
 * |-----------------------------|--------------|
 * | ui_trophies.c               | the screen around the tabs (title, tab bar; Daily and Weekly greyed out until the clock is set), asking for and drawing what the open tab shows, the "Achievement unlocked" toast, and the cards' look (tr_hint(), tr_card()) |
 * | ui_trophies_achievements.c  | Achievements: unlocked ones first, then the others with their progress |
 * | ui_trophies_daily.c         | Daily: a daily puzzle's leaderboard, a day at a time, and Play today's puzzle |
 * | ui_trophies_weekly.c        | Weekly: a week's daily puzzles added up per player |
 * | ui_trophies_records.c       | Records: your personal bests |
 *
 * A tab's file builds its pane, keeps its widgets to itself, asks sudoku.db for what it shows
 * (through trophies_core.h) and draws the answer, which ui_trophies.c's poll hands it.
 * @{
 */

/** @name What every tab uses (ui_trophies.c)
 *  @{ */
/**
 * @brief Replaces a list's contents with a line of text (loading, nothing yet, why not).
 * @param[in] list  the list
 * @param[in] text  the text
 */
void tr_hint(lv_obj_t *list, const char *text);
/**
 * @brief A card in a list: full width, the card colour, rounded, not scrollable.
 * @param[in] list  the list
 * @param[in] h     its height
 * @return the card
 */
lv_obj_t *tr_card(lv_obj_t *list, int h);
/** @} */

/** @name The tabs' interface
 *  build: its pane on the screen (the open tab's area), when the screen is built. ask: asks
 *  sudoku.db for what it shows (when it's opened, or the player changes). no_db: says why there's
 *  nothing (no sudoku.db). render: draws sudoku.db's answer.
 *  @{ */
/**
 * @brief Achievements: the count and the list.
 * @param[in] scr  the screen
 * @return its pane
 */
lv_obj_t *tr_ach_build(lv_obj_t *scr);
/** @brief Achievements: asks for the current player's. */
void tr_ach_ask(void);
/** @brief Achievements: without sudoku.db. */
void tr_ach_no_db(void);
/**
 * @brief Achievements: unlocked ones first, then the rest.
 * @param[in] a  the current player's achievements
 */
void tr_ach_render(const stats_achievements_t *a);

/**
 * @brief Daily: \< day \>, the leaderboard and Play today's puzzle.
 * @param[in] scr  the screen
 * @return its pane
 */
lv_obj_t *tr_daily_build(lv_obj_t *scr);
/** @brief Daily: asks for the day shown's leaderboard (without the clock: why not). */
void tr_daily_ask(void);
/** @brief Daily: without sudoku.db. */
void tr_daily_no_db(void);
/**
 * @brief Daily: the day, its puzzle and the leaderboard; Play today's puzzle if it's today and you
 *        haven't solved it.
 * @param[in] b  the day's leaderboard
 */
void tr_daily_render(const stats_board_t *b);

/**
 * @brief Weekly: \< week \> and the list.
 * @param[in] scr  the screen
 * @return its pane
 */
lv_obj_t *tr_weekly_build(lv_obj_t *scr);
/** @brief Weekly: asks for the week shown (without the clock: why not). */
void tr_weekly_ask(void);
/** @brief Weekly: without sudoku.db. */
void tr_weekly_no_db(void);
/**
 * @brief Weekly: the week and each player who solved a daily puzzle in it (a square per day).
 * @param[in] w  the week
 */
void tr_weekly_render(const stats_week_t *w);

/**
 * @brief Records: the list.
 * @param[in] scr  the screen
 * @return its pane
 */
lv_obj_t *tr_records_build(lv_obj_t *scr);
/** @brief Records: asks for the current player's. */
void tr_records_ask(void);
/** @brief Records: without sudoku.db. */
void tr_records_no_db(void);
/**
 * @brief Records: each with its puzzle and date.
 * @param[in] R  the current player's records
 */
void tr_records_render(const stats_records_t *R);
/** @} */

/** @} */

#ifdef __cplusplus
}
#endif
