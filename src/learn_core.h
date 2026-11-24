/**
 * @file learn_core.h
 * @ingroup learn_core
 * @brief Learn's lessons and the search for a puzzle to practise each one on, without LVGL (the dialog and the
 *        practice game: ui_learn.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup learn_core Learn core
 * @ingroup topic_game
 * @brief Learn (Puzzles > ⋮ > Learn): its lessons, and a position to practise each one.
 *
 * Two kinds of lesson:
 * - **techniques** (LEARN_HIDDEN .. LEARN_XY_WING), on the built-in Classic puzzles: a puzzle is played forward
 *   with the hint engine (sudoku_next_step()) until the next step is that technique (the four rarest:
 *   until the singles run out and that technique opens one up, sudoku_step_with());
 * - **variant rules** (LEARN_RULES ..), one per variant but Classic, Mini 6x6, Mini 8x8 and Kids 4x4 (which only change
 *   the grid): new puzzles of that variant are made (Medium) and played forward until the next step needs its
 *   rule. For a variant whose rule is a technique of the hint engine's (a cage, a line, a clue, the marks
 *   between cells), that technique; for one whose rule only adds regions or cells that see each other (X-Sudoku,
 *   Hyper, Jigsaw, Disjoint Groups, Asterisk, Center Dot, Argyle, Anti-Knight, Anti-King, Even-Odd), a single
 *   that the same digits on a Classic board wouldn't give.
 * @{
 */

/** @brief Learn's technique lessons, then the variant rules from LEARN_RULES (learn_count() in all). */
enum {
    LEARN_HIDDEN = 0,                   /**< hidden single */
    LEARN_NAKED,                        /**< naked single */
    LEARN_POINTING,                     /**< pointing */
    LEARN_CLAIMING,                     /**< claiming */
    LEARN_NAKED_PAIR,                   /**< naked pair */
    LEARN_HIDDEN_PAIR,                  /**< hidden pair */
    LEARN_NAKED_TRIPLE,                 /**< naked triple */
    LEARN_X_WING,                       /**< X-Wing */
    LEARN_SWORDFISH,                    /**< Swordfish */
    LEARN_XY_WING,                      /**< XY-Wing */
    LEARN_RULES                         /**< the first variant rule lesson */
};

/**
 * @brief How many lessons there are.
 * @return LEARN_RULES + the variant rules
 */
int learn_count(void);
/**
 * @brief A lesson's name ("Naked pair", "Kropki dots").
 * @param[in] k  the lesson, 0..learn_count()-1
 * @return it
 */
const char *learn_name(int k);
/**
 * @brief What the lesson's technique or rule does, in a sentence.
 * @param[in] k  the lesson
 * @return it
 */
const char *learn_desc(int k);
/** @brief The Learn dialog's tabs (learn_group()): the techniques, then the variant rules as New puzzle sorts them. */
enum { LEARN_TECHNIQUES = 0, LEARN_GRIDS, LEARN_MARKS, LEARN_LINES, LEARN_CLUES, LEARN_GROUPS };
/**
 * @brief A lesson's tab.
 * @param[in] k  the lesson
 * @return LEARN_TECHNIQUES .. LEARN_CLUES
 */
int learn_group(int k);
/**
 * @brief A tab's name ("Techniques", "Grids", ...).
 * @param[in] g  LEARN_TECHNIQUES .. LEARN_CLUES
 * @return it
 */
const char *learn_group_name(int g);
/**
 * @brief A variant rule lesson's variant.
 * @param[in] k  the lesson
 * @return VAR_*, or -1 for a technique lesson (Classic, built-in puzzles)
 */
int learn_variant(int k);

/** @brief A position to practise a lesson on (learn_find()). */
typedef struct {
    uint16_t puzzle;                    /**< a technique lesson: the built-in puzzle */
    uint8_t givens[81];                 /**< a variant rule lesson: the puzzle made (its board: learn_find()'s) */
    uint8_t vals[81];                   /**< the position: the givens and the digits placed so far */
    sudoku_step_t step;                 /**< the next step, the one the lesson shows */
} learn_found_t;

/**
 * @brief Looks for a position where the lesson's technique or rule is the next step. Takes a while (the board:
 *        a few seconds for a variant, which runs the generator; call it off the UI task).
 * @param[in] k  the lesson
 * @param[in] seed  any random number (the same one finds the same position)
 * @param[out] board  gets a variant rule lesson's board (PSRAM on the board); untouched for a technique lesson
 * @param[out] out  gets the position
 * @return false: none found this time (try again with another seed)
 */
bool learn_find(int k, uint32_t seed, sudoku_geom_t *board, learn_found_t *out);

/** @} */

#ifdef __cplusplus
}
#endif
