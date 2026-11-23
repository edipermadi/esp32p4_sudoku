/**
 * @file sysmon_format.h
 * @ingroup sysmon_format
 * @brief The system monitor's numbers as text.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sysmon_format Formatting
 * @ingroup sysmon_core
 * @brief The system monitor's numbers as text.
 * @{
 */

/**
 * @brief A size in B, KB, MB or GB (1024s), one or two decimals.
 * @param[out] buf  the text
 * @param[in] n         buf's size
 * @param[in] b         the size in bytes
 */
void fmt_bytes(char *buf, size_t n, uint64_t b);

/** @} */

#ifdef __cplusplus
}
#endif
