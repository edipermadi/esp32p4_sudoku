/**
 * @file sysmon_core.h
 * @ingroup sysmon_core
 * @brief The system monitor's numbers, without the screen: the chip, CPU load, temperature, memory,
 *        storage, the task list and the last hour's history.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sysmon_core System monitor core
 * @brief What the system monitor (ui_sysmon.c) shows, gathered without LVGL.
 *
 * Everything here only reads: heap counters, the partition table, NVS's in-RAM entry counts,
 * FreeRTOS run-time stats (esp_timer based, enabled in the core's sdkconfig). No flash writes (they
 * make the panel blink).
 *
 * Nothing here may switch interrupts off on the LVGL core (core 1, which also serves the display)
 * for long: the whole screen blinks cyan. Measured: the largest free PSRAM block
 * (heap_caps_get_largest_free_block walks the 28 MB heap under its lock) 4 ms, uxTaskGetSystemState
 * 1.5 ms; once a second, either made the screen blink (2026-11-20). So: no PSRAM largest block at
 * all, CPU load from the idle tasks' counters, and the full task list read by a task on core 0,
 * whose interrupts the display doesn't use. The microSD free space is counted by a short-lived task
 * too: FatFs may scan the whole FAT the first time, seconds on a big card.
 * @{
 */

/** @name The chip
 *  @{ */
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

/** @name CPU load and temperature
 *  @{ */
/** @brief CPU load per core from the idle tasks' run time: the previous reading, to compare with. */
typedef struct {
    uint32_t idle[2];                   /**< each core's idle task's run time then (microseconds) */
    int64_t t;                          /**< when (esp_timer, microseconds); 0 = no reading yet */
    bool primed;                        /**< a reading since sysmon_load_reset() */
} sysmon_load_t;
/**
 * @brief Forgets the previous reading: the next sysmon_load_permille() only takes one.
 * @param s  the load reader
 */
void sysmon_load_reset(sysmon_load_t *s);
/**
 * @brief CPU load per core since the previous call, in per mille of the core.
 * @param s        the load reader (its previous reading, updated)
 * @param[out] pm  core 0's and core 1's load, 0..1000
 * @return false on the first call (after sysmon_load_reset()): there's nothing to compare with yet
 */
bool sysmon_load_permille(sysmon_load_t *s, int pm[2]);
/**
 * @brief Keeps the temperature sensor on (the page is shown), or lets it be off between readings.
 * @param on  true: on until called with false
 */
void sysmon_temp_hold(bool on);
/**
 * @brief The chip's temperature (the sensor on just for the reading, unless held on).
 * @param[out] celsius  degrees C
 * @return false if there's no sensor or no reading
 */
bool sysmon_temp_read(float *celsius);
/** @} */

/** @name Memory
 *  @{ */
/** @brief A heap's numbers. */
typedef struct {
    size_t total;                       /**< its size in bytes (0: no such heap) */
    size_t free;                        /**< free now */
    size_t min_free;                    /**< the least it has had free (with_min only) */
    size_t largest;                     /**< the largest free block (with_min only) */
} sysmon_heap_t;
/**
 * @brief A heap's size and free space.
 * @param caps      which heap: MALLOC_CAP_INTERNAL, MALLOC_CAP_SPIRAM, ...
 * @param with_min  also the low-water mark and the largest free block. Not for PSRAM: its low-water
 *                  mark reads 0 from boot on (the core registers it that way), and its largest block
 *                  walks the whole 28 MB heap with interrupts off (4 ms: the screen blinks).
 * @param[out] out  the numbers
 */
void sysmon_heap(uint32_t caps, bool with_min, sysmon_heap_t *out);
/** @} */

/** @name Storage
 *  @{ */
/** @brief The flash, read once (it doesn't change while running). */
typedef struct {
    uint32_t flash_size;                /**< the chip's size in bytes */
    uint32_t app_len;                   /**< the running firmware's size */
    uint32_t app_part;                  /**< its slot's size */
    uint32_t part_end;                  /**< where the last partition ends: the flash partitioned */
    const char *run_label;              /**< the running slot's name (app0, app1); NULL if unknown */
    uint32_t run_addr;                  /**< its offset in the flash */
} sysmon_flash_t;
/**
 * @brief The flash's numbers (read on the first call).
 * @return the numbers
 */
const sysmon_flash_t *sysmon_flash(void);
/**
 * @brief NVS (the settings kept on the board): its entries in use.
 * @param[out] used   entries used
 * @param[out] total  entries there are
 * @return false if NVS can't say
 */
bool sysmon_nvs(uint32_t *used, uint32_t *total);
/** @brief Counts the microSD card's free space again, in a task (only while sudoku.db has it mounted). */
void sysmon_sd_query(void);
/**
 * @brief The microSD card's FAT volume, as last counted (sysmon_sd_query()).
 * @param[out] total   its size in bytes
 * @param[out] free_b  free bytes
 * @param[out] err     ESP_OK, ESP_ERR_INVALID_STATE (not mounted: no card, not FAT, or lent over USB),
 *                     or why counting failed
 * @return false while nothing has been counted yet
 */
bool sysmon_sd(uint64_t *total, uint64_t *free_b, esp_err_t *err);
/**
 * @brief The whole card's size (all its sectors, not just the volume).
 * @return bytes
 */
uint64_t sysmon_sd_card_bytes(void);
/** @} */

/** @name Tasks
 *  The FreeRTOS task list with each task's share of a core since the previous list. It's read by
 *  a task on core 0 (uxTaskGetSystemState holds the kernel lock with interrupts off for ~1.5 ms),
 *  so a list is a call old: sysmon_tasks_poll() takes the last one and asks for the next.
 *  @{ */
/** @brief The task table's columns, and the orders it can be sorted in. */
enum {
    SYSMON_COL_NAME = 0,                /**< the task's name, A-Z */
    SYSMON_COL_CORE,                    /**< the core it's pinned to (unpinned after the cores) */
    SYSMON_COL_PRIO,                    /**< its priority */
    SYSMON_COL_STATE,                   /**< running, ready, waiting ... */
    SYSMON_COL_CPU,                     /**< its share of a core */
    SYSMON_COL_STACK,                   /**< the least stack it has had free */
    SYSMON_COLS,                        /**< how many columns */
};
/**
 * @brief Takes the list read last (if there's a new one: its CPU shares worked out, sorted) and asks
 *        for the next.
 * @return true if there's a new list to show
 */
bool sysmon_tasks_poll(void);
/** @brief CPU shares from the next list on (the Tasks tab was just opened: the last list is old). */
void sysmon_tasks_restart(void);
/**
 * @brief Sorts the list (now, and the ones after it).
 * @param col   a SYSMON_COL_ column (NAME, CORE, CPU and STACK sort)
 * @param desc  highest first
 */
void sysmon_tasks_sort(int col, bool desc);
/**
 * @brief The column the list is sorted by.
 * @return a SYSMON_COL_ value (SYSMON_COL_CPU at first)
 */
int sysmon_tasks_sort_col(void);
/**
 * @brief The direction the list is sorted in.
 * @return true: highest first
 */
bool is_sysmon_tasks_sort_desc(void);
/**
 * @brief How many tasks the list has.
 * @return the number of rows
 */
int sysmon_tasks_count(void);
/**
 * @brief A row of the list, in its order.
 * @param r              the row, 0 .. sysmon_tasks_count() - 1
 * @param[out] permille  the task's share of a core, 0..1000 (if is_sysmon_tasks_cpu_valid())
 * @return the task (its name, core, priority, state, stack)
 */
const TaskStatus_t *sysmon_task(int r, uint32_t *permille);
/**
 * @brief The CPU shares are there: the list had one before it to compare with.
 * @return true if the permille values mean something
 */
bool is_sysmon_tasks_cpu_valid(void);
/** @} */

/** @name History
 *  The last hour, a sample every 10 s, kept from start-up whether the page is open or not.
 *  @{ */
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

/** @} */

#ifdef __cplusplus
}
#endif
