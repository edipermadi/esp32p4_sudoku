/**
 * @file players_core.c
 * @ingroup players_core
 * @brief The Players screen's data: the player list, switching, colours, avatars, adding, renaming,
 *        deleting; the avatar picker's rows.
 */
#include "players_core.h"
#include "avatars.h"
#include "store.h"
#include <string.h>

static stats_player_t p_players[STATS_MAX_PLAYERS];   /**< the last list from sudoku.db */
static int p_count = -1;                 /**< its players, -1 = none yet */
static int32_t p_delete_id;              /**< the player a Delete asked about (0: none) */
static int av_cols;                      /**< emoji per row in the picker */
static int av_row0[AVATAR_GROUPS + 1];   /**< each group starts a row: its first; [n] = all rows */

/* (documented in players_core.h) */
void players_take_list(const stats_player_t *list, int n)
{
    memcpy(p_players, list, sizeof(stats_player_t) * n);
    p_count = n;
}

/* (documented in players_core.h) */
const stats_player_t *players_all(void)
{
    return p_players;
}

/* (documented in players_core.h) */
int players_count(void)
{
    return p_count;
}

/* (documented in players_core.h) */
void players_set_count(int v)
{
    p_count = v;
}

/**
 * @brief Who plays now, in the list (to change).
 * @return them, or NULL if they aren't in it yet
 */
static stats_player_t *current(void)
{
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == store_player_id()) {
            return &p_players[i];
        }
    }
    return NULL;
}

/* (documented in players_core.h) */
const stats_player_t *players_current(void)
{
    return current();
}

/* (documented in players_core.h) */
void players_switch(int32_t id)
{
    if (id && id != store_player_id()) {
        stats_db_switch_player(id);
    }
}

/* (documented in players_core.h) */
void players_next_color(int32_t id)
{
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == id) {
            stats_db_set_player_color(id, p_players[i].color + 1);   /* the list comes back with it */
            p_players[i].color = (uint8_t)((p_players[i].color + 1) % STATS_PLAYER_COLORS);
        }
    }
}

/* (documented in players_core.h) */
bool players_set_avatar(int k)
{
    stats_player_t *pl = current();
    if (!pl) {
        return false;
    }
    stats_db_set_player_avatar(pl->id, k);           /* the list comes back with it */
    pl->avatar = (uint16_t)k;
    return true;
}

/* (documented in players_core.h) */
void players_name_entered(int32_t rename_id, const char *name)
{
    if (rename_id) {
        stats_db_rename_player(rename_id, name);
    } else {
        stats_db_add_player(name);
    }
}

/* (documented in players_core.h) */
const stats_player_t *players_delete_ask(void)
{
    int32_t id = store_player_id();
    const stats_player_t *pl = NULL;
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == id) {
            pl = &p_players[i];
        }
    }
    if (pl) {
        p_delete_id = id;
    }
    return pl;
}

/* (documented in players_core.h) */
void players_delete_confirmed(void)
{
    if (p_delete_id) {
        stats_db_delete_player(p_delete_id);
    }
    p_delete_id = 0;
}

/**
 * @brief The end of a group: the first emoji after it.
 * @param[in] g  the group
 * @return the avatar, from 0
 */
static int av_group_end(int g)
{
    return g + 1 < AVATAR_GROUPS ? avatar_group_first(g + 1) : AVATAR_COUNT;
}

/* (documented in players_core.h) */
void players_av_layout(int cols)
{
    av_cols = cols;
    for (int g = 0, r = 0; g <= AVATAR_GROUPS; g++) {
        av_row0[g] = r;
        if (g < AVATAR_GROUPS) {
            r += (av_group_end(g) - avatar_group_first(g) + cols - 1) / cols;
        }
    }
}

/* (documented in players_core.h) */
int players_av_rows(void)
{
    return av_row0[AVATAR_GROUPS];
}

/* (documented in players_core.h) */
int players_av_row_first(int g)
{
    return av_row0[g];
}

/* (documented in players_core.h) */
int players_av_row_group(int row)
{
    int g = 0;
    while (g + 1 < AVATAR_GROUPS && av_row0[g + 1] <= row) {
        g++;
    }
    return g;
}

/* (documented in players_core.h) */
int players_av_at(int row, int col)
{
    if (col < 0 || col >= av_cols || row < 0 || row >= av_row0[AVATAR_GROUPS]) {
        return -1;
    }
    int g = players_av_row_group(row), k = avatar_group_first(g) + (row - av_row0[g]) * av_cols + col;
    return k < av_group_end(g) ? k : -1;    /* (not the empty end of a group's last row) */
}

/* (documented in players_core.h) */
int players_av_row_of(int k)
{
    int g = 0;
    while (g + 1 < AVATAR_GROUPS && avatar_group_first(g + 1) <= k) {
        g++;
    }
    return av_row0[g] + (k - avatar_group_first(g)) / av_cols;
}
