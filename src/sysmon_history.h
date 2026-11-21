/**
 * @file sysmon_history.h
 * @ingroup sysmon_history
 * @brief The last hour, a sample every 10 s: CPU load, temperature, free RAM, Wi-Fi signal.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sysmon_history History
 * @ingroup sysmon_core
 * @brief The last hour, a sample every 10 s: CPU load, temperature, free RAM, Wi-Fi signal.
 *
 * The last hour, a sample every 10 s, kept from start-up whether the page is open or not.
 * @{
 */

#define SYSMON_HIST_N    360            /**< samples kept: an hour */
#define SYSMON_HIST_MS   10000          /**< between samples, in ms */
#define SYSMON_NONE      INT16_MIN      /**< no value (no temperature reading; Wi-Fi not connected) */
/** @brief One sample. */
typedef struct {
    int16_t load[2];                    /**< CPU load of core 0 and core 1, % */
    int16_t temp;                       /**< temperature, degrees C x10 (SYSMON_NONE: no reading) */
    int16_t ram;                        /**< free internal RAM, KB */
    int16_t rssi;                       /**< Wi-Fi signal, dBm (SYSMON_NONE: not connected) */
} sysmon_sample_t;
/**
 * @brief Takes a sample (every SYSMON_HIST_MS) and keeps it.
 * @param[out] out  the sample (may be NULL)
 * @return false on the first call, which only takes the first idle reading (a load needs two)
 */
bool sysmon_hist_sample(sysmon_sample_t *out);
/**
 * @brief How many samples are kept.
 * @return 0 .. SYSMON_HIST_N
 */
int sysmon_hist_count(void);
/**
 * @brief A kept sample.
 * @param i         0 the oldest .. sysmon_hist_count() - 1 the newest
 * @param[out] out  the sample
 */
void sysmon_hist_get(int i, sysmon_sample_t *out);

/** @} */

#ifdef __cplusplus
}
#endif
