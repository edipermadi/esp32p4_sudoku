/**
 * @file ui_wifi_part.h
 * @brief What the Wi-Fi page's files share (ui_wifi.c, ui_wifi_dialog.c); the rest of the UI uses ui_wifi.h.
 */
#pragma once
#include <stdbool.h>
#include <lvgl.h>
#include "wifi_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ui_wifi_dialog.c */
/**
 * @brief Closes the password dialog (and clears the password).
 */
void wifi_dlg_close(void);
/**
 * @brief Joins the dialog's network with the password typed (8 characters at least).
 */
void wifi_dlg_join(void);
/**
 * @brief Builds the password dialog (hidden) on the top layer.
 */
void wifi_dlg_build(void);
/**
 * @brief Opens the password dialog for a network.
 * @param[in] ssid  the network
 */
void wifi_dlg_open(const char *ssid);
/**
 * @brief A rounded text button.
 * @param[in] parent  where
 * @param[in] txt  its text
 * @param[in] primary  in the accent colour
 * @param[in] cb  a tap
 * @return it
 */
lv_obj_t *wifi_pill_btn(lv_obj_t *parent, const char *txt, bool primary, lv_event_cb_t cb);
/**
 * @brief A transparent full-width row.
 * @param[in] parent  where
 * @return it
 */
lv_obj_t *wifi_plain_row(lv_obj_t *parent);
/**
 * @brief A round icon button.
 * @param[in] parent  where
 * @param[in] sym  its icon
 * @param[in] cb  a tap
 * @return it
 */
lv_obj_t *wifi_round_btn(lv_obj_t *parent, const char *sym, lv_event_cb_t cb);
/** @brief The dialog dropped (the page rebuilt on a theme change). */
void wifi_dlg_drop(void);

#ifdef __cplusplus
}
#endif
