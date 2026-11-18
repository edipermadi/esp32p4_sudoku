#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the puzzle list and game screens and shows the list.
 * Call after bsp_lvgl_start() and store_init(). */
esp_err_t sudoku_ui_start(void);

/* Shows a message banner at the bottom of the screen for `ms` milliseconds. */
void sudoku_ui_notice(const char *text, uint32_t ms);

/* Turns the screen off now, as its timeout does (and shows the lock screen
 * under it, if that's on); the next tap wakes it. For serial control. */
void sudoku_ui_screen_off(void);

/* Call once start-up went well (screens up, sudoku.db open): a firmware just
 * installed from update.bin is kept; until then, a restart goes back to the
 * previous one (the bootloader's rollback). */
void sudoku_ui_mark_ok(void);

#ifdef __cplusplus
}
#endif
