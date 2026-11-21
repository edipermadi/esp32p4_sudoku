/**
 * @file replay_timeline.c
 * @ingroup replay_timeline
 * @brief A move log played back: the board at any step, the play times, the speed.
 */
#include "replay_timeline.h"
#include <string.h>
#include "esp_heap_caps.h"

static const uint8_t RP_SPEEDS[] = {1, 2, 4, 8};    /**< the speeds, round */

static const sudoku_geom_t *rp_geo;                 /**< the replayed puzzle's board */
static uint8_t rp_given[81];                        /**< its givens */
static game_move_t *rp_moves;                       /**< the log (PSRAM, GAME_MOVES_MAX) */
static uint32_t *rp_vt;                             /**< when each move plays, gaps shortened (PSRAM) */
static int rp_n;                                    /**< the moves */
static int rp_k;                                    /**< the step shown: moves 0..rp_k-1 made */
static uint32_t rp_clock;                           /**< where playing is on the timeline */
static bool rp_playing;                             /**< it plays */
static int rp_speed;                                /**< RP_SPEEDS[rp_speed] */

/* (documented in replay_timeline.h) */
bool replay_prepare(const uint8_t given[81], const sudoku_geom_t *geo, const game_move_t *moves, int n)
{
    if (!rp_moves) {
        rp_moves = heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    }
    if (!rp_vt) {
        rp_vt = heap_caps_malloc(sizeof(uint32_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    }
    if (!rp_moves || !rp_vt) {
        return false;
    }
    if (n > GAME_MOVES_MAX) {
        n = GAME_MOVES_MAX;
    }
    if (n > 0 && moves != rp_moves) {
        memcpy(rp_moves, moves, sizeof(game_move_t) * n);
    }
    rp_n = n;
    uint32_t prev_t = 0, vt = 0;
    for (int i = 0; i < n; i++) {
        uint32_t gap = rp_moves[i].t_ms >= prev_t ? rp_moves[i].t_ms - prev_t : 0;
        vt += gap > RP_MAX_GAP_MS ? RP_MAX_GAP_MS : gap;
        rp_vt[i] = vt;
        prev_t = rp_moves[i].t_ms;
    }
    memcpy(rp_given, given, sizeof(rp_given));
    rp_geo = geo;
    return true;
}

/* (documented in replay_timeline.h) */
void replay_seek(int k, bool keep_playing)
{
    rp_k = k < 0 ? 0 : (k > rp_n ? rp_n : k);
    rp_clock = rp_k ? rp_vt[rp_k - 1] : 0;
    if (!keep_playing) {
        rp_playing = false;
    }
}

/* (documented in replay_timeline.h) */
bool replay_tick(uint32_t dt)
{
    if (!rp_playing) {
        return false;
    }
    rp_clock += dt * RP_SPEEDS[rp_speed];
    int k = rp_k;
    while (k < rp_n && rp_vt[k] <= rp_clock) {
        k++;
    }
    if (k == rp_n) {
        rp_playing = false;
    }
    if (k != rp_k || !rp_playing) {
        rp_k = k;
        return true;
    }
    return false;
}

/* (documented in replay_timeline.h) */
void replay_toggle(void)
{
    if (!rp_playing && rp_k >= rp_n) {
        replay_seek(0, false);    /* at the end: from the start */
    }
    rp_playing = !rp_playing;
}

/* (documented in replay_timeline.h) */
void replay_next_speed(void)
{
    rp_speed = (rp_speed + 1) % (int)sizeof(RP_SPEEDS);
}

/* (documented in replay_timeline.h) */
int replay_speed_x(void)
{
    return RP_SPEEDS[rp_speed];
}

/* (documented in replay_timeline.h) */
void replay_set(int k, bool playing)
{
    rp_k = k;
    rp_playing = playing;
}

/* (documented in replay_timeline.h) */
int replay_k(void)
{
    return rp_k;
}

/* (documented in replay_timeline.h) */
int replay_n(void)
{
    return rp_n;
}

/* (documented in replay_timeline.h) */
bool is_replay_playing(void)
{
    return rp_playing;
}

/* (documented in replay_timeline.h) */
uint32_t replay_ms_at(int k)
{
    return k ? rp_moves[k - 1].t_ms : 0;
}

/* (documented in replay_timeline.h) */
uint32_t replay_vt_at(int k)
{
    return k ? rp_vt[k - 1] : 0;
}

/* (documented in replay_timeline.h) */
const sudoku_geom_t *replay_geom(void)
{
    return rp_geo;
}

/* (documented in replay_timeline.h) */
uint8_t replay_given(int i)
{
    return rp_given[i];
}

/* (documented in replay_timeline.h) */
int replay_board(uint8_t val[81], uint8_t kind[81], bool bad[81])
{
    memcpy(val, rp_given, 81);
    memset(kind, MOVE_HAND, 81);
    for (int i = 0; i < rp_k; i++) {
        val[rp_moves[i].cell] = rp_moves[i].digit;
        kind[rp_moves[i].cell] = rp_moves[i].kind;
    }
    for (int i = 0; i < 81; i++) {
        bool b = false;
        if (rp_geo->used[i]) {
            for (int j = 0; j < 81 && val[i] && !b; j++) {
                b = j != i && val[j] == val[i] && is_sudoku_geom_peer(rp_geo, i, j);
            }
        }
        bad[i] = b && !rp_given[i];
    }
    return rp_k ? rp_moves[rp_k - 1].cell : -1;
}
