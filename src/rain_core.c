/**
 * @file rain_core.c
 * @ingroup rain_core
 * @brief Digital rain for the screen saver, without LVGL (drawn by ui_saver_rain.c).
 */
#include "rain_core.h"
#include <stdlib.h>
#include <string.h>

/** @brief A column of rain. */
typedef struct {
    int32_t head;                         /**< the head's row, 1/256 rows (negative: not in yet) */
    int16_t speed;                        /**< rows a step, 1/256 */
    uint8_t trail;                        /**< the trail's length, cells (the head included) */
    uint8_t colour;                       /**< its colour, 0..RAIN_COLOURS-1 */
} rain_col_t;

static rain_col_t *s_col;                 /**< the columns (NULL: no rain) */
static uint8_t *s_digit;                  /**< each cell's digit, 1..9, row by row */
static uint8_t *s_level;                  /**< each cell's level, 0..RAIN_HEAD, row by row */
static uint32_t s_rng = 1;                /**< xorshift32 state (never 0) */

/**
 * @brief The next random number (xorshift32).
 * @return it
 */
static uint32_t rnd(void)
{
    uint32_t x = s_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng = x;
    return x;
}

/**
 * @brief A random number in a range.
 * @param[in] lo  at least
 * @param[in] hi  at most
 * @return it
 */
static int32_t rnd_in(int32_t lo, int32_t hi)
{
    return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1));
}

/**
 * @brief A random digit, 1..9.
 * @return it
 */
static uint8_t rnd_digit(void)
{
    return (uint8_t)rnd_in(1, 9);
}

/**
 * @brief A column starts again: a new speed, trail and colour, its head above the top after a random pause.
 * @param[in] c  the column
 */
static void col_restart(rain_col_t *c)
{
    c->speed = (int16_t)rnd_in(RAIN_SPEED_MIN, RAIN_SPEED_MAX);
    c->trail = (uint8_t)rnd_in(RAIN_TRAIL_MIN, RAIN_TRAIL_MAX);
    c->colour = (uint8_t)rnd_in(0, RAIN_COLOURS - 1);
    c->head = -(int32_t)rnd_in(1, RAIN_PAUSE_MAX) * c->speed;   /* (the pause, in steps) */
}

/* (documented in rain_core.h) */
void rain_free(void)
{
    free(s_col);
    free(s_digit);
    free(s_level);
    s_col = NULL;
    s_digit = s_level = NULL;
}

/* (documented in rain_core.h) */
bool rain_start(uint32_t seed)
{
    if (!s_col) {
        s_col = calloc(RAIN_COLS, sizeof(*s_col));
        s_digit = malloc(RAIN_CELLS);
        s_level = malloc(RAIN_CELLS);
        if (!s_col || !s_digit || !s_level) {
            rain_free();
            return false;
        }
    }
    s_rng = seed ? seed : 0x9E3779B9u;
    for (int i = 0; i < RAIN_CELLS; i++) {
        s_digit[i] = rnd_digit();
    }
    memset(s_level, 0, RAIN_CELLS);
    for (int c = 0; c < RAIN_COLS; c++) {
        col_restart(&s_col[c]);
        s_col[c].head -= (int32_t)rnd_in(0, RAIN_ROWS) * 256;    /* (staggered more at the start) */
    }
    return true;
}

/* (documented in rain_core.h) */
void rain_step(void)
{
    if (!s_col) {
        return;
    }
    for (int c = 0; c < RAIN_COLS; c++) {
        rain_col_t *k = &s_col[c];
        int32_t before = k->head >> 8;
        k->head += k->speed;
        int32_t row = k->head >> 8;                 /* (arithmetic shift: negative stays negative) */
        for (int32_t r = before + 1; r <= row; r++) {
            if (r >= 0 && r < RAIN_ROWS) {
                s_digit[r * RAIN_COLS + c] = rnd_digit();   /* a new digit where the head arrives */
            }
        }
        for (int r = 0; r < RAIN_ROWS; r++) {
            int32_t d = row - r;                    /* cells behind the head */
            uint8_t lv = 0;
            if (k->head >= 0 && d >= 0 && d < k->trail) {
                lv = (uint8_t)(1 + (RAIN_HEAD - 1) * (k->trail - d) / k->trail);
                if (lv >= RAIN_HEAD) {
                    lv = RAIN_HEAD - 1;                 /* (the trail: below the head) */
                }
                if (d == 0) {
                    lv = RAIN_HEAD;
                }
            }
            s_level[r * RAIN_COLS + c] = lv;
        }
        if (row - k->trail >= RAIN_ROWS) {          /* the whole trail has gone: again from the top */
            col_restart(k);
        }
    }
    int flicker = rnd_in(0, RAIN_FLICKER);
    for (int tries = 0; flicker > 0 && tries < 64; tries++) {
        int i = (int)(rnd() % RAIN_CELLS);
        if (s_level[i] > 0 && s_level[i] < RAIN_HEAD) {
            s_digit[i] = rnd_digit();
            flicker--;
        }
    }
}

/* (documented in rain_core.h) */
uint8_t rain_cell(int col, int row, uint8_t *digit)
{
    if (!s_col) {
        if (digit) {
            *digit = 1;
        }
        return 0;
    }
    int i = row * RAIN_COLS + col;
    if (digit) {
        *digit = s_digit[i];
    }
    return s_level[i];
}

/* (documented in rain_core.h) */
uint8_t rain_colour(int col)
{
    return s_col ? s_col[col].colour : 0;
}

/* (documented in rain_core.h) */
int32_t rain_head(int col)
{
    return s_col ? s_col[col].head : 0;
}
