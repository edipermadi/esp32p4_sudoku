/**
 * @file usb_drive.h
 * @ingroup usb_drive
 * @brief The microSD card lent to a computer over USB, and taken back.
 */
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup usb_drive USB drive
 * @ingroup settings_core
 * @brief The card as a USB drive on the computer at the OTG port (usb_msc.cpp), with sudoku.db closed
 *        meanwhile so the two never write it at once.
 * @{
 */

/**
 * @brief Lends the card to the computer: sudoku.db saved and closed, the card unmounted, then the USB drive on.
 * @return ESP_OK, or why the USB drive didn't start (then sudoku.db is open again)
 */
esp_err_t usb_drive_lend(void);
/** @brief Takes the card back: the USB drive off, then sudoku.db reopened (a new skeleton if it was deleted). */
void usb_drive_return(void);

/** @} */

#ifdef __cplusplus
}
#endif
