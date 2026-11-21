/**
 * @file sd_card.h
 * @ingroup sd_card
 * @brief The microSD card: probe and format, in a task the UI follows.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sd_card microSD card
 * @ingroup settings_core
 * @brief Is a card there, its size and name; formatting it (a fresh sudoku.db after).
 *
 * The card work runs in a short-lived task; the UI follows sd_state() and drives the dialogs. Only one
 * job at a time.
 *
 * The card shares the SDMMC host with the Wi-Fi link, and initialising a card (400 kHz) disturbs that
 * link badly enough that ESP-Hosted restarts the board. So the card is detected at boot, before Wi-Fi
 * starts (sd_probe_card(), from sudoku_ui_start()), and formatting then only transfers data at the
 * link's own 40 MHz. A card inserted later can only be detected while Wi-Fi has not been started since
 * boot.
 * @{
 */

/** @brief Where the card work is. */
typedef enum {
    SD_IDLE,                                        /**< nothing running (or its result has been shown) */
    SD_PROBING,                                     /**< SD_JOB_PROBE: looking for a card */
    SD_FOUND,                                       /**< a card is there (sd_bytes(), sd_name()) */
    SD_NO_CARD,                                     /**< no card answered */
    SD_FORMATTING,                                  /**< SD_JOB_FORMAT: formatting it */
    SD_DONE,                                        /**< formatted, with a fresh sudoku.db */
    SD_FAILED,                                      /**< formatting failed: sd_last_error() */
} sd_state_t;

/** @brief What sd_run() does. */
typedef enum {
    SD_JOB_PROBE,                                   /**< look for a card */
    SD_JOB_FORMAT,                                  /**< format the card found */
} sd_job_t;

/** @brief At start-up, before Wi-Fi: is a card there (is_sd_present(), sd_bytes(), sd_name()). */
void sd_probe_card(void);
/**
 * @brief Starts card work in a task of its own; follow it with sd_state().
 * @param job  SD_JOB_PROBE: look for a card (then SD_FOUND or SD_NO_CARD); SD_JOB_FORMAT: close
 *             sudoku.db, format the card FAT32 and start a fresh sudoku.db (then SD_DONE or SD_FAILED)
 * @return false if the task couldn't start
 */
bool sd_run(sd_job_t job);
/**
 * @brief Where the card work is.
 * @return the state (SD_IDLE when nothing runs)
 */
sd_state_t sd_state(void);
/**
 * @brief Sets the state: the UI puts it back to SD_IDLE once it has shown a result.
 * @param st  the new state
 */
void sd_set_state(sd_state_t st);
/**
 * @brief Why formatting failed.
 * @return the error, after SD_FAILED
 */
esp_err_t sd_last_error(void);
/**
 * @brief A card was found (at start-up, or by SD_JOB_PROBE).
 * @return true if there's a card
 */
bool is_sd_present(void);
/** @brief The card stopped answering: it isn't there any more (until found again). */
void sd_forget(void);
/**
 * @brief The card's size.
 * @return its size in bytes
 */
uint64_t sd_bytes(void);
/**
 * @brief The card's name (its product name, e.g. "SC16G").
 * @return the name
 */
const char *sd_name(void);
/**
 * @brief A size in GB with one decimal, as printed on cards ("14.8 GB": 10^9 bytes).
 * @param[out] buf  the text
 * @param n         buf's size
 * @param bytes     the size in bytes
 */
void fmt_gb(char *buf, size_t n, uint64_t bytes);

/** @} */

#ifdef __cplusplus
}
#endif
