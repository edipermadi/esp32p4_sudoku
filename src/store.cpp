#include "store.h"
#include <Preferences.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include "stats_db.h"
#include "puzzles.h"

/* Legacy NVS layout, namespace "sudoku" (read once at start-up, never written):
 *   "best" : uint16_t[PUZZLE_COUNT] best time in seconds (0 = unsolved, capped at 65535)
 *   "stat" : store_stat_t: fewest mistakes + times solved per puzzle
 *   "game" : saved_game_t (older saves lack the trailing "mistakes")
 *   "coll" : uint8_t last collection
 *   "snd"  : bool sound effects on
 *   "dark" : bool dark colour scheme
 *   "sleep": uint8_t screen timeout in minutes (0 = never)
 *   "tz"   : string, IANA time zone name
 * All of it now lives in sudoku.db (stats_db.c); Wi-Fi keeps its own NVS
 * namespace (wifi_mgr.cpp). */
static uint16_t s_best[PUZZLE_COUNT];
typedef struct {
    uint8_t fewest[PUZZLE_COUNT];   /* STORE_MISTAKES_UNKNOWN = not known */
    uint8_t solves[PUZZLE_COUNT];
} store_stat_t;
static store_stat_t s_stat;
static store_prefs_t s_p = {
    .game = {.id = STORE_NO_GAME},
    .last_collection = 0,
    .dark_mode = true,
    .sound = true,
    .screen_timeout_min = 5,
    .timezone = "UTC",
    .player_id = 0,
    .player_name = "Player 1",
};
#define s_game (s_p.game)

void store_init(void)
{
    memset(s_best, 0, sizeof(s_best));
    memset(s_stat.fewest, STORE_MISTAKES_UNKNOWN, sizeof(s_stat.fewest));
    memset(s_stat.solves, 0, sizeof(s_stat.solves));
    Preferences prefs;
    if (!prefs.begin("sudoku", true)) return;      /* read only; absent on a fresh board */
    if (prefs.getBytesLength("best") == sizeof(s_best)) {
        prefs.getBytes("best", s_best, sizeof(s_best));
    }
    if (prefs.getBytesLength("stat") == sizeof(s_stat)) {
        prefs.getBytes("stat", &s_stat, sizeof(s_stat));
    } else {
        for (int i = 0; i < PUZZLE_COUNT; i++) s_stat.solves[i] = s_best[i] ? 1 : 0;
    }
    size_t len = prefs.getBytesLength("game");     /* the two layouts NVS ever held */
    if (len == offsetof(saved_game_t, started_at) || len == offsetof(saved_game_t, mistakes)) {
        prefs.getBytes("game", &s_game, len);
        if (s_game.id >= PUZZLE_COUNT) s_game.id = STORE_NO_GAME;
    }
    uint8_t c = prefs.getUChar("coll", 0);
    s_p.last_collection = c < PUZZLE_COLLECTION_COUNT ? c : 0;
    s_p.dark_mode = prefs.getBool("dark", true);
    s_p.sound = prefs.getBool("snd", true);
    s_p.screen_timeout_min = prefs.getUChar("sleep", 5);
    if (!prefs.getString("tz", s_p.timezone, sizeof(s_p.timezone))) strcpy(s_p.timezone, "UTC");
    prefs.end();
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
    if (s_best[id] != 0 && s_best[id] <= seconds) return false;
    s_best[id] = (uint16_t)seconds;
    return true;
}

void store_merge_stats(const uint16_t *best, const uint8_t *fewest, const uint8_t *solves)
{
    for (int i = 0; i < PUZZLE_COUNT; i++) {
        if (!best[i]) continue;
        if (!s_best[i] || best[i] < s_best[i]) s_best[i] = best[i];
        if (fewest[i] != STORE_MISTAKES_UNKNOWN &&
            (s_stat.fewest[i] == STORE_MISTAKES_UNKNOWN || fewest[i] < s_stat.fewest[i])) {
            s_stat.fewest[i] = fewest[i];
        }
        if (solves[i] > s_stat.solves[i]) s_stat.solves[i] = solves[i];
    }
}

void store_reset_stats(void)
{
    memset(s_best, 0, sizeof(s_best));
    memset(s_stat.fewest, STORE_MISTAKES_UNKNOWN, sizeof(s_stat.fewest));
    memset(s_stat.solves, 0, sizeof(s_stat.solves));
}

void store_copy_stats(uint16_t *best, uint8_t *fewest, uint8_t *solves)
{
    memcpy(best, s_best, sizeof(s_best));
    memcpy(fewest, s_stat.fewest, sizeof(s_stat.fewest));
    memcpy(solves, s_stat.solves, sizeof(s_stat.solves));
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
    stats_db_save_game(s_game.id == STORE_NO_GAME ? NULL : &s_game);
}

void store_clear_game(void)
{
    if (s_game.id == STORE_NO_GAME) return;
    s_game.id = STORE_NO_GAME;
    stats_db_save_game(NULL);
}

void store_get_prefs(store_prefs_t *out)
{
    *out = s_p;
}

void store_apply_prefs(const store_prefs_t *in)
{
    s_p = *in;
    if (s_game.id >= PUZZLE_COUNT) s_game.id = STORE_NO_GAME;
    if (s_p.last_collection >= PUZZLE_COLLECTION_COUNT) s_p.last_collection = 0;
}

int32_t store_player_id(void)
{
    return s_p.player_id;
}

const char *store_player_name(void)
{
    return s_p.player_name;
}

void store_set_player(int32_t id, const char *name)
{
    s_p.player_id = id;
    snprintf(s_p.player_name, sizeof(s_p.player_name), "%s", name);
}

uint8_t store_last_collection(void)
{
    return s_p.last_collection;
}

void store_set_last_collection(uint8_t c)
{
    if (c >= PUZZLE_COLLECTION_COUNT || c == s_p.last_collection) return;
    s_p.last_collection = c;
    stats_db_set_text("last_level", puzzle_collections[c].name);
}

bool store_dark_mode(void)
{
    return s_p.dark_mode;
}

void store_set_dark_mode(bool dark)
{
    s_p.dark_mode = dark;
    stats_db_set_int("dark_mode", dark);
}

bool store_sound_enabled(void)
{
    return s_p.sound;
}

void store_set_sound_enabled(bool on)
{
    s_p.sound = on;
    stats_db_set_int("sound", on);
}

const char *store_timezone(void)
{
    return s_p.timezone;
}

void store_set_timezone(const char *name)
{
    snprintf(s_p.timezone, sizeof(s_p.timezone), "%s", name);
    stats_db_set_text("time_zone", s_p.timezone);
}

uint8_t store_screen_timeout_min(void)
{
    return s_p.screen_timeout_min;
}

void store_set_screen_timeout_min(uint8_t min)
{
    s_p.screen_timeout_min = min;
    stats_db_set_int("screen_off_minutes", min);
}
