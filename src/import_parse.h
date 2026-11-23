/**
 * @file import_parse.h
 * @ingroup import_parse
 * @brief Reading puzzle files to import, without the database (the import itself: stats_db_import.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup import_parse Import parsing
 * @ingroup topic_puzzles
 * @brief Classic puzzles read from a file: one per line (81 digits, '.' or '0' empty) or OpenSudoku's XML.
 * @{
 */

/**
 * @brief 81 cells from text.
 * @param[in] p    the text (digits, '.' or '0' = empty)
 * @param[out] out  gets the cells
 * @return false if it isn't a puzzle
 */
bool import_parse_cells(const char *p, uint8_t out[81]);
/**
 * @brief The puzzles of one file.
 * @param[in] path   the file
 * @param[in] xml    OpenSudoku's XML: the puzzles are data="..." (or '...') attributes; else a puzzle
 *               per line
 * @param[out] cells  gets them
 * @param[in] max    puzzles at most
 * @return how many
 */
int import_parse_file(const char *path, bool xml, uint8_t (*cells)[81], int max);
/**
 * @brief Whether a name ends with a suffix (any case).
 * @param[in] s       the name
 * @param[in] suffix  the suffix, e.g. ".sdm"
 * @return true: it does
 */
bool is_import_name_ends_with(const char *s, const char *suffix);

/** @} */

#ifdef __cplusplus
}
#endif
