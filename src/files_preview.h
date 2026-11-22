/**
 * @file files_preview.h
 * @ingroup files_preview
 * @brief A puzzle file's preview: the file read and its puzzles found, one parsed at a time.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "generator.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup files_preview Puzzle file preview
 * @ingroup files_core
 * @brief A puzzle file's preview: the file read and its puzzles found, one parsed at a time.
 * @{
 */

#define FILES_PV_MAX_BYTES (512 * 1024)   /**< read at most this much of a file */
#define FILES_PV_MAX       2000           /**< puzzles listed (an import takes as many) */

#define FILES_PV_MAX_BYTES (512 * 1024)   /**< read at most this much of a file */
#define FILES_PV_MAX       2000           /**< puzzles listed (an import takes as many) */
/**
 * @brief Reads a puzzle file and sorts out its lines: the puzzles, what kinds, how many lines
 *        aren't one. OpenSudoku's XML: each data="..." (or '...') is a Classic one.
 * @param[in] path     the file
 * @param[out] summary  gets "42 puzzles: 30 Classic, 12 Killer" (+ the lines left out, or why not)
 * @param[in] n        its size
 * @return how many puzzles (files_pv_count())
 */
int files_pv_load(const char *path, char *summary, size_t n);
/** @brief Frees what files_pv_load() read (the preview is closed). */
void files_pv_free(void);
/**
 * @brief The puzzles found.
 * @return how many
 */
int files_pv_count(void);
/**
 * @brief Parses one of them into its board (files_pv_geom()).
 * @param[in] at   0 .. files_pv_count() - 1
 * @param[out] var  gets its sudoku_variant_t
 * @param[out] lv   gets its level (-1: none given)
 * @param[out] g    gets its givens (81 cells)
 * @return false if it can't be drawn
 */
bool files_pv_parse(int at, int *var, int *lv, uint8_t g[81]);
/**
 * @brief The board of the puzzle parsed last.
 * @return it (NULL before files_pv_load())
 */
const sudoku_geom_t *files_pv_geom(void);

/** @} */

#ifdef __cplusplus
}
#endif
