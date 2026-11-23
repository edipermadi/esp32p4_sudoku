/**
 * @file store_game.cpp
 * @brief The game in progress kept in RAM (its copy in sudoku.db) and its move log, read from the old NVS key
 *        once.
 */
#include "store.h"
#include "store_internal.h"
#include <Preferences.h>
#include <string.h>
#include <stddef.h>
#include "stats_db.h"
#include "puzzles.h"
#include "esp_heap_caps.h"

#define s_game (store_p()->game)        /**< the game in progress (in the settings: store.cpp) */
static game_move_t *s_moves;          /**< move log of the game in progress, PSRAM, GAME_MOVES_MAX */
static int s_nmoves;                    /**< moves in s_moves */

/* (documented in store_internal.h) */
void store_game_init(void)
{
    Preferences prefs;
    if (!prefs.begin("sudoku", true)) {
        return;    /* read only; absent on a fresh board */
    }
    size_t len = prefs.getBytesLength("game");     /* the two layouts NVS ever held */
    if (len == offsetof(saved_game_t, started_at) || len == offsetof(saved_game_t, mistakes)) {
        prefs.getBytes("game", &s_game, len);
        if (s_game.id >= PUZZLE_COUNT) {
            s_game.id = STORE_NO_GAME;
        }
    }
    prefs.end();
}

/* (documented in store_internal.h) */
void store_game_forget_moves(void)
{
    s_nmoves = 0;
}

/* (documented in store.h) */
const saved_game_t *store_saved_game(void)
{
    return &s_game;
}

/* (documented in store.h) */
void store_set_game(const saved_game_t *g)
{
    s_game = *g;
}

/**
 * @brief The move log's buffer, made on first use.
 * @return false: no memory
 */
static bool moves_buf(void)
{
    if (!s_moves) {
        s_moves = (game_move_t *)heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    }
    return s_moves != NULL;
}

/* (documented in store.h) */
void store_set_game_moves(const game_move_t *m, int n)
{
    if (!moves_buf()) {
        return;
    }
    if (n > GAME_MOVES_MAX) {
        n = GAME_MOVES_MAX;
    }
    if (n > 0) {
        memcpy(s_moves, m, sizeof(game_move_t) * n);
    }
    s_nmoves = n > 0 ? n : 0;
}

/* (documented in store.h) */
int store_game_moves(const game_move_t **out)
{
    *out = s_moves;
    return s_moves ? s_nmoves : 0;
}

/* (documented in store.h) */
void store_load_game_moves(void)
{
    s_nmoves = 0;
    if (s_game.id == STORE_NO_GAME || !moves_buf()) {
        return;
    }
    uint16_t puzzle = STORE_NO_GAME;
    int n = stats_db_copy_saved_moves(&puzzle, s_moves, GAME_MOVES_MAX);
    if (n > 0 && puzzle == s_game.id) {
        s_nmoves = n;
    }
}

/* (documented in store.h) */
void store_commit_game(void)
{
    if (s_game.id != STORE_NO_GAME) {
        stats_db_save_game(&s_game, s_moves, s_moves ? s_nmoves : 0);
    }
}

/* (documented in store.h) */
void store_clear_game(void)
{
    s_nmoves = 0;
    if (s_game.id == STORE_NO_GAME) {
        return;
    }
    uint16_t id = s_game.id;
    s_game.id = STORE_NO_GAME;
    stats_db_clear_game(id);                  /* the other unfinished games stay in sudoku.db */
}
