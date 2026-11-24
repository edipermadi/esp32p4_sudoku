/**
 * @file saver_lissajous_core.h
 * @ingroup saver_lissajous_core
 * @brief Lissajous figures for the screen saver, without LVGL (drawn by ui_saver_lissajous.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup saver_lissajous_core Lissajous core
 * @ingroup ui_sudoku
 * @brief The figure an oscilloscope draws from two sine waves: x = sin(a t + d), y = sin(b t), for a
 *        frequency ratio a:b from a list of pleasing ones. The phase d drifts a little every step, so
 *        the figure seems to turn in space; every LISS_HOLD steps it shrinks to the middle and grows
 *        back with another ratio. A beam runs along the figure, for a bright spot with a short tail.
 * @{
 */

#define LISS_W          720                   /**< the field's size, px (the screen) */
#define LISS_R          320                   /**< the figure's half width, px */
#define LISS_HOLD       400                   /**< steps a ratio is shown */
#define LISS_FADE       25                    /**< steps the figure shrinks (and as many it grows) */
#define LISS_DRIFT      0.012f                /**< the phase's drift a step, radians */
#define LISS_BEAM       0.02f                 /**< the beam's way along the figure a step, of a turn */
#define LISS_SEGS_PER   96                    /**< the figure's segments, per unit of a + b */
#define LISS_SEGS_MAX   (LISS_SEGS_PER * 16)  /**< at most */

/**
 * @brief A new figure: a random ratio and phase.
 * @param[in] seed  the random seed (any value; the same seed gives the same figures)
 */
void liss_start(uint32_t seed);
/** @brief One step: the phase drifts, the beam moves on, and the figure shrinks, changes or grows. */
void liss_step(void);
/**
 * @brief The figure's segments now (more for a busier ratio).
 * @return how many (the figure has one point more: it closes on itself)
 */
int liss_segs(void);
/**
 * @brief A point of the figure.
 * @param[in] i  0..liss_segs() (the last the same as the first)
 * @param[out] x  gets x, px
 * @param[out] y  gets y, px
 */
void liss_point(int i, int *x, int *y);
/**
 * @brief Where the beam is, or a place behind it along the figure (its tail).
 * @param[in] age  0 (the beam), 1 (a segment of the figure behind it) ...
 * @param[out] x  gets x, px
 * @param[out] y  gets y, px
 */
void liss_beam(int age, int *x, int *y);
/**
 * @brief The ratio shown.
 * @param[out] a  gets x's frequency
 * @param[out] b  gets y's frequency
 */
void liss_ratio(int *a, int *b);
/**
 * @brief How big the figure is now (while it shrinks or grows).
 * @return 0..1
 */
float liss_size(void);

/** @} */

#ifdef __cplusplus
}
#endif
