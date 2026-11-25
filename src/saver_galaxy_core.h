/**
 * @file saver_galaxy_core.h
 * @ingroup saver_galaxy_core
 * @brief Two galaxies colliding, for the screen saver, without LVGL (drawn by ui_saver_galaxy.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup saver_galaxy_core Galaxy core
 * @ingroup ui_sudoku
 * @brief Two galaxies meeting, the way Alar and Juri Toomre first showed how tidal tails form (1972): each galaxy a
 *        heavy core with a disk of GALAXY_PER stars going round it; the two cores pull each other (gravity,
 *        softened a little so a close pass stays smooth) and come in on a parabola; the stars are pulled by both
 *        cores (but pull nothing themselves). As the cores swing past each other, the near side of each disk is
 *        drawn out into a bridge and the far side flung off into a long curving tail. Real galaxies also slow
 *        each other down (dynamical friction: their stars and dark matter soak up the motion); here the cores are
 *        slowed a little when close, so after a pass or two they fall together and merge. Each disk is tilted at
 *        random, and spins with the cores' orbit or against it (against: no tail), so every meeting is
 *        different. The stepping is leapfrog (kick, drift, kick), which keeps orbits from drifting.
 *
 * Units: the gravitational constant 1, a core's mass 1, lengths in disk radii (a disk is 1 across, roughly).
 * Shown in 3D (galaxy_get()): seen from a little above, the view turning slowly round.
 * @{
 */

#define GALAXY_PER          3000              /**< stars in each galaxy */
#define GALAXY_STARS        (2 * GALAXY_PER)  /**< stars in all */
#define GALAXY_W            720               /**< the view's size, px */
#define GALAXY_SCALE        80.0f             /**< px a unit (a disk's radius) */
#define GALAXY_TONES        6                 /**< star colours: galaxy 0 inner, middle, outer; galaxy 1 the same */
#define GALAXY_END          13.5f             /**< a meeting's time (from the start, units): then the next */

/**
 * @brief Starts a meeting: the stars' memory taken (from malloc(): PSRAM on the board) if it isn't yet, the two
 *        disks built, tilted at random, the cores far apart on their way in.
 * @param[in] seed  the random seed (any value; the same seed, the same meeting)
 * @return false if there was no memory
 */
bool galaxy_start(uint32_t seed);
/** @brief Gives the stars' memory back (galaxy_start() takes it again). */
void galaxy_free(void);
/** @brief A frame's worth of time: a few leapfrog steps, the view turned a little. */
void galaxy_step(void);
/**
 * @brief Is the meeting over (GALAXY_END reached: time for the next)?
 * @return true if so
 */
bool is_galaxy_done(void);
/**
 * @brief A star as it shows.
 * @param[in] i  0..GALAXY_STARS-1 (the first GALAXY_PER galaxy 0's)
 * @param[out] x  gets where, px
 * @param[out] y  gets where, px
 * @param[out] tone  gets its colour, 0..GALAXY_TONES-1 (galaxy 1's from 3)
 * @return false if it's off the view
 */
bool galaxy_get(int i, int *x, int *y, int *tone);
/**
 * @brief A core as it shows.
 * @param[in] k  0 or 1
 * @param[out] x  gets where, px
 * @param[out] y  gets where, px
 * @return false if it's off the view
 */
bool galaxy_core(int k, int *x, int *y);
/**
 * @brief The time since the meeting began (for the tests).
 * @return it, units
 */
float galaxy_time(void);
/**
 * @brief How far apart the cores are (for the tests).
 * @return it, units
 */
float galaxy_separation(void);
/**
 * @brief How far a star is from its own core (for the tests).
 * @param[in] i  0..GALAXY_STARS-1
 * @return it, units
 */
float galaxy_radius(int i);

/** @} */

#ifdef __cplusplus
}
#endif
