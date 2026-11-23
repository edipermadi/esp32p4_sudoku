/**
 * @file ant_core.h
 * @ingroup ant_core
 * @brief Langton's ant for the screen saver, without LVGL (drawn by ui_saver_ant.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ant_core Ant core
 * @ingroup ui_sudoku
 * @brief Langton's ant on an ANT_N x ANT_N grid whose edges wrap around: each step the ant turns by
 *        the rule for the state of the cell it is on (L or R), moves that cell on to its next state,
 *        and steps forward. The classic rule "RL" (two states) wanders chaotically for about 10,000
 *        steps and then builds a straight diagonal "highway"; longer rules (more states) build other
 *        shapes. Every run picks a rule from a list; after ANT_RUN steps the grid is cleared and
 *        another starts.
 * @{
 */

#define ANT_N           120                   /**< cells across and down */
#define ANT_CELLS       (ANT_N * ANT_N)       /**< cells */
#define ANT_STATES_MAX  12                    /**< a rule's states at most */
#define ANT_RUN         60000                 /**< steps before the grid is cleared and another starts */

/**
 * @brief A new run: the grid cleared, the ant in the middle facing up, a rule picked at random. The
 *        grid's memory (from malloc(): PSRAM on the board) is taken here if it isn't yet.
 * @param[in] seed  the random seed (any value; the same seed gives the same runs)
 * @return false if there was no memory for the grid
 */
bool ant_start(uint32_t seed);
/** @brief Gives the grid's memory back (ant_start() takes it again). */
void ant_free(void);
/**
 * @brief A run with a given rule (for the tests): the grid cleared, the ant in the middle facing up.
 * @param[in] rule  "RL", "LLRR" ...: a letter a state, 2..ANT_STATES_MAX of them
 */
void ant_set_rule(const char *rule);
/**
 * @brief One step of the ant; after ANT_RUN steps, a new run instead (another rule, the grid cleared).
 * @param[out] changed  gets the cell the step changed (y * ANT_N + x), or -1 for a new run (every cell)
 * @return true if it was a new run
 */
bool ant_step(int *changed);
/**
 * @brief A cell's state.
 * @param[in] x  column, 0..ANT_N-1
 * @param[in] y  row, 0..ANT_N-1
 * @return 0 .. ant_states()-1
 */
uint8_t ant_cell(int x, int y);
/**
 * @brief Where the ant is.
 * @param[out] x  gets its column
 * @param[out] y  gets its row
 */
void ant_pos(int *x, int *y);
/**
 * @brief The rule running.
 * @return its letters ("RL" ...)
 */
const char *ant_rule(void);
/**
 * @brief The rule's states.
 * @return how many, 2..ANT_STATES_MAX
 */
int ant_states(void);
/**
 * @brief Steps since the run began.
 * @return how many
 */
uint32_t ant_steps(void);

/** @} */

#ifdef __cplusplus
}
#endif
