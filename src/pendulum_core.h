/**
 * @file pendulum_core.h
 * @ingroup pendulum_core
 * @brief A double pendulum for the screen saver, without LVGL (drawn by ui_saver_pendulum.c).
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
 * @brief A double pendulum: two equal rods and bobs swinging under gravity without friction, its
 *        equations of motion integrated with Runge-Kutta (RK4) in PEND_SUBSTEPS small steps a frame, so
 *        its energy stays put. It starts from random high angles, where its motion is chaotic, and
 *        starts again from new ones every PEND_RUN frames. The lower bob's last PEND_TRAIL places are
 *        kept, for its trail.
 * @{
 */

#define PEND_W          720                   /**< the field's size, px (the screen) */
#define PEND_ARM        165                   /**< a rod's length, px */
#define PEND_TRAIL      1000                  /**< the lower bob's places kept (PEND_PER_FRAME a frame) */
#define PEND_SUBSTEPS   24                    /**< integration steps a frame */
#define PEND_PER_FRAME  8                     /**< trail places a frame (every PEND_SUBSTEPS / this steps) */
#define PEND_DT         0.0025f               /**< an integration step, s (in the pendulum's own time) */
#define PEND_RUN        3000                  /**< frames before it starts again (2 minutes at 25 a second) */

/**
 * @brief A new swing: random high angles, still. Its memory (about 8 KB, from malloc(): PSRAM on the
 *        board) is taken here if it isn't yet.
 * @param[in] seed  the random seed (any value; the same seed gives the same swing)
 * @return false if there was no memory for it
 */
bool pend_start(uint32_t seed);
/** @brief Gives its memory back (pend_start() takes it again). */
void pend_free(void);
/**
 * @brief One frame of the swing (PEND_SUBSTEPS integration steps, PEND_PER_FRAME trail places); every
 *        PEND_RUN frames it starts again from new angles, its trail forgotten.
 * @return true if it started again
 */
bool pend_step(void);
/**
 * @brief The bobs now.
 * @param[out] x1  gets the upper bob's x, px
 * @param[out] y1  gets its y, px
 * @param[out] x2  gets the lower bob's x, px
 * @param[out] y2  gets its y, px
 */
void pend_bobs(float *x1, float *y1, float *x2, float *y2);
/**
 * @brief The trail's places kept so far.
 * @return how many, 0..PEND_TRAIL
 */
int pend_trail_len(void);
/**
 * @brief A place of the lower bob's trail.
 * @param[in] age  0 (the newest) .. pend_trail_len()-1
 * @param[out] x  gets x, px
 * @param[out] y  gets y, px
 */
void pend_trail(int age, float *x, float *y);
/**
 * @brief The swing's energy (kinetic + potential, in the pendulum's own units; for the tests).
 * @return it
 */
float pend_energy(void);
/**
 * @brief The angles now (for the tests).
 * @param[out] a1  gets the upper rod's, radians from straight down
 * @param[out] a2  gets the lower rod's
 */
void pend_angles(float *a1, float *a2);
/**
 * @brief Sets the angles, still (for the tests).
 * @param[in] a1  the upper rod's, radians from straight down
 * @param[in] a2  the lower rod's
 */
void pend_set(float a1, float a2);

/** @} */

#ifdef __cplusplus
}
#endif
