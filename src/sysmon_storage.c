/**
 * @file sysmon_storage.c
 * @ingroup sysmon_storage
 * @brief The flash (sizes, the running slot), NVS entries, the microSD card's free space.
 */
#include "sysmon_storage.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "bsp/bsp_board.h"
#include "stats_db.h"

static sysmon_flash_t s_flash;                  /**< the flash's numbers */
static bool s_flash_read;                       /**< s_flash has been read */

/* (documented in sysmon_storage.h) */
const sysmon_flash_t *sysmon_flash(void)
{
    if (s_flash_read) {
        return &s_flash;
    }
    s_flash_read = true;
    esp_flash_get_size(NULL, &s_flash.flash_size);
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run) {
        s_flash.app_part = run->size;
        s_flash.run_label = run->label;
        s_flash.run_addr = run->address;
        esp_partition_pos_t pos = {.offset = run->address, .size = run->size};
        esp_image_metadata_t md;
        if (esp_image_get_metadata(&pos, &md) == ESP_OK) {
            s_flash.app_len = md.image_len;
        }
    }
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p->address + p->size > s_flash.part_end) {
            s_flash.part_end = p->address + p->size;
        }
    }
    esp_partition_iterator_release(it);
    return &s_flash;
}

/* (documented in sysmon_storage.h) */
bool sysmon_nvs(uint32_t *used, uint32_t *total)
{
    nvs_stats_t ns;
    if (nvs_get_stats(NULL, &ns) != ESP_OK || !ns.total_entries) {
        return false;
    }
    *used = (uint32_t)ns.used_entries;
    *total = (uint32_t)ns.total_entries;
    return true;
}

static volatile bool s_sd_busy;                 /**< the counting task runs */
static volatile bool s_sd_valid;                /**< something has been counted */
static volatile esp_err_t s_sd_err;             /**< the last count's result */
static volatile uint64_t s_sd_total;            /**< the volume's size */
static volatile uint64_t s_sd_free;             /**< its free bytes */

/** @brief Counts the card's free space (FatFs may scan the whole FAT: seconds), then ends. @param[in] arg unused */
static void sd_task(void *arg)
{
    uint64_t total = 0, free_b = 0;
    esp_err_t err = esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total, &free_b);
    s_sd_total = total;
    s_sd_free = free_b;
    s_sd_err = err;
    s_sd_valid = true;
    s_sd_busy = false;
    vTaskDelete(NULL);
}

/* (documented in sysmon_storage.h) */
void sysmon_sd_query(void)
{
    if (s_sd_busy) {
        return;
    }
    /* only while sudoku.db has the card mounted (not while it's lent over USB) */
    if (!is_stats_db_ready()) {
        s_sd_err = ESP_ERR_INVALID_STATE;
        s_sd_valid = true;
        return;
    }
    s_sd_busy = true;
    if (xTaskCreatePinnedToCore(sd_task, "sysmon_sd", 4096, NULL, 2, NULL, 0) != pdPASS) {
        s_sd_busy = false;
    }
}

/* (documented in sysmon_storage.h) */
bool sysmon_sd(uint64_t *total, uint64_t *free_b, esp_err_t *err)
{
    if (!s_sd_valid) {
        return false;
    }
    *total = s_sd_total;
    *free_b = s_sd_free;
    *err = s_sd_err;
    return true;
}

/* (documented in sysmon_storage.h) */
uint64_t sysmon_sd_card_bytes(void)
{
    return (uint64_t)bsp_sdcard_sector_count() * bsp_sdcard_sector_size();
}
