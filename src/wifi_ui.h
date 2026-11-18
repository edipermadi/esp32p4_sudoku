#pragma once
/*
 * Wi-Fi screen: on/off switch, the current connection (with Forget), and the
 * list of nearby networks; tapping one asks for its password with an
 * on-screen keyboard and connects (see wifi_mgr.h).
 * Adapted from the esp32p4_86box project's page to this app's palette.
 */
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the page into scr (an empty screen). back_cb runs on the back button.
 * Can be called again after a theme change: the previous page's dialog and
 * timer are dropped (the caller deletes the old screen). */
void wifi_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb);

/* Call when the page is loaded (true) or left (false). */
void wifi_ui_set_shown(bool shown);

/* One-line summary for the Settings page, e.g. "MyNetwork" or "Off". */
const char *wifi_ui_summary(void);

#ifdef __cplusplus
}
#endif
