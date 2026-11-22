/**
 * @file ui_sysmon.h
 * @ingroup ui_sysmon
 * @brief The system monitor screen (Home > System), for the rest of the UI.
 *
 * Five tabs (System, Memory, Storage, Graphics, Tasks; the last one open is kept): uptime, CPU
 * temperature and load, internal RAM / PSRAM heap, flash, NVS and microSD usage, LVGL's frame rate and
 * render times, and the FreeRTOS task list with each task's CPU share and free stack. Refreshed once a
 * second while shown; the last hour's graphs are kept from start-up.
 */
#pragma once
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Builds the page into a screen. Can be called again after a theme change (the caller deletes the
 *        old screen); the history kept and the tab open stay.
 * @param[in] scr      an empty screen
 * @param[in] back_cb  runs on the back button
 */
void sysmon_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb);

/**
 * @brief The page is loaded (refreshed every second from now) or left (the refresh paused).
 * @param[in] shown  true: loaded
 */
void sysmon_ui_set_shown(bool shown);

#ifdef __cplusplus
}
#endif
