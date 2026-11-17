#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the puzzle list and game screens and shows the list.
 * Call after bsp_lvgl_start() and store_init(). */
esp_err_t sudoku_ui_start(void);

#ifdef __cplusplus
}
#endif
