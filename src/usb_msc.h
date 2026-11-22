/**
 * @file usb_msc.h
 * @brief The microSD card as a USB drive.
 */
#pragma once
/*
 * The microSD card as a USB mass-storage drive on the board's USB OTG
 * (high-speed) port, so a computer can copy sudoku.db (backup) or put one
 * back (restore). Adapted from the esp32p4_86box project.
 *
 * The card is either the board's or the computer's, never both: the caller
 * closes sudoku.db and unmounts the card (stats_db_suspend()) before
 * usb_msc_set_enabled(true), and reopens it after usb_msc_set_enabled(false).
 * Requires Tools > USB Mode = "USB-OTG (TinyUSB)" (the default).
 */
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 * @return ESP_OK, or why not
 */
/** @brief Starts USB with the drive empty ("no media"), so the board can tell when a
 * computer is plugged into the OTG port. Call at boot once the card has been
 * probed (nothing to do without a card). */
esp_err_t usb_msc_start(void);

/**
 * @brief
 * @return true if so
 */
/** @brief True while a computer has the board enumerated on the OTG port and the bus
 * isn't suspended (unplugging suspends it). The UART port never counts. */
bool is_usb_msc_host_connected(void);

/**
 * @brief
 * @param[in] on  on
 * @return ESP_OK, or why not
 */
/** @brief On: shows the card to the computer. Off: reports "no media" again, so the
 * computer lets go of it. */
esp_err_t usb_msc_set_enabled(bool on);

/**
 * @brief
 * @return true if so
 */
/** @brief True once the computer has ejected the drive since it was enabled. */
bool is_usb_msc_ejected(void);

/**
 * @brief
 * @return true if so
 */
/** @brief True once the computer has gone away while the drive was enabled: the OTG
 * port is no longer enumerated, or its bus has been suspended for 1.5 s
 * (an unplugged cable, or a computer gone to sleep). */
bool is_usb_msc_unplugged(void);

#ifdef __cplusplus
}
#endif
