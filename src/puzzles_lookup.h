/**
 * @file puzzles_lookup.h
 * @ingroup puzzles_lookup
 * @brief Any puzzle by its id: the generated-puzzle cache, a puzzle's givens and board, without LVGL (the
 *        collections and puzzle names: collections.h).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "generator.h"
#include "puzzles.h"
#include "stats_db.h"
#include "collections.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup puzzles_lookup Puzzle lookup
 * @ingroup topic_puzzles
 * @brief A puzzle by its id, built-in or generated: its givens, variant, level and board. The puzzles made on
 *        the board are looked up in sudoku.db and the last GEN_CACHE kept here (without a card, only here).
 *        Also defines the collections' functions (collections.h).
 * @{
 */

/** @brief The puzzles made on the board live in sudoku.db (stats_db_gen_*); the last
 * few are cached here, and without a card that's the only place they are. */
#define GEN_CACHE 24
/** @brief A puzzle made on the board, cached: its board's extras, where it came from. */
typedef struct {
    uint16_t id;                        /**< 0 = free slot */
    uint8_t variant;                    /**< sudoku_variant_t */
    uint8_t level;                      /**< gen_level_t */
    uint8_t regions[81];                /**< Jigsaw: the region map */
    char cages[STATS_CAGES_LEN];        /**< Killer: the cages (sudoku_cages_text()), "" = none */
    char marks[82];                     /**< Even-Odd / Consecutive: the marks (sudoku_marks_text()), "" = none */
    char thermos[STATS_THERMOS_LEN];    /**< Thermo: the thermometers (sudoku_thermos_text()), "" = none */
    char source[STATS_IMPORT_NAME];     /**< a puzzle typed in: its collection ("My puzzles"), "" = none */
    int number;                         /**< and its number there */
    uint8_t givens[81];                 /**< the puzzle, 0 = empty */
    char day[12];                       /**< daily puzzle's date, "" otherwise */
} gen_cache_t;

/**
 * @brief The generated-puzzle cache (GEN_CACHE entries).
 * @return it
 */
const gen_cache_t *ui_gen_cache(void);
/**
 * @brief Sets the cache's buffer (made on first use).
 * @param[in] v  it
 */
void ui_set_gen_cache(gen_cache_t *v);
/**
 * @brief The saved collections (Puzzles > Saved): sets how many there are.
 * @param[in] v  how many
 */
void ui_set_nsaved(int v);
/**
 * @brief the next RAM-only puzzle id (no card: they last until power-off)
 * @return the id
 */
uint16_t gen_ram_take(void);
/**
 * @brief Caches a generated puzzle.
 * @param[in] id  the puzzle
 * @param[in] variant  sudoku_variant_t
 * @param[in] level  gen_level_t
 * @param[in] givens  the puzzle
 * @param[in] regions  Jigsaw's regions, or NULL
 * @param[in] cages  Killer's cages, or NULL
 * @param[in] marks  the marks, or NULL
 * @param[in] thermos  the lines, or NULL
 * @param[in] day  a daily puzzle's date, or NULL
 */
void gen_cache_put(uint16_t id, int variant, int level, const uint8_t givens[81], const uint8_t *regions, const char *cages, const char *marks, const char *thermos, const char *day);
/**
 * @brief Notes where a cached puzzle came from (a puzzle typed in).
 * @param[in] id  the puzzle
 * @param[in] source  its collection
 * @param[in] number  its number there
 */
void gen_cache_source(uint16_t id, const char *source, int number);
/**
 * @brief A generated puzzle, and its date if it is a daily one.
 * @param[in] id  the puzzle
 * @param[out] givens  gets it, or NULL
 * @param[out] variant  gets it, or NULL
 * @param[out] level  gets it, or NULL
 * @param[out] day  gets its date ("" if not a daily one)
 * @return false: not found
 */
bool gen_lookup_day(uint16_t id, uint8_t *givens, int *variant, int *level, char day[12]);
/**
 * @brief The board of any puzzle: a fixed variant's geometry, or for a Jigsaw
 * puzzle its regions built into buf. *variant (may be NULL) is set too.
 * @param[in] id  the puzzle
 * @param[out] buf  room for a Jigsaw board
 * @param[out] variant  gets the variant, or NULL
 * @return its board
 */
const sudoku_geom_t *puzzle_geom(uint16_t id, sudoku_geom_t *buf, int *variant);
/**
 * @brief the givens of any puzzle, built-in or generated
 * @param[in] id  the puzzle
 * @param[out] cells  gets the givens
 * @return false: not found
 */
bool puzzle_get(uint16_t id, uint8_t cells[81]);
/**
 * @brief A daily puzzle kept only in the cache (no card), by its date.
 * @param[in] day  "YYYY-MM-DD"
 * @return its id, 0 if none
 */
uint16_t gen_cache_find_day(const char *day);

/** @} */

#ifdef __cplusplus
}
#endif
