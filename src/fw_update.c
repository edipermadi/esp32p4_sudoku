/**
 * @file fw_update.c
 * @ingroup fw_update
 * @brief Firmware update from the card: the write task, and where it is.
 */
#include "fw_update.h"
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

static volatile fw_state_t fw_state = FW_IDLE;   /**< where the update is (set by the task) */
static uint32_t fw_size;                        /**< update.bin's size, for esp_ota_begin() */
static char fw_err[96];                         /**< after FW_FAILED: what went wrong, for the user */

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

/**
 * @brief The write: update.bin read in FW_CHUNK pieces into the other app slot, the image checked
 *        (esp_ota_end()), set to boot, update.bin renamed update.done; then fw_state is the result.
 * @param arg  unused
 */
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
    if (xTaskCreatePinnedToCore(fw_task, "fwupdate", 8192, NULL, 3, NULL, SETTINGS_TASK_CORE) != pdPASS) {
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
