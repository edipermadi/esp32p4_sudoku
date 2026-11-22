/**
 * @file files_names.h
 * @ingroup files_names
 * @brief Names in the folder shown: the game's database protected, a new name checked, rename, sizes,
 *        file types.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup files_names Names
 * @ingroup files_core
 * @brief Names in the folder shown: the game's database protected, a new name checked, rename, sizes,
 *        file types.
 * @{
 */

/**
 * @brief Whether a name in the folder shown is the game's database or one of SQLite's files next to
 *        it (only in the card's root). Deleting one loses data; a stray file renamed to a
 *        journal's name would be "rolled back" into the database at the next open and corrupt it.
 * @param[in] name  the name (FAT ignores case)
 * @return true: protected
 */
bool is_files_protected(const char *name);
/**
 * @brief Why a new name can't be used in the folder shown.
 * @param[in] name  the new name
 * @param[in] old   the entry's name now
 * @return a sentence, or "" if it can
 */
const char *files_bad_name(const char *name, const char *old);
/**
 * @brief Renames an entry of the folder shown (check the name with files_bad_name() first).
 * @param[in] old   its name
 * @param[in] name  the new name
 * @param[out] err   gets why not, on false
 * @param[in] n     its size
 * @return false if it couldn't be renamed
 */
bool files_rename(const char *old, const char *name, char *err, size_t n);
/**
 * @brief A size as text: "512 B", "3.4 KB", "12.0 MB".
 * @param[out] buf  gets it
 * @param[in] n    its size
 * @param[in] b    bytes
 */
void files_fmt_size(char *buf, size_t n, uint32_t b);
/**
 * @brief Whether a name ends with a suffix (any case).
 * @param[in] s       the name
 * @param[in] suffix  the suffix, e.g. ".sdm"
 * @return true: it does
 */
bool is_files_name_ending(const char *s, const char *suffix);
/**
 * @brief Whether a file is a puzzle file the preview opens (.sdm, .sdx, .opensudoku).
 * @param[in] name  its name
 * @return true: it is
 */
bool is_files_puzzle_file(const char *name);

/** @} */

#ifdef __cplusplus
}
#endif
