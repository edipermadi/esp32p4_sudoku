/**
 * @file sd_card.c
 * @ingroup sd_card
 * @brief The microSD card: the card task (probe, format), and what's known about the card.
 */
#include "sd_card.h"
#include "settings_core.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/bsp_board.h"
#include "stats_db.h"

static volatile sd_state_t s_sd_state = SD_IDLE;   /**< where the card work is (set by the task) */
static volatile bool s_sd_present;              /**< a card was found */
static uint64_t s_sd_bytes;                     /**< its size in bytes */
static char s_sd_name[8];                       /**< its product name */
static esp_err_t s_sd_err;                      /**< why formatting failed */

void sd_probe_card(void)
{
    s_sd_present = bsp_sdcard_probe(&s_sd_bytes, s_sd_name) == ESP_OK;
}

/**
 * @brief The card task: one job (sd_job_t), then s_sd_state is its result.
 * @param arg  the job, cast to a pointer
 */
static void sd_task(void *arg)
{
    sd_job_t job = (sd_job_t)(intptr_t)arg;
    esp_err_t err;
    switch (job) {
    case SD_JOB_PROBE:
        err = bsp_sdcard_probe(&s_sd_bytes, s_sd_name);
        s_sd_present = err == ESP_OK;
        s_sd_state = s_sd_present ? SD_FOUND : SD_NO_CARD;
        break;
    case SD_JOB_FORMAT:
        stats_db_suspend();                        /* close sudoku.db, unmount */
        err = bsp_sdcard_check();                  /* still the card found earlier? */
        if (err == ESP_OK) {
            err = bsp_sdcard_format_fat32();
        }
        s_sd_err = err;
        if (err == ESP_OK) {
            stats_db_start_fresh();    /* new sudoku.db: current settings, no statistics */
        }
        s_sd_state = err == ESP_OK ? SD_DONE : SD_FAILED;
        break;
    }
    vTaskDelete(NULL);
}

bool sd_run(sd_job_t job)
{
    s_sd_state = job == SD_JOB_PROBE ? SD_PROBING : SD_FORMATTING;
    if (xTaskCreatePinnedToCore(sd_task, "sd_card", 8192, (void *)(intptr_t)job, 4, NULL, SETTINGS_TASK_CORE) != pdPASS) {
        s_sd_state = SD_IDLE;
        return false;
    }
    return true;
}

sd_state_t sd_state(void) { return s_sd_state; }
void sd_set_state(sd_state_t st) { s_sd_state = st; }
esp_err_t sd_last_error(void) { return s_sd_err; }
bool sd_present(void) { return s_sd_present; }
void sd_forget(void) { s_sd_present = false; }
uint64_t sd_bytes(void) { return s_sd_bytes; }
const char *sd_name(void) { return s_sd_name; }

void fmt_gb(char *buf, size_t n, uint64_t bytes)
{
    uint32_t tenths = (uint32_t)((bytes + 50000000ULL) / 100000000ULL);   /* GB as printed on cards */
    snprintf(buf, n, "%lu.%lu GB", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
}
