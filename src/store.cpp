#include "store.h"
#include <Preferences.h>
#include <string.h>
#include "puzzles.h"

/* Key layout in namespace "sudoku":
 *   "best" : uint16_t[PUZZLE_COUNT] best time in seconds (0 = unsolved, capped at 65535)
 *   "game" : saved_game_t
 *   "coll" : uint8_t last collection */
static Preferences s_prefs;
static uint16_t s_best[PUZZLE_COUNT];
static saved_game_t s_game;

void store_init(void)
{
    s_prefs.begin("sudoku", false);
    memset(s_best, 0, sizeof(s_best));
    if (s_prefs.getBytesLength("best") == sizeof(s_best)) {
        s_prefs.getBytes("best", s_best, sizeof(s_best));
    }
    s_game.id = STORE_NO_GAME;
    if (s_prefs.getBytesLength("game") == sizeof(s_game)) {
        s_prefs.getBytes("game", &s_game, sizeof(s_game));
        if (s_game.id >= PUZZLE_COUNT) s_game.id = STORE_NO_GAME;
    }
}

uint32_t store_best_time(uint16_t id)
{
    return id < PUZZLE_COUNT ? s_best[id] : 0;
}

bool store_record_solve(uint16_t id, uint32_t seconds)
{
    if (id >= PUZZLE_COUNT) return false;
    if (seconds == 0) seconds = 1;           /* 0 means "unsolved" */
    if (seconds > 0xFFFF) seconds = 0xFFFF;
    if (s_best[id] != 0 && s_best[id] <= seconds) return false;
    s_best[id] = (uint16_t)seconds;
    s_prefs.putBytes("best", s_best, sizeof(s_best));
    return true;
}

uint16_t store_solved_count(uint16_t first, uint16_t count)
{
    uint16_t n = 0;
    for (uint16_t i = first; i < first + count && i < PUZZLE_COUNT; i++) {
        if (s_best[i]) n++;
    }
    return n;
}

const saved_game_t *store_saved_game(void)
{
    return &s_game;
}

void store_save_game(const saved_game_t *g)
{
    s_game = *g;
    s_prefs.putBytes("game", &s_game, sizeof(s_game));
}

void store_clear_game(void)
{
    if (s_game.id == STORE_NO_GAME) return;
    s_game.id = STORE_NO_GAME;
    s_prefs.remove("game");
}

uint8_t store_last_collection(void)
{
    uint8_t c = s_prefs.getUChar("coll", 0);
    return c < PUZZLE_COLLECTION_COUNT ? c : 0;
}

void store_set_last_collection(uint8_t c)
{
    if (c != s_prefs.getUChar("coll", 0xFF)) s_prefs.putUChar("coll", c);
}
