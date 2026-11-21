/**
 * @file sysmon_chip.h
 * @ingroup sysmon_chip
 * @brief The chip: why it started, uptime, its model, the Ethernet MAC, the task count.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sysmon_chip The chip
 * @ingroup sysmon_core
 * @brief The chip: why it started, uptime, its model, the Ethernet MAC, the task count.
 * @{
 */

/**
 * @brief Why the chip last started.
 * @return a few words: "Power-on", "Software restart", "Crash (panic)", "Brownout", ...
 */
const char *sysmon_reset_text(void);
/**
 * @brief Time since start-up.
 * @return seconds
 */
uint64_t sysmon_uptime_s(void);
/**
 * @brief The chip as one line: "ESP32-P4 v1.0, 2 cores, 360 MHz".
 * @param[out] buf  the text
 * @param n         buf's size
 */
void sysmon_chip_text(char *buf, size_t n);
/**
 * @brief The Ethernet MAC: the chip's own, from its factory MAC (eFuse).
 * @param[out] mac  6 bytes
 * @return false if it couldn't be read
 */
bool sysmon_eth_mac(uint8_t mac[6]);
/**
 * @brief How many FreeRTOS tasks there are (no lock).
 * @return the number of tasks
 */
unsigned sysmon_task_count(void);

/** @} */

#ifdef __cplusplus
}
#endif
