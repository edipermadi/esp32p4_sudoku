/**
 * @file store_internal.h
 * @brief What store.cpp, store_stats.cpp and store_game.cpp share; the rest of the firmware uses store.h.
 */
#pragma once
#include "store.h"

/**
 * @brief The settings, and the game in progress in them (store.cpp).
 * @return them
 */
store_prefs_t *store_p(void);
/** @brief The statistics emptied, then read from the old NVS keys if there (store_stats.cpp). */
void store_stats_init(void);
/** @brief The game in progress read from the old NVS key if there (store_game.cpp). */
void store_game_init(void);
/** @brief The move log of the game in progress forgotten (a factory reset; store_game.cpp). */
void store_game_forget_moves(void);
