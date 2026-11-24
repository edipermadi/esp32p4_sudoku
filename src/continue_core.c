/**
 * @file continue_core.c
 * @ingroup continue_core
 * @brief The current player's unfinished games: the list, the one played last, one loaded or discarded.
 */
#include "continue_core.h"
#include "store.h"
#include "generator.h"
#include "collections.h"

/** @brief the current player's unfinished games in sudoku.db (most recent first), read by continue_refresh() */
static stats_game_row_t s_games[STATS_MAX_GAMES];
static int s_ngames;                      /**< how many */

/* (documented in continue_core.h) */
void continue_refresh(void)
{
    int n = is_stats_db_ready() ? stats_db_list_games(s_games) : -1;
    s_ngames = n < 0 ? 0 : n;
}

/* (documented in continue_core.h) */
const stats_game_row_t *continue_games(void)
{
    return s_games;
}

/* (documented in continue_core.h) */
int continue_ngames(void)
{
    return s_ngames;
}

/* (documented in continue_core.h) */
bool continue_last(uint16_t *id, uint32_t *ms, int *others)
{
    const saved_game_t *s = store_saved_game();
    *id = s->id != STORE_NO_GAME ? s->id : (s_ngames ? s_games[0].puzzle : STORE_NO_GAME);
    *ms = s->id != STORE_NO_GAME ? s->elapsed_ms : (s_ngames ? s_games[0].elapsed_ms : 0);
    *others = 0;
    for (int i = 0; i < s_ngames; i++) {
        *others += s_games[i].puzzle != *id;
    }
    return *id != STORE_NO_GAME;
}

/* (documented in continue_core.h) */
bool is_continue_in_progress(uint16_t id)
{
    if (store_saved_game()->id == id) {
        return true;
    }
    for (int i = 0; i < s_ngames; i++) {
        if (s_games[i].puzzle == id) {
            return true;
        }
    }
    return false;
}

/* (documented in continue_core.h) */
bool continue_load(uint16_t id)
{
    if (store_saved_game()->id == id) {
        return true;                                /* (in RAM already) */
    }
    saved_game_t g;
    if (!is_stats_db_ready() || !stats_db_load_game(id, &g)) {
        return false;
    }
    store_set_game(&g);                             /* the one being played now */
    store_load_game_moves();
    return true;
}

/* (documented in continue_core.h) */
void continue_discard(uint16_t id)
{
    if (store_saved_game()->id == id) {
        store_clear_game();
    } else {
        stats_db_clear_game(id);
    }
}

/* (documented in continue_core.h) */
int continue_cells(uint16_t id)
{
    int var = VAR_CLASSIC;
    if (!puzzle_is_generated(id) || !gen_lookup(id, NULL, &var, NULL)) {
        return 81;
    }
    return var == VAR_MINI ? 36 : var == VAR_MINI8 ? 64 : var == VAR_JIG7 ? 49 : var == VAR_KIDS ? 16 : 81;
}
