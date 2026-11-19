#pragma once
/*
 * System monitor screen (Settings > Data > System monitor): uptime, CPU
 * temperature and load, internal RAM / PSRAM heap, flash, NVS and microSD
 * usage, LVGL's frame rate and render times, and the FreeRTOS task list with
 * each task's CPU share and free stack. Refreshed once a second while shown.
 */
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the page into scr (an empty screen). back_cb runs on the back button.
 * Can be called again after a theme change (the caller deletes the old screen). */
void sysmon_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb);

/* Call when the page is loaded (true) or left (false). */
void sysmon_ui_set_shown(bool shown);

#ifdef __cplusplus
}
#endif
