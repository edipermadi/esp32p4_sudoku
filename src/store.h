#pragma once
/*
 * Game state the UI reads at once: statistics, the game in progress and the
 * settings. They are kept in sudoku.db on the microSD card (stats_db.h);
 * this is their RAM copy. Setters update RAM and queue the database write.
 *
 * Start-up: store_init() fills RAM from the NVS keys earlier versions wrote
 * (read only: nothing here writes NVS any more), then store_apply_prefs()
 * overrides that with what sudoku.db holds. Without a card, changes last
 * until power-off. (Wi-Fi keeps its network in NVS, see wifi_mgr.cpp.)
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORE_NO_GAME 0xFFFF
#define STORE_MISTAKES_UNKNOWN 0xFF   /* solved before mistakes were counted */

typedef struct {
    uint16_t id;               /* puzzle index, STORE_NO_GAME = none */
    uint8_t  val[81];          /* 0 = empty; givens included */
    uint16_t notes[81];        /* bit d (1..9) = pencil mark d */
    uint32_t elapsed_ms;
    uint16_t mistakes;         /* rule-breaking digits placed so far */
    int64_t  started_at;       /* Unix time the puzzle was opened (or restarted), 0 = unknown */
} saved_game_t;                /* (older NVS saves end before mistakes or started_at) */

void store_init(void);

/* Best time in seconds, 0 = never solved. */
uint32_t store_best_time(uint16_t id);
/* Records a solve in RAM (stats_db_record_solve() saves it); returns true if
 * it is a new best time (or the first solve). */
bool store_record_solve(uint16_t id, uint32_t seconds, uint32_t mistakes);
/* Merges per-puzzle statistics into the RAM copy (best: lower wins, fewest:
 * lower wins, solves: higher wins). Arrays of PUZZLE_COUNT; best 0 = unsolved. */
void store_merge_stats(const uint16_t *best, const uint8_t *fewest, const uint8_t *solves);
/* Copies the RAM statistics out (same layout). */
void store_copy_stats(uint16_t *best, uint8_t *fewest, uint8_t *solves);
/* Forgets all statistics in RAM (the card was formatted). */
void store_reset_stats(void);
/* Fewest mistakes over all solves (capped at 254), STORE_MISTAKES_UNKNOWN if
 * every solve predates mistake counting or the puzzle is unsolved. */
uint8_t store_fewest_mistakes(uint16_t id);
/* Number of times solved (capped at 255), 0 = never. */
uint8_t store_solve_count(uint16_t id);
/* Number of solved puzzles among [first, first+count). */
uint16_t store_solved_count(uint16_t first, uint16_t count);

/* Game in progress (one slot). */
const saved_game_t *store_saved_game(void);   /* id == STORE_NO_GAME if none */
/* Updates the in-memory copy only; store_commit_game() queues the save. */
void store_set_game(const saved_game_t *g);
void store_commit_game(void);
void store_clear_game(void);                  /* memory + queued delete */

/* Everything except statistics, as one block: what sudoku.db is seeded with
 * on first use, and what it hands back at start-up. */
#define STORE_TZ_LEN 48
typedef struct {
    saved_game_t game;                        /* game.id == STORE_NO_GAME: none */
    uint8_t last_collection;
    bool dark_mode, sound;
    uint8_t screen_timeout_min;
    char timezone[STORE_TZ_LEN];
} store_prefs_t;
void store_get_prefs(store_prefs_t *out);
void store_apply_prefs(const store_prefs_t *in);   /* RAM only */

uint8_t store_last_collection(void);
void store_set_last_collection(uint8_t c);

bool store_dark_mode(void);
void store_set_dark_mode(bool dark);

bool store_sound_enabled(void);
void store_set_sound_enabled(bool on);

/* IANA time zone name, e.g. "Asia/Dubai"; "UTC" until one is chosen. */
const char *store_timezone(void);
void store_set_timezone(const char *name);

/* Minutes without a touch before the screen turns off; 0 = never. */
uint8_t store_screen_timeout_min(void);
void store_set_screen_timeout_min(uint8_t min);

#ifdef __cplusplus
}
#endif
