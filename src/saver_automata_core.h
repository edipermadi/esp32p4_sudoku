/**
 * @file saver_automata_core.h
 * @ingroup saver_automata_core
 * @brief Cellular automata (and Langton's ant) for the screen savers, without LVGL (drawn by
 *        ui_saver_automata.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup saver_automata_core Automata core
 * @ingroup ui_sudoku
 * @brief The cellular automata screen savers, each one rule on an AUTO_N x AUTO_N board whose edges wrap
 *        around, started again from its own random start when its run ends (and when it dies out,
 *        settles, or grows too quiet, as the rule says). The rules are of five kinds:
 *        - Generations rules (Life-like ones too): a cell is dead (0), alive (1) or dying (2..states-1);
 *          a dead cell is born with a number of live neighbours in its birth set, a live one stays with
 *          one in its survive set, else starts dying, a dying one goes on to dead. Game of Life is
 *          B3/S23, Brian's Brain B2/S with 3 states.
 *        - Cyclic rules: a cell in state s moves on to s+1 (mod states) when at least a threshold of its
 *          neighbours within a range are in s+1; random noise turns into spirals.
 *        - An excitable medium (Greenberg-Hastings): a resting cell (0) is excited (1) when enough
 *          neighbours are, or now and then by itself (a spark); an excited one goes through refractory
 *          states back to rest. Each spark sends out a ring; rings meet and annihilate.
 *        - Forest fire: trees grow on empty ground at random, lightning strikes now and then, fire spreads
 *          to the trees next to it, and burnt ground is empty again.
 *        - Langton's ant: an ant on the board turns by the state of its cell (a letter of the rule, L or
 *          R), moves that cell on to its next state and steps forward, many steps a generation.
 *        Each cell also has a key for drawing (auto_key()): its state, a just-born mark, the ant, or the
 *        fading of a cell gone dead.
 * @{
 */

#define AUTO_N          120                   /**< cells across and down */
#define AUTO_CELLS      (AUTO_N * AUTO_N)     /**< cells on the board */
#define AUTO_FADE       4                     /**< keys 0..AUTO_FADE-1: a dead cell fading out (0 unlit) */
#define AUTO_STATES_MAX 16                    /**< a rule's states at most */
#define AUTO_KEY_ANT    (AUTO_FADE + AUTO_STATES_MAX)   /**< the key of the ant's cell */
#define AUTO_KEYS       (AUTO_KEY_ANT + 1)    /**< keys: fading, AUTO_FADE + state, the ant */
#define AUTO_SETTLE     16                    /**< a settling rule: the board again within this many generations */

/** @brief A rule's kind. */
typedef enum {
    AUTO_GENERATIONS = 0,                 /**< birth / survive / states (Life-like with 2 states) */
    AUTO_CYCLIC,                          /**< cyclic: on to the next state with enough neighbours in it */
    AUTO_EXCITABLE,                       /**< Greenberg-Hastings excitable medium */
    AUTO_FOREST,                          /**< forest fire: 0 ground, 1 tree, 2 fire */
    AUTO_TURMITE                          /**< Langton's ant */
} auto_kind_t;

/** @brief The rules, as auto_rule_name() names them. */
typedef enum {
    AUTO_R_LIFE = 0,                      /**< Conway's Game of Life, B3/S23 */
    AUTO_R_BRAIN,                         /**< Brian's Brain, B2/S/C3 */
    AUTO_R_ANT,                           /**< Langton's ant, RL */
    AUTO_R_CYCLIC,                        /**< cyclic spirals, R3/T5/C8 */
    AUTO_R_RPS,                           /**< rock, paper, scissors, R1/T3/C3 */
    AUTO_R_EXCITABLE,                     /**< excitable medium (Greenberg-Hastings) */
    AUTO_R_DAY_NIGHT,                     /**< Day & Night, B3678/S34678 */
    AUTO_R_SEEDS,                         /**< Seeds, B2/S */
    AUTO_R_MAZE,                          /**< Maze, B3/S12345 */
    AUTO_R_STAR_WARS,                     /**< Star Wars, S345/B2/C4 */
    AUTO_R_FOREST,                        /**< forest fire */
    AUTO_R_COUNT                          /**< how many */
} auto_rule_t;

/**
 * @brief Starts a rule: the random numbers seeded, the board's memory (about 60 KB, from malloc(): PSRAM
 *        on the board) taken if it isn't yet, and the rule's random start.
 * @param[in] seed  the random seed (any value; the same seed gives the same runs)
 * @param[in] rule  auto_rule_t
 * @return false if there was no memory for the board
 */
bool auto_start(uint32_t seed, int rule);
/** @brief Gives the board's memory back (auto_start() takes it again). */
void auto_free(void);
/**
 * @brief The rule started again from its own random start (or another rule, for the tests).
 * @param[in] rule  auto_rule_t
 */
void auto_set_rule(int rule);
/** @brief Clears the board: every cell 0 and unlit, the ant in the middle facing up (for the tests). */
void auto_clear(void);
/**
 * @brief Sets a cell's state (for the tests).
 * @param[in] x  column, 0..AUTO_N-1
 * @param[in] y  row, 0..AUTO_N-1
 * @param[in] st  0..auto_states()-1
 */
void auto_set(int x, int y, int st);
/**
 * @brief One generation (for Langton's ant, its steps a generation); the rule started again instead when
 *        its run has ended, or it has died out, settled or grown too quiet.
 * @return true if it started again (every cell to draw again)
 */
bool auto_step(void);
/**
 * @brief A cell's state.
 * @param[in] x  column, 0..AUTO_N-1
 * @param[in] y  row, 0..AUTO_N-1
 * @return 0..auto_states()-1
 */
int auto_state(int x, int y);
/**
 * @brief A cell's key for drawing: 0..AUTO_FADE-1 a dead cell fading (0 unlit), AUTO_FADE + state a live
 *        one (for a Generations rule, AUTO_FADE + 0 is a cell just born; for a cyclic rule every state is
 *        AUTO_FADE + state), AUTO_KEY_ANT the ant.
 * @param[in] x  column, 0..AUTO_N-1
 * @param[in] y  row, 0..AUTO_N-1
 * @return 0..AUTO_KEYS-1
 */
int auto_key(int x, int y);
/**
 * @brief Where the ant is (Langton's ant).
 * @param[out] x  gets its column
 * @param[out] y  gets its row
 */
void auto_ant(int *x, int *y);
/**
 * @brief The rule running.
 * @return auto_rule_t
 */
int auto_rule(void);
/**
 * @brief A rule's name and notation ("Day & Night  B3678/S34678" ...).
 * @param[in] rule  auto_rule_t
 * @return it
 */
const char *auto_rule_name(int rule);
/**
 * @brief The running rule's kind.
 * @return auto_kind_t
 */
int auto_kind(void);
/**
 * @brief The running rule's states.
 * @return how many, 2..AUTO_STATES_MAX
 */
int auto_states(void);
/**
 * @brief How often the running rule takes a generation.
 * @return ms
 */
int auto_step_ms(void);
/**
 * @brief Generations (for Langton's ant, steps) since the run began.
 * @return how many
 */
uint32_t auto_generation(void);

/** @} */

#ifdef __cplusplus
}
#endif
