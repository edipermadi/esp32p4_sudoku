/**
 * @file trophies_results.c
 * @ingroup trophies_results
 * @brief sudoku.db's answers for the Trophies screen, taken into buffers of their own (PSRAM).
 */
#include "trophies_results.h"
#include "trophies_view.h"
#include <string.h>
#include "esp_heap_caps.h"

static stats_achievements_t *s_ach;      /**< the achievements taken last (PSRAM: internal RAM is tight) */
static stats_board_t *s_board;           /**< the leaderboard taken last (PSRAM) */
static stats_records_t *s_records;       /**< the records taken last (PSRAM) */
static stats_week_t *s_week;             /**< the week taken last (PSRAM) */

/* (documented in trophies_results.h) */
bool trophies_bufs(void)
{
    if (!s_ach) {
        s_ach = heap_caps_malloc(sizeof(*s_ach), MALLOC_CAP_SPIRAM);
    }
    if (!s_board) {
        s_board = heap_caps_malloc(sizeof(*s_board), MALLOC_CAP_SPIRAM);
    }
    return s_ach && s_board;
}

/* (documented in trophies_results.h) */
const stats_achievements_t *trophies_take_achievements(void)
{
    return s_ach && stats_db_take_achievements(s_ach) ? s_ach : NULL;
}

/* (documented in trophies_results.h) */
const stats_board_t *trophies_take_board(void)
{
    return s_board && stats_db_take_board(s_board) && !strcmp(s_board->day, trophies_day_shown()) ? s_board : NULL;
}

/* (documented in trophies_results.h) */
const stats_records_t *trophies_take_records(void)
{
    if (!s_records) {
        s_records = heap_caps_malloc(sizeof(*s_records), MALLOC_CAP_SPIRAM);
    }
    return s_records && stats_db_take_records(s_records) ? s_records : NULL;
}

/* (documented in trophies_results.h) */
const stats_week_t *trophies_take_week(void)
{
    if (!s_week) {
        s_week = heap_caps_malloc(sizeof(*s_week), MALLOC_CAP_SPIRAM);
    }
    return s_week && stats_db_take_week(s_week) && !strcmp(s_week->monday, trophies_week_shown()) ? s_week : NULL;
}
