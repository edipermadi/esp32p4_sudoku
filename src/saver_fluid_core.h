/**
 * @file saver_fluid_core.h
 * @ingroup saver_fluid_core
 * @brief Coloured liquids stirred and mixing, for the screen saver, without LVGL (drawn by ui_saver_fluid.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup saver_fluid_core Fluid core
 * @ingroup ui_sudoku
 * @brief A tank of water, FLUID_N x FLUID_N cells, stirred by FLUID_SPOONS invisible spoons that go round on slow
 *        looping paths, each pouring its own dye in pulses. The water follows Jos Stam's "stable fluids": each
 *        step the flow carries itself and the dyes along (each cell takes what was where its water came from,
 *        so nothing blows up however fast it goes), a little extra swirl is put back where the flow turns
 *        (vorticity confinement: the stepping smooths eddies away otherwise), and the flow is made
 *        incompressible (the pressure worked out by FLUID_PRESSURE_PASSES Gauss-Seidel passes and its slope
 *        taken off). The tank's walls let nothing through. It starts full, in a band of each dye; a cell holds at
 *        most a cell of dye in all, so a spoon's fresh dye pushes the others out and the tank never settles into
 *        one muddy colour. A cell's colour is the dyes' colours averaged by how much of each it holds (blue and
 *        yellow make a light green, not white), as bright as there's dye.
 *
 * The cells' colours are kept twice: one set to draw (fluid_frame()), the other for the next step
 * (fluid_publish()), so the stepping (a worker task) and the drawing (LVGL's task) don't get in each other's way.
 * @{
 */

#define FLUID_N             96                /**< the tank's cells a side */
#define FLUID_SPOONS        3                 /**< the spoons (and dyes) */
#define FLUID_PRESSURE_PASSES 24              /**< Gauss-Seidel passes for the pressure each step */
#define FLUID_SCREEN        720               /**< a frame's size, px */
#define FLUID_DRAWN         360               /**< drawn at this size, each pixel then doubled */

/**
 * @brief Starts: the tank's memory taken (from malloc(): PSRAM on the board) if it isn't yet, the water still and
 *        clear, the spoons' paths drawn at random.
 * @param[in] seed  the random seed (any value; the same seed stirs the same way)
 * @param[in] colours  each dye's colour, 0xRRGGBB
 * @return false if there was no memory
 */
bool fluid_start(uint32_t seed, const uint32_t colours[FLUID_SPOONS]);
/** @brief Gives the tank's memory back (fluid_start() takes it again). */
void fluid_free(void);
/** @brief One step (the worker's part): the spoons stir and pour, the flow and the dyes move on. */
void fluid_step(void);
/**
 * @brief The cells' colours after the last step become the ones drawn, if the last ones were drawn already
 *        (fluid_frame() takes them).
 * @return true if they were handed over
 */
bool fluid_publish(void);
/**
 * @brief Is there a set of colours not drawn yet?
 * @return true if so
 */
bool is_fluid_fresh(void);
/**
 * @brief A frame: the handed-over colours scaled up smoothly to FLUID_DRAWN, each pixel doubled to fill
 *        FLUID_SCREEN; then they count as drawn.
 * @param[out] out  gets the frame, FLUID_SCREEN x FLUID_SCREEN RGB565
 * @param[in] bright  the brightness, 0 (black) .. 256 (as it is)
 * @return false if there was nothing handed over (out unchanged)
 */
bool fluid_frame(uint16_t *out, int bright);
/**
 * @brief The flow's largest divergence (for the tests: about 0 after a step, the water incompressible).
 * @return it, cells a step
 */
float fluid_divergence(void);
/**
 * @brief All the dye in the tank (for the tests).
 * @param[in] k  the dye, 0..FLUID_SPOONS-1
 * @return its amount, summed over the cells
 */
float fluid_dye(int k);
/**
 * @brief The fastest the water moves (for the tests).
 * @return cells a step
 */
float fluid_speed(void);

/** @} */

#ifdef __cplusplus
}
#endif
