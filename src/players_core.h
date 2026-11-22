/**
 * @file players_core.h
 * @ingroup players_core
 * @brief The Players screen's data, without LVGL calls (the screen: ui_players.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "stats_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup players_core Players core
 * @ingroup topic_players
 * @brief What the Players screen shows and does, apart from drawing it: the player list from
 *        sudoku.db, who plays now, switching, colours, avatars, adding, renaming, deleting; and the
 *        avatar picker's rows (each group of emoji starts a row).
 * @{
 */

/** @name The players
 *  @{ */
/**
 * @brief Takes the player list sudoku.db sent.
 * @param[in] list  the players
 * @param[in] n     how many
 */
void players_take_list(const stats_player_t *list, int n);
/**
 * @brief The player list.
 * @return it (players_count() of them)
 */
const stats_player_t *players_all(void);
/**
 * @brief The players in the list.
 * @return how many; -1 if none came from sudoku.db yet
 */
int players_count(void);
/**
 * @brief Sets how many players the list has (-1: none yet, after the database changes).
 * @param[in] v  how many
 */
void players_set_count(int v);
/**
 * @brief Who plays now, in the list.
 * @return them, or NULL if they aren't in it yet
 */
const stats_player_t *players_current(void);
/**
 * @brief Switches to a player (not to who plays now).
 * @param[in] id  players.id (0: nobody)
 */
void players_switch(int32_t id);
/**
 * @brief A player's colour: the next one round.
 * @param[in] id  players.id
 */
void players_next_color(int32_t id);
/**
 * @brief Who plays now gets an avatar.
 * @param[in] k  1..AVATAR_COUNT (avatars.h), 0 = none
 * @return false if who plays now isn't in the list
 */
bool players_set_avatar(int k);
/**
 * @brief A name typed in: a new player, or a player renamed.
 * @param[in] rename_id  the player to rename (0: add one)
 * @param[in] name       the name
 */
void players_name_entered(int32_t rename_id, const char *name);
/**
 * @brief Delete was asked for: who plays now, remembered until the answer.
 * @return them, or NULL if they aren't in the list
 */
const stats_player_t *players_delete_ask(void);
/** @brief The delete was confirmed: the player remembered is deleted (another takes over). */
void players_delete_confirmed(void);
/** @} */

/** @name The avatar picker's rows
 *  @{ */
/**
 * @brief Lays the emoji out in rows, each group starting a row.
 * @param[in] cols  emoji per row
 */
void players_av_layout(int cols);
/**
 * @brief The rows in all.
 * @return how many
 */
int players_av_rows(void);
/**
 * @brief A group's first row.
 * @param[in] g  0..AVATAR_GROUPS-1
 * @return the row
 */
int players_av_row_first(int g);
/**
 * @brief The group of a row.
 * @param[in] row  the row
 * @return the group
 */
int players_av_row_group(int row);
/**
 * @brief The emoji at a place.
 * @param[in] row  the row
 * @param[in] col  the column
 * @return the avatar, from 0; -1: none (past the grid, or the empty end of a group's last row)
 */
int players_av_at(int row, int col);
/**
 * @brief The row an emoji is in.
 * @param[in] k  the avatar, from 0
 * @return the row
 */
int players_av_row_of(int k);
/** @} */

/** @} */

#ifdef __cplusplus
}
#endif
