/**
 * @file search_core.h
 * @ingroup search_core
 * @brief The Puzzles screen's search: does a puzzle match what was typed? Plain C, without LVGL (the PC tests
 *        build it).
 */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup search_core Search core
 * @ingroup topic_puzzles
 * @brief Matching a typed search against a puzzle's name and variant.
 * @{
 */

/**
 * @brief Does a puzzle match a search? Every word of the query (split at spaces) must appear somewhere in
 * the puzzle's words, ignoring case: "killer hard", "gnome 2 easy", "#14", "x-sudoku".
 * @param[in] query  what was typed
 * @param[in] text   the puzzle's words: its name, its variant, its level ("Gnome 2 Easy #14 Classic")
 * @return true if every word is there (false for a query without any)
 */
bool is_search_match(const char *query, const char *text);

/** @} */

#ifdef __cplusplus
}
#endif
