/**
 * @file boids_core.h
 * @ingroup boids_core
 * @brief A flock of boids for the screen saver, without LVGL (drawn by ui_saver_boids.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup boids_core Boids core
 * @ingroup ui_sudoku
 * @brief Reynolds' boids: BOIDS_N birds on a BOIDS_W x BOIDS_W field, each steering by three rules
 *        about its neighbours within BOIDS_SEE: keep apart from those nearer than BOIDS_APART
 *        (separation), head the way they head (alignment) and towards their middle (cohesion); with a
 *        little wander, a speed kept between BOIDS_SPEED_MIN and BOIDS_SPEED_MAX, and a turn away
 *        from the edges. Each remembers its last BOIDS_TRAIL places, for its trail.
 * @{
 */

#define BOIDS_N           120                 /**< birds */
#define BOIDS_W           720                 /**< the field's size, px (the screen) */
#define BOIDS_TRAIL       12                  /**< places remembered a bird (the newest first) */
#define BOIDS_SEE         60.0f               /**< how far a bird sees its neighbours, px */
#define BOIDS_APART       18.0f               /**< nearer than this it keeps away, px */
#define BOIDS_SPEED_MIN   2.0f                /**< px a step, at least */
#define BOIDS_SPEED_MAX   5.0f                /**< px a step, at most */
#define BOIDS_EDGE        70.0f               /**< nearer the edge than this it turns back, px */

/**
 * @brief A new flock: the birds at random places, heading random ways, their trails where they are.
 *        Its memory (about 14 KB, from malloc(): PSRAM on the board) is taken here if it isn't yet.
 * @param[in] seed  the random seed (any value; the same seed gives the same flight)
 * @return false if there was no memory for it
 */
bool boids_start(uint32_t seed);
/** @brief Gives the flock's memory back (boids_start() takes it again). */
void boids_free(void);
/** @brief One step: every bird steers by the rules, then moves; its trail moves along. */
void boids_step(void);
/**
 * @brief Where a bird was.
 * @param[in] i  the bird, 0..BOIDS_N-1
 * @param[in] age  0 (now) .. BOIDS_TRAIL-1 (the oldest place remembered)
 * @param[out] x  gets x, px
 * @param[out] y  gets y, px
 */
void boids_at(int i, int age, float *x, float *y);
/**
 * @brief A bird's velocity.
 * @param[in] i  the bird, 0..BOIDS_N-1
 * @param[out] vx  gets x, px a step
 * @param[out] vy  gets y, px a step
 */
void boids_vel(int i, float *vx, float *vy);

/** @} */

#ifdef __cplusplus
}
#endif
