/**
 * @file saver_automata3d_core.h
 * @ingroup saver_automata3d_core
 * @brief 3D cellular automata for the screen saver, without LVGL (drawn by ui_saver_automata3d.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup saver_automata3d_core 3D automata core
 * @ingroup ui_sudoku
 * @brief The 3D cellular automata screen savers, each one Generations rule in a cube of A3_N x A3_N x A3_N cells
 *        whose faces are walls (outside is dead). A cell is dead (0), alive (1) or dying
 *        (2..states-1); a dead cell is born with a number of live neighbours in its birth set, a live
 *        one stays with one in its survive set, else starts dying, a dying one goes on to dead. The
 *        neighbours are the 26 around (Moore) or the 6 face to face (von Neumann), as the rule says.
 *        Each rule goes at its own pace and starts again from its own random start (a blob of noise in
 *        the middle, or one cell) when it dies out, stands still or has run its time (and been held a
 *        while: a crystal before it reaches the walls).
 * @{
 */

#define A3_N            32                    /**< cells along each edge */
#define A3_CELLS        (A3_N * A3_N * A3_N)  /**< cells in the cube */
#define A3_STATES_MAX   16                    /**< a rule's states at most */
#define A3_STILL        20                    /**< generations without a change before it starts again */

/** @brief The rules, each its own screen saver (a3_rule_name() names them). */
typedef enum {
    A3_R_CRYSTAL = 0,                     /**< Crystal growth, 0-6/1,3/2/VN: one cell grows a crystal, held */
    A3_R_DIAMOND,                         /**< Diamond growth, 5-6/1-3/7/VN: one cell sends out a shell, then dies out */
    A3_R_PYRO,                            /**< Pyroclastic, 4-7/6-8/10/M: a boiling mass */
    A3_R_BUILDER,                         /**< Builder 1, 2,6,9/4,6,8-9/10/M: grows, then crumbles away */
    A3_R_COUNT                            /**< how many */
} a3_rule_t;

/**
 * @brief Starts: the random numbers seeded, the cube's memory (128 KB, from malloc(): PSRAM on the
 *        board) taken if it isn't yet, and the rule's random start.
 * @param[in] seed  the random seed (any value; the same seed gives the same runs)
 * @param[in] rule  a3_rule_t
 * @return false if there was no memory for the cube
 */
bool a3_start(uint32_t seed, int rule);
/** @brief Gives the cube's memory back (a3_start() takes it again). */
void a3_free(void);
/**
 * @brief The rule started again from its own random start (or another rule, for the tests).
 * @param[in] rule  a3_rule_t
 */
void a3_set_rule(int rule);
/** @brief Clears the cube: every cell dead (for the tests). */
void a3_clear(void);
/**
 * @brief Sets a cell's state (for the tests).
 * @param[in] x  0..A3_N-1
 * @param[in] y  0..A3_N-1 (up)
 * @param[in] z  0..A3_N-1
 * @param[in] st  0..a3_states()-1
 */
void a3_set(int x, int y, int z, int st);
/**
 * @brief One generation; or, when the rule has died out, stood still for A3_STILL generations or run
 *        its time and then been held still for a while (a grown crystal), the rule started again from
 *        a new random start instead.
 * @return true if it started again
 */
bool a3_step(void);
/**
 * @brief A cell's state.
 * @param[in] x  0..A3_N-1
 * @param[in] y  0..A3_N-1 (up)
 * @param[in] z  0..A3_N-1
 * @return 0..a3_states()-1
 */
int a3_state(int x, int y, int z);
/**
 * @brief The cells' states, x fastest, then y, then z (index (z * A3_N + y) * A3_N + x).
 * @return them (A3_CELLS; NULL before a3_start())
 */
const uint8_t *a3_cells(void);
/**
 * @brief The rule running.
 * @return a3_rule_t
 */
int a3_rule(void);
/**
 * @brief A rule's name and notation (survive/born/states/neighbourhood), "445  4/4/5/M" ...
 * @param[in] rule  a3_rule_t
 * @return it
 */
const char *a3_rule_name(int rule);
/**
 * @brief The running rule's states.
 * @return how many, 2..A3_STATES_MAX
 */
int a3_states(void);
/**
 * @brief Cells not dead.
 * @return how many
 */
int a3_population(void);
/**
 * @brief How often the running rule takes a generation.
 * @return ms
 */
int a3_step_ms(void);
/**
 * @brief Generations since the run began (held ones too).
 * @return how many
 */
uint32_t a3_generation(void);

/** @} */

#ifdef __cplusplus
}
#endif
