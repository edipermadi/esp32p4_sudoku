/**
 * @file saver_stars_core.h
 * @ingroup saver_stars_core
 * @brief A starfield flown through, for the screen saver, without LVGL (drawn by ui_saver_stars.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup saver_stars_core Stars core
 * @ingroup ui_sudoku
 * @brief Stars scattered in front of the viewer, coming nearer each step: a star at (x, y, z) shows at
 *        x / z, y / z from the middle of the screen, so far ones crowd the middle and near ones rush
 *        out to the edges, brighter and bigger the nearer they come. A star that passes the viewer or
 *        leaves the screen comes back far away at a new random place. The speed rises and falls in
 *        slow waves (surges with long streaks, calm drifts between them), and the view rolls slowly
 *        about the way it flies.
 * @{
 */

#define STARS_W         720                   /**< the field's size, px (the screen) */
#define STARS_N         640                   /**< stars */
#define STARS_FAR       4.0f                  /**< how far stars come back, in units of the screen's half width */
#define STARS_NEAR      0.08f                 /**< nearer than this a star has passed */
#define STARS_SPEED     0.028f                /**< the way flown a step, on average */
#define STARS_TRAIL     4.0f                  /**< a streak reaches back where the star was this many steps ago */
#define STARS_WAVE      900                   /**< steps a wave of the speed takes */
#define STARS_ROLL      0.0015f               /**< the view's roll a step, radians */
#define STARS_TONES     6                     /**< star colours (stars_get()'s tone) */

/**
 * @brief Starts: the stars' memory taken (from malloc(): PSRAM on the board) if it isn't yet, and the
 *        stars scattered at random through the whole depth.
 * @param[in] seed  the random seed (any value; the same seed gives the same flights)
 * @return false if there was no memory for them
 */
bool stars_start(uint32_t seed);
/** @brief Gives the stars' memory back (stars_start() takes it again). */
void stars_free(void);
/** @brief One step: every star nearer by the speed, the view rolled, the stars that passed back far away. */
void stars_step(void);
/**
 * @brief A star as it shows: its streak, from where it was STARS_TRAIL steps ago (at this step's speed and
 *        roll) to where it is.
 * @param[in] i  0..STARS_N-1
 * @param[out] x0  gets the streak's tail, px
 * @param[out] y0  gets the streak's tail, px
 * @param[out] x1  gets where it is, px
 * @param[out] y1  gets where it is, px
 * @param[out] bright  gets its brightness, 0 (far: unseen) .. 255 (near)
 * @param[out] tone  gets its colour, 0..STARS_TONES-1
 * @return false if it doesn't show (just come back far away, or no stars)
 */
bool stars_get(int i, int *x0, int *y0, int *x1, int *y1, int *bright, int *tone);
/**
 * @brief The speed now.
 * @return the way flown a step
 */
float stars_speed(void);
/**
 * @brief A star's depth (for the tests).
 * @param[in] i  0..STARS_N-1
 * @return its z, STARS_NEAR..STARS_FAR (0 with no stars)
 */
float stars_depth(int i);

/** @} */

#ifdef __cplusplus
}
#endif
