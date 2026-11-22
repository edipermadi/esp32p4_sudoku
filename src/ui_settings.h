/**
 * @file ui_settings.h
 * @ingroup settings
 * @brief The Settings screen's files: what they share. Only they include it.
 *
 * The rest of the UI reaches Settings through ui_internal.h.
 */
#pragma once
#include "ui_internal.h"

/**
 * @defgroup settings Settings screen
 * @ingroup topic_settings
 * @brief The Settings screen: a tab bar over five tabs, a file per tab.
 *
 * | File                  | What's in it |
 * |-----------------------|--------------|
 * | ui_settings.c         | the screen around the tabs (title, tab bar, a pane per tab), the rows' look (settings_row() and the controls), and what isn't one tab's: the theme, settings reloaded from sudoku.db, the poll of sudoku.db's results, power off, the dark screen while the flash is written |
 * | ui_settings_network.c | Wi-Fi, time zone |
 * | ui_settings_display.c | dark mode, accent, colour-blind colours, digit size, brightness, screen off, lock screen and PIN, screen-saver clock, night mode |
 * | ui_settings_sound.c   | sound effects, volume, Gibberlink |
 * | ui_settings_game.c    | timer, mistake check, no-notes challenge, left-handed, hints, tour |
 * | ui_settings_data.c    | microSD card (Format), USB drive, firmware update, import, export to CSV, back up, factory reset (what those do to the board: settings_core.h) |
 *
 * A tab's file builds its rows into the pane it's given (settings_tab_network() and the others), and
 * keeps its widgets and callbacks to itself. A row is settings_row() with a control on its right:
 * settings_switch(), settings_dropdown() or settings_slider().
 * @{
 */

/** @name Tabs
 *  Each builds its tab's rows into @p pane (a scrolling column made by build_settings()).
 *  @{ */
/** @brief Settings > Network: Wi-Fi (opens the Wi-Fi screen) and the time zone.
 *  @param[in] pane  the tab's pane */
void settings_tab_network(lv_obj_t *pane);
/** @brief Settings > Display: theme, accent, colour-blind, digit size, brightness, screen off, lock, night mode.
 *  @param[in] pane  the tab's pane */
void settings_tab_display(lv_obj_t *pane);
/** @brief Settings > Sound: sound effects, volume, Gibberlink.
 *  @param[in] pane  the tab's pane */
void settings_tab_sound(lv_obj_t *pane);
/** @brief Settings > Game: timer, mistake check, no-notes challenge, left-handed, hints, tour.
 *  @param[in] pane  the tab's pane */
void settings_tab_game(lv_obj_t *pane);
/** @brief Settings > Data: microSD card, USB drive, firmware update, import, export, backup, factory reset.
 *  @param[in] pane  the tab's pane */
void settings_tab_data(lv_obj_t *pane);
/** @} */

/** @name Rows and their controls
 *  @{ */
/**
 * @brief A row of a tab: a card with a title, a hint under it, and room on the right for a control.
 * @param[in] parent  the tab's pane
 * @param[in] title   the setting's name
 * @param[in] hint    a line about it, under the title (its label is the row's child 1, to change it later)
 * @return the row, to put a control on
 */
lv_obj_t *settings_row(lv_obj_t *parent, const char *title, const char *hint);
/**
 * @brief An on/off switch on the right of a row.
 * @param[in] row  the row
 * @param[in] on   its state
 * @param[in] cb   called when it's switched (LV_EVENT_VALUE_CHANGED)
 * @return the switch
 */
lv_obj_t *settings_switch(lv_obj_t *row, bool on, lv_event_cb_t cb);
/**
 * @brief A dropdown on the right of a row, styled like the others (finger-sized list rows).
 * @param[in] row      the row
 * @param[in] options  the choices, one per line (kept, not copied: a static string)
 * @param[in] sel      the one selected, from 0
 * @param[in] width    its width in pixels
 * @param[in] cb       called when a choice is picked (LV_EVENT_VALUE_CHANGED)
 * @return the dropdown
 */
lv_obj_t *settings_dropdown(lv_obj_t *row, const char *options, uint32_t sel, int width, lv_event_cb_t cb);
/**
 * @brief A slider on the right of a row, with its value ("80%") left of it.
 * @param[in] row    the row
 * @param[in] min    its lowest value
 * @param[in] max    its highest value
 * @param[in] value  its value now
 * @param[in] cb     called while it moves (LV_EVENT_VALUE_CHANGED) and once let go (LV_EVENT_RELEASED): apply
 *               while it moves, save when let go
 * @param[out] lbl  the value's label, for @p cb to update
 * @return the slider
 */
lv_obj_t *settings_slider(lv_obj_t *row, int min, int max, int value, lv_event_cb_t cb, lv_obj_t **lbl);
/** @} */

/** @name Between the tabs and the screen around them
 *  @{ */
/**
 * @brief The dark look is on: dark mode, or night mode now (ui_settings.c).
 * @return true while the screens are in the dark palette
 */
bool is_ui_dark(void);
/**
 * @brief The settings come again (ui_settings.c): after the card was lent over USB, or a factory reset.
 * @param[in] from_db  from sudoku.db, once it has (re)opened (sd_poll_cb() takes them then); false: from
 *                 the settings in RAM, at once (apply_loaded_prefs())
 * @param[in] to_home  then Home is shown (a factory reset)
 */
void settings_reload(bool from_db, bool to_home);
/** @brief The settings were reloaded: night mode is decided again at its next check (ui_settings_display.c). */
void night_forget(void);
/** @brief Settings > Data is shown: the Firmware update row looks at update.bin again (ui_settings_data.c). */
void settings_data_shown(void);
/** @brief From sd_poll_cb(): takes the card back once the computer ejected it, or the cable came out
 *  (ui_settings_data.c). */
void settings_data_poll_usb(void);
/** @brief From sd_poll_cb(): follows Format's steps as the card task moves through them, and shows the
 *  result (ui_settings_data.c). */
void settings_data_poll_card(void);
/** @} */

/** @} */
