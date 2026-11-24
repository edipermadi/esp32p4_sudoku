/**
 * @file saver_rain_core.h
 * @ingroup saver_rain_core
 * @brief Digital rain for the screen saver, without LVGL (drawn by ui_saver_rain.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup saver_rain_core Rain core
 * @ingroup ui_sudoku
 * @brief Digital rain: RAIN_COLS columns of RAIN_ROWS cells, each column a falling trail of the digits
 *        1 to 9. A trail's head is the brightest (RAIN_HEAD), the digits behind it dimmer the further
 *        back, and now and then a digit in a trail changes. Each column falls at its own speed with its
 *        own trail length, and once its trail has left the bottom it starts again from the top after a
 *        random pause, in one of RAIN_COLOURS colours.
 * @{
 */

#define RAIN_COLS       30                    /**< columns */
#define RAIN_ROWS       30                    /**< rows */
#define RAIN_CELLS      (RAIN_COLS * RAIN_ROWS) /**< cells */
#define RAIN_LEVELS     8                     /**< brightness levels: 0 unlit .. RAIN_HEAD */
#define RAIN_HEAD       (RAIN_LEVELS - 1)     /**< the level of a trail's head */
#define RAIN_TRAIL_MIN  6                     /**< a trail's length, cells: at least */
#define RAIN_TRAIL_MAX  22                    /**< at most */
#define RAIN_SPEED_MIN  64                    /**< a column's speed, 1/256 cell a step: at least */
#define RAIN_SPEED_MAX  230                   /**< at most */
#define RAIN_PAUSE_MAX  40                    /**< steps at most between a trail leaving and the next */
#define RAIN_FLICKER    3                     /**< digits in trails changed each step, at most */
#define RAIN_COLOURS    6                     /**< colours a column may fall in (picked as it starts again) */

/**
 * @brief A new rain: every column paused for a random while (so they don't start together). Its
 *        memory (a few KB, from malloc(): PSRAM on the board) is taken here if it isn't yet.
 * @param[in] seed  the random seed (any value; the same seed gives the same rain)
 * @return false if there was no memory for it
 */
bool rain_start(uint32_t seed);
/** @brief Gives the rain's memory back (rain_start() takes it again). */
void rain_free(void);
/** @brief One step: every column falls by its speed, its trail is lit again, some digits flicker. */
void rain_step(void);
/**
 * @brief A cell: its brightness and digit.
 * @param[in] col  column, 0..RAIN_COLS-1
 * @param[in] row  row, 0..RAIN_ROWS-1
 * @param[out] digit  gets its digit, 1..9 (any while unlit); NULL: not wanted
 * @return its level, 0 (unlit) .. RAIN_HEAD
 */
uint8_t rain_cell(int col, int row, uint8_t *digit);
/**
 * @brief A column's colour (it keeps it while its trail is on the screen).
 * @param[in] col  column, 0..RAIN_COLS-1
 * @return 0..RAIN_COLOURS-1
 */
uint8_t rain_colour(int col);
/**
 * @brief Where a column's head is (for the tests).
 * @param[in] col  column, 0..RAIN_COLS-1
 * @return the row, 1/256 row units (negative: above the top, waiting or paused)
 */
int32_t rain_head(int col);

/** @} */

#ifdef __cplusplus
}
#endif
