/**
 * @file pendulum_core.h
 * @ingroup pendulum_core
 * @brief Double pendulums for the screen saver, without LVGL (drawn by ui_saver_pendulum.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup pendulum_core Pendulum core
 * @ingroup ui_sudoku
 * @brief PEND_N double pendulums: two equal rods and bobs each, swinging under gravity without friction,
 *        their equations of motion integrated with Runge-Kutta (RK4) in PEND_SUBSTEPS small steps a
 *        frame, so their energy stays put. They start from the same random high angles but for
 *        PEND_NUDGE radians between them; the motion there is chaotic, so they swing as one for a while
 *        and then go their own ways. They start again from new angles every PEND_RUN frames. Each lower
 *        bob's last PEND_TRAIL places are kept with its speed there, for its trail.
 * @{
 */

#define PEND_N          3                     /**< pendulums */
#define PEND_W          720                   /**< the field's size, px (the screen) */
#define PEND_ARM        165                   /**< a rod's length, px */
#define PEND_TRAIL      750                   /**< a lower bob's places kept (PEND_PER_FRAME a frame) */
#define PEND_SUBSTEPS   24                    /**< integration steps a frame */
#define PEND_PER_FRAME  8                     /**< trail places a frame (every PEND_SUBSTEPS / this steps) */
#define PEND_DT         0.0025f               /**< an integration step, s (in the pendulum's own time) */
#define PEND_RUN        3000                  /**< frames before they start again (2 minutes at 25 a second) */
#define PEND_NUDGE      1e-5f                 /**< radians between one pendulum's start and the next's */
#define PEND_FAST       9.0f                  /**< a lower bob's speed counted as fast (rods of length 1) */

/**
 * @brief A new swing: random high angles (each pendulum PEND_NUDGE on from the last), still. The trails'
 *        memory (about 27 KB, from malloc(): PSRAM on the board) is taken here if it isn't yet.
 * @param[in] seed  the random seed (any value; the same seed gives the same swing)
 * @return false if there was no memory for it
 */
bool pend_start(uint32_t seed);
/** @brief Gives the trails' memory back (pend_start() takes it again). */
void pend_free(void);
/**
 * @brief One frame of the swing (PEND_SUBSTEPS integration steps, PEND_PER_FRAME trail places); every
 *        PEND_RUN frames they start again from new angles, their trails forgotten.
 * @return true if they started again
 */
bool pend_step(void);
/**
 * @brief A pendulum's bobs now.
 * @param[in] k  the pendulum, 0..PEND_N-1
 * @param[out] x1  gets the upper bob's x, px
 * @param[out] y1  gets its y, px
 * @param[out] x2  gets the lower bob's x, px
 * @param[out] y2  gets its y, px
 */
void pend_bobs(int k, float *x1, float *y1, float *x2, float *y2);
/**
 * @brief The trails' places kept so far (the same for every pendulum).
 * @return how many, 0..PEND_TRAIL
 */
int pend_trail_len(void);
/**
 * @brief A place of a lower bob's trail.
 * @param[in] k  the pendulum, 0..PEND_N-1
 * @param[in] age  0 (the newest) .. pend_trail_len()-1
 * @param[out] x  gets x, px
 * @param[out] y  gets y, px
 * @param[out] fast  gets how fast the bob was going there, 0 (still) .. 1 (PEND_FAST or more)
 */
void pend_trail(int k, int age, float *x, float *y, float *fast);
/**
 * @brief A pendulum's energy (kinetic + potential, in the pendulum's own units; for the tests).
 * @param[in] k  the pendulum, 0..PEND_N-1
 * @return it
 */
float pend_energy(int k);
/**
 * @brief A pendulum's angles now (for the tests).
 * @param[in] k  the pendulum, 0..PEND_N-1
 * @param[out] a1  gets the upper rod's, radians from straight down
 * @param[out] a2  gets the lower rod's
 */
void pend_angles(int k, float *a1, float *a2);
/**
 * @brief Sets the angles, still: pendulum k's upper rod at a1 + k PEND_NUDGE (for the tests).
 * @param[in] a1  the first pendulum's upper rod, radians from straight down
 * @param[in] a2  the lower rods'
 */
void pend_set(float a1, float a2);

/** @} */

#ifdef __cplusplus
}
#endif
