#pragma once
/*
 * Persistent state in NVS: per-puzzle statistics (best time, fewest
 * mistakes, times solved), the one game in progress, and settings.
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
    uint16_t mistakes;         /* rule-breaking digits placed so far (added later: */
} saved_game_t;                /* a save without it loads with 0) */

void store_init(void);

/* Best time in seconds, 0 = never solved. */
uint32_t store_best_time(uint16_t id);
/* Records a solve; returns true if it is a new best time (or the first solve). */
bool store_record_solve(uint16_t id, uint32_t seconds, uint32_t mistakes);
/* Fewest mistakes over all solves (capped at 254), STORE_MISTAKES_UNKNOWN if
 * every solve predates mistake counting or the puzzle is unsolved. */
uint8_t store_fewest_mistakes(uint16_t id);
/* Number of times solved (capped at 255), 0 = never. */
uint8_t store_solve_count(uint16_t id);
/* Number of solved puzzles among [first, first+count). */
uint16_t store_solved_count(uint16_t first, uint16_t count);

/* Game in progress (one slot). */
const saved_game_t *store_saved_game(void);   /* id == STORE_NO_GAME if none */
/* Updates the in-memory copy only; store_commit_game() writes it to flash.
 * A flash write can blink the display, so callers commit it while the
 * screen is changing anyway. */
void store_set_game(const saved_game_t *g);
void store_commit_game(void);
void store_clear_game(void);                  /* memory + flash, immediately */

uint8_t store_last_collection(void);
void store_set_last_collection(uint8_t c);

bool store_dark_mode(void);
void store_set_dark_mode(bool dark);

bool store_sound_enabled(void);
void store_set_sound_enabled(bool on);

/* Minutes without a touch before the screen turns off; 0 = never. */
uint8_t store_screen_timeout_min(void);
void store_set_screen_timeout_min(uint8_t min);

#ifdef __cplusplus
}
#endif
