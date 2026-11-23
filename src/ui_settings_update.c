/**
 * @file ui_settings_update.c
 * @ingroup settings
 * @brief Settings > Data > Firmware update: update.bin from the card into the other app slot, with the screen dark meanwhile.
 */
#include "ui_settings.h"

/* this file's own functions, in any order */
static void fw_row_update(void);
static void fw_poll_cb(lv_timer_t *t);
static void fw_start_cb(lv_timer_t *t);
static void fw_go_cb(lv_event_t *e);
static void fw_update_cb(lv_event_t *e);

static lv_obj_t *st_fw_btn;                         /**< the Firmware update row's Update button */
static lv_obj_t *st_fw_hint;                        /**< the Firmware update row's hint: the builds */
static lv_timer_t *fw_timer;                        /**< the card's wait, then following the write */

/**
 * @brief The Firmware update row: the running build, and update.bin's if there is one.
 *
 * Update is greyed out without a usable update.bin.
 */
static void fw_row_update(void)
{
    if (!st_fw_hint) {
        return;
    }
    char run_id[9], new_id[9];
    fw_build_id(esp_app_get_description(), run_id);
    esp_app_desc_t d;
    uint32_t size = 0;
    bool have = is_stats_db_ready() && fw_file_desc(&d, &size);   /* (the card is mounted with sudoku.db open) */
    if (have) {
        fw_build_id(&d, new_id);
        lv_label_set_text_fmt(st_fw_hint, "update.bin: build %s, %u KB%s", new_id, (unsigned)(size / 1024),
                              strcmp(new_id, run_id) ? "" : " (the one running)");
    } else {
        lv_label_set_text_fmt(st_fw_hint, "Running build %s; put update.bin on the card", run_id);
    }
    lv_obj_set_state(st_fw_btn, LV_STATE_DISABLED, !have);
}

/**
 * @brief Follows the firmware write: restarts into it when done, or brings the screen back with the error.
 * @param[in] t  the timer
 */
static void fw_poll_cb(lv_timer_t *t)
{
    fw_state_t st = fw_update_state();
    if (st == FW_WRITING) {
        return;
    }
    lv_timer_delete(fw_timer);
    fw_timer = NULL;
    if (st == FW_FAILED) {
        sudoku_ui_flash_done();
        sudoku_ui_notice(fw_update_error(), 6000);
        fw_row_update();
        return;
    }
    fw_restart_into_new();                          /* (the screen comes back with the new firmware) */
}

/**
 * @brief After the card has been on screen a moment: the screen dark, then the write starts.
 * @param[in] t  the timer (its user data is update.bin's size)
 */
static void fw_start_cb(lv_timer_t *t)
{
    sudoku_ui_flash_dark();
    if (!fw_update_start((uint32_t)(uintptr_t)lv_timer_get_user_data(t))) {
        fw_timer = NULL;
        sudoku_ui_flash_done();
        sudoku_ui_notice("Couldn't start the update.", 4000);
        return;
    }
    fw_timer = lv_timer_create(fw_poll_cb, 200, NULL);
}

/**
 * @brief Update, confirmed: the card that says the screen goes dark, then (fw_start_cb()) the write.
 * @param[in] e  the event (its user data is the dialog)
 */
static void fw_go_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    esp_app_desc_t d;
    uint32_t size;
    if (fw_timer || !fw_file_desc(&d, &size)) {
        return;
    }
    char text[160];
    snprintf(text, sizeof(text), "The screen goes dark while the firmware is written (about %u seconds), then "
             "the board restarts into it by itself.", (unsigned)(size / (100 * 1024)) + 5);
    sudoku_ui_flash_busy(LV_SYMBOL_DOWNLOAD "  Updating the firmware", text);
    fw_timer = lv_timer_create(fw_start_cb, 3000, (void *)(uintptr_t)size);
    lv_timer_set_repeat_count(fw_timer, 1);
}

/**
 * @brief The Update button: asks first, with the running build and update.bin's.
 * @param[in] e  the LVGL event (its target is the control)
 */
static void fw_update_cb(lv_event_t *e)
{
    esp_app_desc_t d;
    uint32_t size;
    if (!fw_file_desc(&d, &size)) {
        sudoku_ui_notice("No usable update.bin on the card.", 3000);
        fw_row_update();
        return;
    }
    char run_id[9], new_id[9];
    fw_build_id(esp_app_get_description(), run_id);
    fw_build_id(&d, new_id);
    char text[360];
    snprintf(text, sizeof(text),
             "Running: build %s.\nupdate.bin: build %s, %u KB%s.\n\nIt is written to the board's other "
             "firmware slot and checked before the board restarts into it. If the new firmware doesn't start up "
             "properly, the board goes back to this one by itself.",
             run_id, new_id, (unsigned)(size / 1024), strcmp(new_id, run_id) ? "" : " (the same build)");
    lv_obj_t *m = make_msgbox("Update the firmware?", text);
    lv_obj_set_width(m, 600);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_DOWNLOAD "  Update", fw_go_cb, true);
}

/* (documented in ui_settings.h) */
void settings_data_shown(void)
{
    fw_row_update();
}

/* (documented in ui_settings.h) */
void settings_update_row(lv_obj_t *col)
{
    lv_obj_t *r;
    r = settings_row(col, "Firmware update", "");
    st_fw_hint = lv_obj_get_child(r, 1);
    st_fw_btn = make_btn(r, 0, 0, 160, 52, LV_SYMBOL_DOWNLOAD "  Update", &lv_font_montserrat_18, fw_update_cb, NULL);
    lv_obj_set_style_bg_color(st_fw_btn, C_CARD2, 0);
    lv_obj_set_style_bg_opa(st_fw_btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(st_fw_btn, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_align(st_fw_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    fw_row_update();
}

/* (documented in ui_settings.h) */
bool is_settings_update_busy(void)
{
    return fw_timer != NULL;
}
