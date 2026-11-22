/**
 * @file trophies_text.h
 * @ingroup trophies_text
 * @brief Texts the Trophies screen shows: an achievement's progress, the unlock toast.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "stats_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup trophies_text Texts
 * @ingroup trophies_core
 * @brief Texts the Trophies screen shows: an achievement's progress, the unlock toast.
 * @{
 */

/**
 * @brief An achievement's progress as text: "3 / 10", "2 h 5 min / 10 h", "4 / 7 days".
 * @param[in] a    the achievement
 * @param[out] buf  gets the text
 * @param[in] n    its size
 */
void trophies_ach_progress_text(const stats_ach_t *a, char *buf, size_t n);
/**
 * @brief The achievements the last solve unlocked, as the toast's text
 *        ("Achievement unlocked:" and the titles on the next line).
 * @param[out] text  gets it
 * @param[in] n     its size
 * @return false if none
 */
bool trophies_take_unlocked(char *text, size_t n);

/** @} */

#ifdef __cplusplus
}
#endif
