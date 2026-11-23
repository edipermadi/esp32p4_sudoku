/**
 * @file boids_core.h
 * @ingroup boids_core
 * @brief A flock of boids, their colours and the hawks chasing them, for the screen saver, without LVGL
 *        (drawn by ui_saver_boids.c).
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
 *        from the edges.
 *
 *        Each bird is one of BOIDS_COLOURS colours, and now and then takes the colour of a neighbour it
 *        sees (the voter model), so a flock comes to one colour and flocks that meet fight it out; once
 *        in a while a bird takes up another colour, which spreads or dies out.
 *
 *        BOIDS_HAWKS hawks chase the bird nearest each, a little faster than the birds cruise; a bird
 *        that sees a hawk within BOIDS_FEAR flees it, up to BOIDS_SPEED_FLEE. Birds and hawks remember
 *        their last BOIDS_TRAIL places, for their trails.
 * @{
 */

#define BOIDS_N           120                 /**< birds */
#define BOIDS_HAWKS       2                   /**< hawks (numbered after the birds: BOIDS_N ..) */
#define BOIDS_ALL         (BOIDS_N + BOIDS_HAWKS)   /**< birds and hawks */
#define BOIDS_W           720                 /**< the field's size, px (the screen) */
#define BOIDS_TRAIL       12                  /**< places remembered each (the newest first) */
#define BOIDS_SEE         60.0f               /**< how far a bird sees its neighbours, px */
#define BOIDS_APART       18.0f               /**< nearer than this it keeps away, px */
#define BOIDS_FEAR        110.0f              /**< a hawk nearer than this: the bird flees, px */
#define BOIDS_SPEED_MIN   2.0f                /**< px a step, at least */
#define BOIDS_SPEED_MAX   5.0f                /**< px a step, at most (cruising) */
#define BOIDS_SPEED_FLEE  6.5f                /**< px a step, at most, fleeing a hawk */
#define BOIDS_SPEED_HAWK  5.6f                /**< a hawk's speed, px a step */
#define BOIDS_EDGE        70.0f               /**< nearer the edge than this it turns back, px */
#define BOIDS_COLOURS     3                   /**< the birds' colours */

/**
 * @brief A new flock: birds and hawks at random places, heading random ways, their trails where they
 *        are, the birds' colours random. Its memory (about 15 KB, from malloc(): PSRAM on the board) is
 *        taken here if it isn't yet.
 * @param[in] seed  the random seed (any value; the same seed gives the same flight)
 * @return false if there was no memory for it
 */
bool boids_start(uint32_t seed);
/** @brief Gives the flock's memory back (boids_start() takes it again). */
void boids_free(void);
/** @brief One step: every bird and hawk steers by its rules, then moves; colours pass on. */
void boids_step(void);
/**
 * @brief Where a bird or hawk was.
 * @param[in] i  the bird, 0..BOIDS_N-1, or hawk, BOIDS_N..BOIDS_ALL-1
 * @param[in] age  0 (now) .. BOIDS_TRAIL-1 (the oldest place remembered)
 * @param[out] x  gets x, px
 * @param[out] y  gets y, px
 */
void boids_at(int i, int age, float *x, float *y);
/**
 * @brief A bird's or hawk's velocity.
 * @param[in] i  the bird or hawk
 * @param[out] vx  gets x, px a step
 * @param[out] vy  gets y, px a step
 */
void boids_vel(int i, float *vx, float *vy);
/**
 * @brief A bird's colour, and the one before it (for a blend while it changes).
 * @param[in] i  the bird, 0..BOIDS_N-1
 * @param[out] before  gets the colour it had before, 0..BOIDS_COLOURS-1 (NULL: not wanted)
 * @param[out] since  gets the steps since it changed (NULL: not wanted)
 * @return its colour, 0..BOIDS_COLOURS-1
 */
int boids_colour(int i, int *before, int *since);

/** @} */

#ifdef __cplusplus
}
#endif
