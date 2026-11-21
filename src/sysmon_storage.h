/**
 * @file sysmon_storage.h
 * @ingroup sysmon_storage
 * @brief The flash (sizes, the running slot), NVS entries, the microSD card's free space.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sysmon_storage Storage
 * @ingroup sysmon_core
 * @brief The flash (sizes, the running slot), NVS entries, the microSD card's free space.
 * @{
 */

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

#ifdef __cplusplus
}
#endif
