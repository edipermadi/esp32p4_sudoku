/* What Settings does to the board, without the screens (settings_core.h). */
#include "settings_core.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "stats_db.h"
#include "store.h"
#include "usb_msc.h"
#include "wifi_mgr.h"

#define WORKER_CORE 0                               /* (as ui_internal.h: away from LVGL's core) */

/* ------------------------------------------------------------- firmware update */

static volatile fw_state_t fw_state = FW_IDLE;
static uint32_t fw_size;
static char fw_err[96];

bool fw_file_desc(esp_app_desc_t *d, uint32_t *size)
{
    struct stat st;
    if (stat(FW_FILE, &st) != 0 || st.st_size < (off_t)(sizeof(esp_image_header_t) + 256)) {
        return false;
    }
    FILE *f = fopen(FW_FILE, "rb");
    if (!f) {
        return false;
    }
    esp_image_header_t h;
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == ESP_IMAGE_HEADER_MAGIC && h.chip_id == ESP_CHIP_ID_ESP32P4;
    /* the app description is the start of the first segment */
    ok = ok && fseek(f, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), SEEK_SET) == 0 &&
         fread(d, sizeof(*d), 1, f) == 1 && d->magic_word == ESP_APP_DESC_MAGIC_WORD;
    fclose(f);
    if (ok && size) {
        *size = (uint32_t)st.st_size;
    }
    return ok;
}

/* (The app description's date is when the Arduino core's libraries were built: the same for
 * every sketch build, so the ELF's hash tells builds apart.) */
void fw_build_id(const esp_app_desc_t *d, char out[9])
{
    for (int i = 0; i < 4; i++) {
        snprintf(out + 2 * i, 3, "%02x", d->app_elf_sha256[i]);
    }
}

static void fw_task(void *arg)
{
    uint8_t *buf = heap_caps_malloc(FW_CHUNK, MALLOC_CAP_SPIRAM);
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    FILE *f = fopen(FW_FILE, "rb");
    esp_ota_handle_t h = 0;
    uint32_t done = 0;
    esp_err_t err = !buf ? ESP_ERR_NO_MEM : !part ? ESP_ERR_NOT_FOUND : !f ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (err == ESP_OK) {
        err = esp_ota_begin(part, fw_size, &h);    /* erases what the image needs, all at once */
    }
    while (err == ESP_OK) {
        size_t n = fread(buf, 1, FW_CHUNK, f);
        if (!n) {
            break;
        }
        err = esp_ota_write(h, buf, n);
        done += (uint32_t)n;
    }
    if (f) {
        fclose(f);
    }
    if (h) {
        esp_err_t end = esp_ota_end(h);             /* the whole image checked (format, chip, hash) */
        if (err == ESP_OK) {
            err = end;
        }
    }
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(part);
    }
    if (err == ESP_OK) {
        remove(FW_DONE);
        rename(FW_FILE, FW_DONE);                   /* not again at the next look */
        printf("update: %u bytes into %s, starting it\n", (unsigned)done, part->label);
    } else {
        snprintf(fw_err, sizeof(fw_err), "Update failed (%s): the running firmware stays.", esp_err_to_name(err));
        printf("update: %s\n", fw_err);
    }
    heap_caps_free(buf);
    fw_state = err == ESP_OK ? FW_DONE_OK : FW_FAILED;
    vTaskDelete(NULL);
}

bool fw_update_start(uint32_t size)
{
    if (fw_state == FW_WRITING) {
        return false;
    }
    fw_size = size;
    fw_err[0] = 0;
    fw_state = FW_WRITING;
    printf("update: writing %u bytes\n", (unsigned)size);
    if (xTaskCreatePinnedToCore(fw_task, "fwupdate", 8192, NULL, 3, NULL, WORKER_CORE) != pdPASS) {
        fw_state = FW_IDLE;
        return false;
    }
    return true;
}

fw_state_t fw_update_state(void) { return fw_state; }

void fw_restart_into_new(void)
{
    stats_db_suspend();                             /* sudoku.db closed, the card unmounted */
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}
const char *fw_update_error(void) { return fw_err; }

void fw_mark_running_ok(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        printf("update: the new firmware started fine: kept\n");
    }
}

/* ------------------------------------------------------------- microSD card */

static volatile sd_state_t s_sd_state = SD_IDLE;
static volatile bool s_sd_present;
static uint64_t s_sd_bytes;
static char s_sd_name[8];
static esp_err_t s_sd_err;

void sd_probe_card(void)
{
    s_sd_present = bsp_sdcard_probe(&s_sd_bytes, s_sd_name) == ESP_OK;
}

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
    if (xTaskCreatePinnedToCore(sd_task, "sd_card", 8192, (void *)(intptr_t)job, 4, NULL, WORKER_CORE) != pdPASS) {
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

/* ------------------------------------------------------------- factory reset, power off */

bool device_factory_reset(void)
{
    printf("factory reset\n");
    wifi_mgr_forget();                             /* network gone (Wi-Fi's NVS keys) */
    wifi_mgr_set_enabled(false);
    store_factory_reset();                         /* defaults, no stats, old NVS keys erased */
    if (!stats_db_ready()) {
        return false;
    }
    stats_db_factory_reset();                      /* sudoku.db deleted and made again */
    return true;
}

void device_power_off(void)
{
    printf("power off\n");
    stats_db_suspend();                            /* queued writes done, sudoku.db closed, card unmounted */
    bsp_power_off();                               /* deep sleep until RESET */
}

/* ------------------------------------------------------------- USB drive */

esp_err_t usb_drive_lend(void)
{
    stats_db_suspend();                            /* save, close sudoku.db, unmount: the card is free */
    esp_err_t err = usb_msc_set_enabled(true);
    if (err != ESP_OK) {
        stats_db_start();
    }
    return err;
}

void usb_drive_return(void)
{
    usb_msc_set_enabled(false);                    /* the computer lets go first */
    stats_db_start();                              /* remount + reopen (a new skeleton if it was deleted) */
}
