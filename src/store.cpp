#include "store.h"
#include <Preferences.h>
#include <string.h>
#include <stddef.h>
#include "puzzles.h"

/* Key layout in namespace "sudoku":
 *   "best" : uint16_t[PUZZLE_COUNT] best time in seconds (0 = unsolved, capped at 65535)
 *   "stat" : store_stat_t: fewest mistakes + times solved per puzzle
 *   "game" : saved_game_t (older saves lack the trailing "mistakes")
 *   "coll" : uint8_t last collection
 *   "snd"  : bool sound effects on
 *   "dark" : bool dark colour scheme
 *   "sleep": uint8_t screen timeout in minutes (0 = never)
 *   "tz"   : string, IANA time zone name
 * (Wi-Fi keeps its network and on/off switch in its own namespace, see wifi_mgr.cpp) */
static Preferences s_prefs;
static uint16_t s_best[PUZZLE_COUNT];
typedef struct {
    uint8_t fewest[PUZZLE_COUNT];   /* STORE_MISTAKES_UNKNOWN = not known */
    uint8_t solves[PUZZLE_COUNT];
} store_stat_t;
static store_stat_t s_stat;
static saved_game_t s_game;

void store_init(void)
{
    s_prefs.begin("sudoku", false);
    memset(s_best, 0, sizeof(s_best));
    if (s_prefs.getBytesLength("best") == sizeof(s_best)) {
        s_prefs.getBytes("best", s_best, sizeof(s_best));
    }
    if (s_prefs.getBytesLength("stat") == sizeof(s_stat)) {
        s_prefs.getBytes("stat", &s_stat, sizeof(s_stat));
    } else {
        /* first start with statistics: earlier solves count once, mistakes unknown */
        memset(s_stat.fewest, STORE_MISTAKES_UNKNOWN, sizeof(s_stat.fewest));
        for (int i = 0; i < PUZZLE_COUNT; i++) s_stat.solves[i] = s_best[i] ? 1 : 0;
    }
    memset(&s_game, 0, sizeof(s_game));
    s_game.id = STORE_NO_GAME;
    size_t len = s_prefs.getBytesLength("game");
    if (len == sizeof(s_game) || len == offsetof(saved_game_t, mistakes)) {
        s_prefs.getBytes("game", &s_game, len);
        if (s_game.id >= PUZZLE_COUNT) s_game.id = STORE_NO_GAME;
    }
}

uint32_t store_best_time(uint16_t id)
{
    return id < PUZZLE_COUNT ? s_best[id] : 0;
}

bool store_record_solve(uint16_t id, uint32_t seconds, uint32_t mistakes)
{
    if (id >= PUZZLE_COUNT) return false;
    if (seconds == 0) seconds = 1;           /* 0 means "unsolved" */
    if (seconds > 0xFFFF) seconds = 0xFFFF;
    if (mistakes > 254) mistakes = 254;

    if (s_stat.solves[id] < 255) s_stat.solves[id]++;
    if (s_stat.fewest[id] == STORE_MISTAKES_UNKNOWN || mistakes < s_stat.fewest[id]) {
        s_stat.fewest[id] = (uint8_t)mistakes;
    }
    s_prefs.putBytes("stat", &s_stat, sizeof(s_stat));

    if (s_best[id] != 0 && s_best[id] <= seconds) return false;
    s_best[id] = (uint16_t)seconds;
    s_prefs.putBytes("best", s_best, sizeof(s_best));
    return true;
}

uint8_t store_fewest_mistakes(uint16_t id)
{
    return id < PUZZLE_COUNT ? s_stat.fewest[id] : STORE_MISTAKES_UNKNOWN;
}

uint8_t store_solve_count(uint16_t id)
{
    return id < PUZZLE_COUNT ? s_stat.solves[id] : 0;
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

void store_set_game(const saved_game_t *g)
{
    s_game = *g;
}

void store_commit_game(void)
{
    if (s_game.id == STORE_NO_GAME) s_prefs.remove("game");
    else s_prefs.putBytes("game", &s_game, sizeof(s_game));
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

bool store_dark_mode(void)
{
    return s_prefs.getBool("dark", true);
}

void store_set_dark_mode(bool dark)
{
    s_prefs.putBool("dark", dark);
}

bool store_sound_enabled(void)
{
    return s_prefs.getBool("snd", true);
}

void store_set_sound_enabled(bool on)
{
    s_prefs.putBool("snd", on);
}

const char *store_timezone(void)
{
    static char tz[48];
    if (!s_prefs.getString("tz", tz, sizeof(tz))) strcpy(tz, "UTC");
    return tz;
}

void store_set_timezone(const char *name)
{
    s_prefs.putString("tz", name);
}

uint8_t store_screen_timeout_min(void)
{
    return s_prefs.getUChar("sleep", 5);
}

void store_set_screen_timeout_min(uint8_t min)
{
    if (min != s_prefs.getUChar("sleep", 0xFF)) s_prefs.putUChar("sleep", min);
}
