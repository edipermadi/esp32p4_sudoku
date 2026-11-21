/**
 * @file puzzles_core.h
 * @ingroup puzzles_core
 * @brief The Puzzles screen's data, without LVGL: one header for its parts (the screen: ui_list.c).
 */
#pragma once
#include "puzzles_select.h"
#include "puzzles_solved.h"
#include "puzzles_starred.h"
#include "puzzles_colls.h"

/**
 * @defgroup puzzles_core Puzzles core
 * @brief What the Puzzles screen shows and does, apart from drawing it (the collections themselves:
 *        @ref collections).
 *
 * | Files                    | Part |
 * |--------------------------|------|
 * | puzzles_select.c / .h    | @ref puzzles_select "Selection": the tab, the collection, Starred |
 * | puzzles_solved.c / .h    | @ref puzzles_solved "Solved": counts per collection, the panel's numbers |
 * | puzzles_starred.c / .h   | @ref puzzles_starred "Starred": the bookmarks |
 * | puzzles_colls.c / .h     | @ref puzzles_colls "Collections' actions": delete, export, take a puzzle out |
 *
 * Include puzzles_core.h for all of them.
 */
