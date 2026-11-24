/**
 * @file saver_attractor_core.h
 * @ingroup saver_attractor_core
 * @brief Strange attractors for the screen saver, without LVGL (drawn by ui_saver_attractor.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup saver_attractor_core Attractor core
 * @ingroup ui_sudoku
 * @brief Clifford and de Jong attractors: a point moved again and again by four-parameter sine and
 *        cosine formulas, each place it lands counted on an ATTR_W x ATTR_W grid (the screen), so the
 *        shape builds up point by point, brighter where it is denser. The parameters are random, kept
 *        only if the points keep spreading (no single dots or loops), and the shape is scaled to fit.
 * @{
 */

#define ATTR_W          720                   /**< the grid's size, cells (the screen's pixels) */
#define ATTR_MARGIN     30                    /**< the gap kept around the shape, cells */
#define ATTR_MAX        255                   /**< a cell's count at most */
#define ATTR_TRIES      40                    /**< random parameter sets tried for a good one */

/** @brief The formulas. */
typedef enum {
    ATTR_CLIFFORD = 0,                    /**< x' = sin(a y) + c cos(a x), y' = sin(b x) + d cos(b y) */
    ATTR_DEJONG,                          /**< x' = sin(a y) - cos(b x), y' = sin(c x) - cos(d y) */
    ATTR_KINDS                            /**< how many */
} attr_kind_t;

/**
 * @brief The first attractor: the random numbers seeded and the grid taken (about 535 KB, from malloc():
 *        PSRAM on the board) if it isn't yet; then as attr_next().
 * @param[in] seed  the random seed (any value; the same seed gives the same attractors)
 * @return false if there was no memory for the grid
 */
bool attr_start(uint32_t seed);
/** @brief The next attractor: new random parameters that spread well, its size fitted, the grid cleared. */
void attr_next(void);
/** @brief Gives the grid's memory back (attr_start() takes it again). */
void attr_free(void);
/**
 * @brief Moves the point on n times, counting each cell it lands in.
 * @param[in] n  how many points
 * @param[out] dirty  gets the cells landed in (y * ATTR_W + x; a cell may be there more than once), or NULL
 * @param[in] max_dirty  dirty's room
 * @return how many cells went into dirty
 */
int attr_plot(int n, uint32_t *dirty, int max_dirty);
/**
 * @brief The counts, row by row.
 * @return ATTR_W x ATTR_W counts (0..ATTR_MAX), or NULL without a grid
 */
const uint8_t *attr_counts(void);
/**
 * @brief The box the shape is drawn in (it grows to take in every point plotted).
 * @param[out] x0  gets the left column
 * @param[out] y0  gets the top row
 * @param[out] x1  gets the right column (included)
 * @param[out] y1  gets the bottom row (included)
 */
void attr_box(int *x0, int *y0, int *x1, int *y1);
/**
 * @brief The current attractor's formulas.
 * @return its attr_kind_t
 */
int attr_kind(void);

/** @} */

#ifdef __cplusplus
}
#endif
