/**
 * @file usb_drive.c
 * @ingroup usb_drive
 * @brief The microSD card lent to a computer over USB, and taken back.
 */
#include "usb_drive.h"
#include "stats_db.h"
#include "usb_msc.h"

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
