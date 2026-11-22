/**
 * @file fw_update.h
 * @ingroup fw_update
 * @brief Firmware update from the card: update.bin into the other app slot.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_app_desc.h"
#include "bsp/bsp_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup fw_update Firmware update
 * @ingroup settings_core
 * @brief update.bin from the card into the other app slot, checked, then started on probation.
 *
 * update.bin in the card's root (the sketch's .ino.bin from the Arduino IDE's Export Compiled Binary,
 * or the build folder) goes into the other app slot (two 4.44 MiB slots, ota_0 / ota_1: partitions.csv;
 * the avatars are in a partition of their own). esp_ota_end() checks the whole image (format, chip,
 * SHA-256) before the board is told to start it, so a broken file changes nothing. The new firmware
 * starts on probation (the bootloader's rollback): it is kept once the screens are up and sudoku.db
 * has opened (fw_mark_running_ok()); if it crashes before that, the next restart goes back to the old
 * one. Afterwards update.bin is renamed update.done.
 *
 * The flash is erased all at once: the caller darkens the screen meanwhile (writing the flash makes
 * the panel flicker: sudoku_ui_flash_dark()).
 * @{
 */

#define FW_FILE  BSP_SD_MOUNT_POINT "/update.bin"   /**< the firmware to install */
#define FW_DONE  BSP_SD_MOUNT_POINT "/update.done"  /**< update.bin once installed (not again at the next look) */
#define FW_CHUNK (16 * 1024)                        /**< read from the card and written to flash this much at a time */

/** @brief Where a firmware update is. */
typedef enum {
    FW_IDLE,                                        /**< none running (or not started) */
    FW_WRITING,                                     /**< the task is writing and checking it */
    FW_DONE_OK,                                     /**< written, checked and set to boot: fw_restart_into_new() */
    FW_FAILED,                                      /**< it didn't work, the running firmware stays: fw_update_error() */
} fw_state_t;

/**
 * @brief Reads update.bin's app description, to show which build it is.
 * @param[out] d     its app description
 * @param[out] size  its size in bytes (may be NULL)
 * @return false if there's no update.bin, or it isn't an ESP32-P4 app image
 */
bool fw_file_desc(esp_app_desc_t *d, uint32_t *size);
/**
 * @brief A build's ID: the first 8 hex digits of its ELF file's SHA-256 (as a crash log prints it).
 * @param[in] d         the build's app description (esp_app_get_description() for the one running)
 * @param[out] out  the ID, 8 characters and a NUL
 */
void fw_build_id(const esp_app_desc_t *d, char out[9]);
/**
 * @brief Writes update.bin into the other app slot, in a task of its own; follow it with fw_update_state().
 * @param[in] size  update.bin's size in bytes (from fw_file_desc())
 * @return false if it couldn't start (one is running already, or no task)
 */
bool fw_update_start(uint32_t size);
/**
 * @brief Where the update is.
 * @return FW_WRITING while the task runs, then FW_DONE_OK or FW_FAILED
 */
fw_state_t fw_update_state(void);
/**
 * @brief What went wrong, after FW_FAILED.
 * @return a sentence for the user, e.g. "Update failed (ESP_ERR_OTA_VALIDATE_FAILED): the running firmware stays."
 */
const char *fw_update_error(void);
/** @brief After FW_DONE_OK: sudoku.db closed, the card unmounted, and the board restarts into the new firmware. */
void fw_restart_into_new(void) __attribute__((noreturn));
/** @brief Call once start-up went well: a firmware just installed from update.bin is kept (no rollback). */
void fw_mark_running_ok(void);

/** @} */

#ifdef __cplusplus
}
#endif
