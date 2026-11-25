/**
 * @file saver_mandel_core.h
 * @ingroup saver_mandel_core
 * @brief A zoom into the Mandelbrot set, for the screen saver, without LVGL (drawn by ui_saver_mandel.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup saver_mandel_core Mandelbrot core
 * @ingroup ui_sudoku
 * @brief The Mandelbrot set (the points c where z -> z^2 + c, from z = 0, stays near 0), zoomed into for ever:
 *        a dive goes down level by level, each level twice as deep as the one before, and starts again from
 *        the whole set once single-precision floats run out (some 2^12 to 2^15 times deeper).
 *
 * Each level is a key: MANDEL_W x MANDEL_W pixels, each from the smooth escape count of its point (how many
 * steps z took to leave the circle of radius 16, with the fraction that hides the bands; MANDEL_TONE), or
 * MANDEL_INSIDE for a point taken to be in the set (one whose z comes back to where it was, or a new pixel
 * whose neighbours in the key before were all inside). A key is made bit by bit (mandel_work(), on a worker
 * task) while the one before is shown: mandel_frame() scales the shown key up from 1 to 2 times about its
 * middle, which is just where the next key's view ends, so the next key takes over without a jump. A quarter
 * of a key's pixels are the key before's middle ones (every other pixel each way), so only three quarters
 * are worked out. Between keys the middle moves a little (a whole number of the shown key's pixels, which
 * keeps the two keys' pixels lined up), towards the most detailed edge near the middle: an autopilot, so
 * the dive keeps to the set's edge (where the spirals and the small copies of the set are) and doesn't sink
 * into its black inside or drift out into the plain outside.
 * @{
 */

#define MANDEL_W        720                   /**< a key's size, px (the screen) */
#define MANDEL_INSIDE   0xFFFFu               /**< a key's pixel: in the set */
#define MANDEL_BAILOUT  256.0f                /**< |z|^2 beyond this: escaped */
#define MANDEL_SPAN     3.2f                  /**< level 0's width, in the plane */
#define MANDEL_STEER    64                    /**< the most the middle moves from one key to the next, the
                                               *   shown key's px */
#define MANDEL_LEVELS   20                    /**< a dive's levels at most */
#define MANDEL_TONE     80.0f                 /**< a key's pixel: the square root of its escape count times this
                                               *   (so the palette's 1024 colours span counts 0-164, 164-655 ...:
                                               *   the colours spread out where the counts climb, deeper in) */

/**
 * @brief Starts: the keys' memory taken (from malloc(): PSRAM on the board) if it isn't yet, and a dive
 *        begun: its first key to make (mandel_work()).
 * @param[in] seed  the random seed (any value; the same seed gives the same dives)
 * @return false if there was no memory
 */
bool mandel_start(uint32_t seed);
/** @brief Gives the keys' memory back (mandel_start() takes it again). */
void mandel_free(void);
/**
 * @brief Works on the key being made (the worker's part): some more of its rows.
 * @param[in] rows  how many rows, at most
 * @return true once the key is made (then nothing more to do until mandel_advance())
 */
bool mandel_work(int rows);
/**
 * @brief How far the key being made has got.
 * @return 0 .. 1 (1: done)
 */
float mandel_progress(void);
/**
 * @brief Is the key being made done?
 * @return true if so
 */
bool is_mandel_ready(void);
/**
 * @brief The key made becomes the one shown, and the next one is begun: one level deeper, its middle steered
 *        (the autopilot), or a new dive's first key when this dive has gone as deep as floats allow. Only once
 *        is_mandel_ready(), and not while mandel_work() runs.
 */
void mandel_advance(void);
/**
 * @brief Is a key shown (mandel_advance() called once at least)?
 * @return true if so
 */
bool is_mandel_shown(void);
/**
 * @brief Does the key being made begin a new dive (the shown key is its dive's last: fade it out)?
 * @return true if so
 */
bool is_mandel_dive_end(void);
/**
 * @brief The shown key's level in its dive.
 * @return 0 .. MANDEL_LEVELS - 1
 */
int mandel_level(void);
/**
 * @brief A frame: the shown key, zoomed in by 2^p about its middle moving towards the next key's, each
 *        pixel's count through the palette.
 * @param[out] out  gets the frame, MANDEL_W x MANDEL_W
 * @param[in] p  how far towards the next key, 0 .. 1
 * @param[in] pal  the palette: 1024 colours, a pixel's value (plus shift) picks pal[value & 1023]
 * @param[in] shift  added to each count (the colours flowing)
 * @param[in] inside  the colour of the set's inside
 */
void mandel_frame(uint16_t *out, float p, const uint16_t pal[1024], uint32_t shift, uint16_t inside);
/**
 * @brief A point's pixel value: its smooth escape count (for the tests).
 * @param[in] cx  the point's x
 * @param[in] cy  its y
 * @param[in] max_iter  steps at most
 * @param[in] eps  closer than this to where z was a while ago: a cycle, inside
 * @return the count's square root times MANDEL_TONE (up to 0xFFFE), or MANDEL_INSIDE
 */
uint16_t mandel_point(float cx, float cy, int max_iter, float eps);
/**
 * @brief The steps a level's points get at most.
 * @param[in] level  0 ..
 * @return them
 */
int mandel_max_iter(int level);
/**
 * @brief The shown key's pixels (for the tests).
 * @return MANDEL_W x MANDEL_W counts, or NULL
 */
const uint16_t *mandel_key(void);
/**
 * @brief The shown key's view (for the tests).
 * @param[out] cx  gets its middle's x
 * @param[out] cy  gets its middle's y
 * @param[out] d  gets a pixel's size in the plane
 * @param[out] mx  gets how far the next key's middle is, across, px (MANDEL_STEER at most)
 * @param[out] my  gets the same, down
 */
void mandel_view(float *cx, float *cy, float *d, int *mx, int *my);

/** @} */

#ifdef __cplusplus
}
#endif
