/**
 * @file trophies_core.h
 * @ingroup trophies_core
 * @brief The Trophies screen's data, without LVGL: one header for its parts.
 */
#pragma once
#include "trophies_dates.h"
#include "trophies_view.h"
#include "trophies_results.h"
#include "trophies_text.h"

/**
 * @defgroup trophies_core Trophies core
 * @ingroup topic_trophies
 * @brief What the Trophies screen (ui_trophies.c) shows, apart from drawing it: a .c / .h per part.
 *
 * | Files                   | Part |
 * |-------------------------|------|
 * | trophies_dates.c / .h   | @ref trophies_dates "Dates": a day moved, this week's Monday |
 * | trophies_view.c / .h    | @ref trophies_view "Day and week shown": moving them, asking sudoku.db |
 * | trophies_results.c / .h | @ref trophies_results "Results": sudoku.db's answers, taken |
 * | trophies_text.c / .h    | @ref trophies_text "Texts": an achievement's progress, the unlock toast |
 *
 * Include trophies_core.h for all of them.
 *
 * The screen asks (trophies_ask_week(), trophies_ask_board(), the step functions, or
 * stats_db_query_achievements() / stats_db_query_records() directly), then polls the take
 * functions and draws what they return. A day or week answer that isn't the one shown any more is
 * dropped.
 */
