/**
 * @file saver_ripple_core.h
 * @ingroup saver_ripple_core
 * @brief Raindrops rippling a pool, for the screen saver, without LVGL (drawn by ui_saver_ripple.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup saver_ripple_core Ripple core
 * @ingroup ui_sudoku
 * @brief A pool's surface, RIPPLE_W x RIPPLE_W heights, hit by raindrops (and now and then a stone) at random. The
 *        surface follows the wave equation, stepped the classic way with two height fields: a cell's next height
 *        is half its four neighbours' sum less its height a step ago, a little damped; so a drop sends out rings
 *        that spread, cross one another and bounce off the pool's sides, slowly dying away.
 *
 * It is drawn (ripple_publish()) as a tiled pool floor seen through the water: each pixel shows the floor a little
 * to the side, as far as the surface's slope there bends the light (refraction), with a white glint where the
 * slope faces the light. The pictures are kept twice: one to draw (ripple_frame()), one for the next, so the
 * stepping (a worker task) and the drawing (LVGL's task) don't get in each other's way.
 * @{
 */

#define RIPPLE_W            360               /**< the surface's cells a side (each 2 x 2 px on the screen) */
#define RIPPLE_SCREEN       720               /**< a frame's size, px */
#define RIPPLE_DAMP         6                 /**< each step a wave loses 1/2^this of its height */
#define RIPPLE_DROP_EVERY   9                 /**< a raindrop every this many steps, on average */
#define RIPPLE_STONE_EVERY  700               /**< a stone (a big drop) every this many steps, on average */
#define RIPPLE_TONES        3                 /**< the colours ripple_start() takes: the tiles, the joints, the glints */

/**
 * @brief Starts: the pool's memory taken (from malloc(): PSRAM on the board) if it isn't yet, its surface still,
 *        its floor drawn.
 * @param[in] seed  the random seed (any value; the same seed rains the same)
 * @param[in] colours  the floor's tiles, its joints between them, the glints, 0xRRGGBB
 * @return false if there was no memory
 */
bool ripple_start(uint32_t seed, const uint32_t colours[RIPPLE_TONES]);
/** @brief Gives the pool's memory back (ripple_start() takes it again). */
void ripple_free(void);
/**
 * @brief A drop: the surface pushed down in a round dent.
 * @param[in] x  its middle, cells
 * @param[in] y  its middle, cells
 * @param[in] r  its radius, cells
 * @param[in] depth  how far down, at its middle (heights: a big drop is some 2000)
 */
void ripple_drop(int x, int y, int r, int depth);
/** @brief One step (the worker's part): maybe a raindrop or a stone, then the waves move on (ripple_waves()). */
void ripple_step(void);
/** @brief The waves move on a step, no rain (ripple_step()'s second half; for the tests too). */
void ripple_waves(void);
/**
 * @brief The surface as it is now becomes the picture drawn, if the last one was drawn already (ripple_frame()
 *        takes it).
 * @return true if it was handed over
 */
bool ripple_publish(void);
/**
 * @brief Is there a picture not drawn yet?
 * @return true if so
 */
bool is_ripple_fresh(void);
/**
 * @brief A frame: the handed-over picture, each pixel doubled to fill RIPPLE_SCREEN; then it counts as drawn.
 * @param[out] out  gets the frame, RIPPLE_SCREEN x RIPPLE_SCREEN RGB565
 * @return false if there was nothing handed over (out unchanged)
 */
bool ripple_frame(uint16_t *out);
/**
 * @brief A cell's height (for the tests).
 * @param[in] x  across, 0..RIPPLE_W-1
 * @param[in] y  down, 0..RIPPLE_W-1
 * @return it
 */
int ripple_height(int x, int y);
/**
 * @brief The surface's energy: its heights squared, summed (for the tests: it dies away).
 * @return it
 */
double ripple_energy(void);

/** @} */

#ifdef __cplusplus
}
#endif
