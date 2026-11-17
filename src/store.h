#pragma once
/*
 * Persistent state in NVS: best solve time per puzzle, the one game in
 * progress, and the last collection shown in the list.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORE_NO_GAME 0xFFFF

typedef struct {
    uint16_t id;               /* puzzle index, STORE_NO_GAME = none */
    uint8_t  val[81];          /* 0 = empty; givens included */
    uint16_t notes[81];        /* bit d (1..9) = pencil mark d */
    uint32_t elapsed_ms;
} saved_game_t;

void store_init(void);

/* Best time in seconds, 0 = never solved. */
uint32_t store_best_time(uint16_t id);
/* Records a solve; returns true if it is a new best (or the first solve). */
bool store_record_solve(uint16_t id, uint32_t seconds);
/* Number of solved puzzles among [first, first+count). */
uint16_t store_solved_count(uint16_t first, uint16_t count);

/* Game in progress (one slot). */
const saved_game_t *store_saved_game(void);   /* id == STORE_NO_GAME if none */
void store_save_game(const saved_game_t *g);
void store_clear_game(void);

uint8_t store_last_collection(void);
void store_set_last_collection(uint8_t c);

#ifdef __cplusplus
}
#endif
