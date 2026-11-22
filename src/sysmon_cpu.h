/**
 * @file sysmon_cpu.h
 * @ingroup sysmon_cpu
 * @brief CPU load per core from the idle tasks' run time, and the chip's temperature sensor.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sysmon_cpu CPU load and temperature
 * @ingroup sysmon_core
 * @brief CPU load per core from the idle tasks' run time, and the chip's temperature sensor.
 * @{
 */

/** @brief CPU load per core from the idle tasks' run time: the previous reading, to compare with. */
typedef struct {
    uint32_t idle[2];                   /**< each core's idle task's run time then (microseconds) */
    int64_t t;                          /**< when (esp_timer, microseconds); 0 = no reading yet */
    bool primed;                        /**< a reading since sysmon_load_reset() */
} sysmon_load_t;
/**
 * @brief Forgets the previous reading: the next sysmon_load_permille() only takes one.
 * @param[in,out] s  the load reader
 */
void sysmon_load_reset(sysmon_load_t *s);
/**
 * @brief CPU load per core since the previous call, in per mille of the core.
 * @param[in,out] s        the load reader (its previous reading, updated)
 * @param[out] pm  core 0's and core 1's load, 0..1000
 * @return false on the first call (after sysmon_load_reset()): there's nothing to compare with yet
 */
bool sysmon_load_permille(sysmon_load_t *s, int pm[2]);
/**
 * @brief Keeps the temperature sensor on (the page is shown), or lets it be off between readings.
 * @param[in] on  true: on until called with false
 */
void sysmon_temp_hold(bool on);
/**
 * @brief The chip's temperature (the sensor on just for the reading, unless held on).
 * @param[out] celsius  degrees C
 * @return false if there's no sensor or no reading
 */
bool sysmon_temp_read(float *celsius);

/** @} */

#ifdef __cplusplus
}
#endif
