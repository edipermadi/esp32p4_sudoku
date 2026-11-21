/* What Settings does to the board, without the screens: firmware update from the card,
 * the microSD card (probe, format), factory reset, power off, the USB drive. No LVGL here:
 * ui_settings.c draws the dialogs and follows these through their state. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_app_desc.h"
#include "bsp/bsp_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------- firmware update (from the card) */
/*
 * update.bin in the card's root (the sketch's .ino.bin from the Arduino IDE's Export Compiled
 * Binary, or the build folder) goes into the other app slot (two 4.44 MiB slots, ota_0 / ota_1:
 * partitions.csv; the avatars are in a partition of their own). esp_ota_end() checks the whole
 * image (format, chip, SHA-256) before the board is told to start it, so a broken file changes
 * nothing. The new firmware starts on probation (the bootloader's rollback): it is kept once
 * the screens are up and sudoku.db has opened (fw_mark_running_ok()); if it crashes before
 * that, the next restart goes back to the old one. Afterwards update.bin is renamed update.done.
 * The flash is erased all at once: the caller darkens the screen meanwhile (writing the flash
 * makes the panel flicker: sudoku_ui_flash_dark()).
 */
#define FW_FILE  BSP_SD_MOUNT_POINT "/update.bin"
#define FW_DONE  BSP_SD_MOUNT_POINT "/update.done"
#define FW_CHUNK (16 * 1024)

typedef enum { FW_IDLE, FW_WRITING, FW_DONE_OK, FW_FAILED } fw_state_t;

/* update.bin's app description and size; false if it isn't an ESP32-P4 app image */
bool fw_file_desc(esp_app_desc_t *d, uint32_t *size);
/* a build's ID: the first 8 hex digits of its ELF file's SHA-256 (as a crash log prints it) */
void fw_build_id(const esp_app_desc_t *d, char out[9]);
/* writes update.bin (size bytes) into the other slot in a task; false if it couldn't start */
bool fw_update_start(uint32_t size);
fw_state_t fw_update_state(void);
const char *fw_update_error(void);             /* after FW_FAILED: what went wrong, for the user */
/* after FW_DONE_OK: sudoku.db closed, the card unmounted, and the board restarts into it */
void fw_restart_into_new(void) __attribute__((noreturn));
/* Call once start-up went well: a firmware just installed from update.bin is kept. */
void fw_mark_running_ok(void);

/* ------------------------------------------------------------- microSD card */
/* The card work runs in a short-lived task; the UI follows sd_state() and drives the dialogs.
 * Only one job at a time.
 *
 * The card shares the SDMMC host with the Wi-Fi link, and initialising a card (400 kHz)
 * disturbs that link badly enough that ESP-Hosted restarts the board. So the card is detected
 * at boot, before Wi-Fi starts (sd_probe_card(), from sudoku_ui_start()), and formatting then
 * only transfers data at the link's own 40 MHz. A card inserted later can only be detected
 * while Wi-Fi has not been started since boot. */
typedef enum { SD_IDLE, SD_PROBING, SD_FOUND, SD_NO_CARD, SD_FORMATTING, SD_DONE, SD_FAILED } sd_state_t;
typedef enum { SD_JOB_PROBE, SD_JOB_FORMAT } sd_job_t;

void sd_probe_card(void);                      /* at start-up, before Wi-Fi: is a card there */
/* SD_JOB_PROBE: look for a card (-> SD_FOUND / SD_NO_CARD); SD_JOB_FORMAT: close sudoku.db,
 * format the card FAT32, start a fresh sudoku.db (-> SD_DONE / SD_FAILED). false: no task. */
bool sd_run(sd_job_t job);
sd_state_t sd_state(void);
void sd_set_state(sd_state_t st);              /* the UI: back to SD_IDLE once it has shown a result */
esp_err_t sd_last_error(void);                 /* after SD_FAILED */
bool sd_present(void);
void sd_forget(void);                          /* the card stopped answering: not present any more */
uint64_t sd_bytes(void);
const char *sd_name(void);
void fmt_gb(char *buf, size_t n, uint64_t bytes);   /* "14.8 GB", as printed on cards */

/* ------------------------------------------------------------- factory reset, power off */
/* Settings back to the defaults, the Wi-Fi network forgotten and Wi-Fi off, the old NVS keys
 * erased, and sudoku.db deleted and made again. true: sudoku.db is being rebuilt (its settings
 * arrive like a reload: stats_db_take_prefs()); false: no sudoku.db, done now. */
bool device_factory_reset(void);
/* sudoku.db closed and the card unmounted, then deep sleep until RESET. Never returns. */
void device_power_off(void) __attribute__((noreturn));

/* ------------------------------------------------------------- USB drive */
/* The card lent to the computer on the OTG port (sudoku.db closed first), and taken back
 * (sudoku.db reopened; a new skeleton if it was deleted). */
esp_err_t usb_drive_lend(void);
void usb_drive_return(void);

#ifdef __cplusplus
}
#endif
