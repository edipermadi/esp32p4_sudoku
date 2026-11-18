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

/* Starts USB with the drive empty ("no media"), so the board can tell when a
 * computer is plugged into the OTG port. Call at boot once the card has been
 * probed (nothing to do without a card). */
esp_err_t usb_msc_start(void);

/* True while a computer has the board enumerated on the OTG port and the bus
 * isn't suspended (unplugging suspends it). The UART port never counts. */
bool usb_msc_host_connected(void);

/* On: shows the card to the computer. Off: reports "no media" again, so the
 * computer lets go of it. */
esp_err_t usb_msc_set_enabled(bool on);

/* True once the computer has ejected the drive since it was enabled. */
bool usb_msc_ejected(void);

#ifdef __cplusplus
}
#endif
