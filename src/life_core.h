/**
 * @file life_core.h
 * @ingroup life_core
 * @brief Conway's Game of Life for the screen saver, without LVGL (drawn by ui_saver_life.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup life_core Life core
 * @ingroup ui_sudoku
 * @brief Conway's Game of Life on a LIFE_N x LIFE_N board whose edges wrap around (a torus), with each
 *        cell's glow: a cell just born glows hottest, a living one steady, and a dead one fades out over
 *        a few generations, like a phosphor's afterglow. When the board settles
 *        (the same pattern again within LIFE_PERIOD_MAX generations), or after LIFE_GEN_MAX generations,
 *        it is seeded again; the old cells fade out under the new ones.
 * @{
 */

#define LIFE_N              120               /**< cells across and down */
#define LIFE_CELLS          (LIFE_N * LIFE_N) /**< cells on the board */
#define LIFE_PERIOD_MAX     16                /**< a pattern back within this many generations: settled */
#define LIFE_GEN_MAX        3000              /**< generations at most before a new seed */
#define LIFE_DENSITY        30                /**< % of the cells alive in a new seed */

/** @brief A cell's glow: how it is drawn. */
typedef enum {
    LIFE_GLOW_OFF = 0,                    /**< dead for a while: unlit */
    LIFE_GLOW_FADE1,                      /**< dead, nearly faded out */
    LIFE_GLOW_FADE2,                      /**< dead, fading */
    LIFE_GLOW_FADE3,                      /**< dead, fading */
    LIFE_GLOW_FADE4,                      /**< died in the last generation */
    LIFE_GLOW_ALIVE,                      /**< alive for more than a generation */
    LIFE_GLOW_BORN,                       /**< born in the last generation */
    LIFE_GLOW_COUNT                       /**< how many */
} life_glow_t;

/**
 * @brief A new random board; the cells alive before it fade out (none on the first seed). The board's
 *        memory (about 43 KB, from malloc(): PSRAM on the board) is taken here if it isn't yet.
 * @param[in] seed  the random seed (any value; the same seed gives the same board)
 * @return false if there was no memory for the board
 */
bool life_seed(uint32_t seed);
/** @brief Gives the board's memory back (life_seed() takes it again). */
void life_free(void);
/**
 * @brief One generation: Conway's rules (born with 3 neighbours, alive with 2 or 3), the glows moved
 *        on, and a new seed when the board has settled or is old enough.
 * @return true if it was seeded again
 */
bool life_step(void);
/**
 * @brief A cell's glow.
 * @param[in] x  column, 0..LIFE_N-1
 * @param[in] y  row, 0..LIFE_N-1
 * @return its life_glow_t
 */
uint8_t life_glow(int x, int y);
/**
 * @brief Whether a cell is alive.
 * @param[in] x  column, 0..LIFE_N-1
 * @param[in] y  row, 0..LIFE_N-1
 * @return true if alive
 */
bool is_life_alive(int x, int y);
/**
 * @brief Sets a cell alive or dead, as a seed would (born or just died; for the tests).
 * @param[in] x      column, 0..LIFE_N-1
 * @param[in] y      row, 0..LIFE_N-1
 * @param[in] alive  alive
 */
void life_set(int x, int y, bool alive);
/**
 * @brief Clears the board: every cell dead and unlit, the generation count and history reset (taking
 *        the board's memory if it isn't yet).
 * @return false if there was no memory for the board
 */
bool life_clear(void);
/**
 * @brief The cells alive.
 * @return how many
 */
int life_population(void);
/**
 * @brief Generations since the last seed (or clear).
 * @return how many
 */
uint32_t life_generation(void);

/** @} */

#ifdef __cplusplus
}
#endif
