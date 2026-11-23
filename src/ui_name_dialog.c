/**
 * @file ui_name_dialog.c
 * @ingroup ui_players
 * @brief The name dialog: a player's name typed in, to add a player or rename one (Players, the first start).
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void nd_close(void);
static void nd_ok(void);
static void nd_kb_cb(lv_event_t *e);
static void nd_btn_cb(lv_event_t *e);

/* name dialog (add or rename), on the top layer with an on-screen keyboard */
static lv_obj_t *nd_ovl;                  /**< the dialog's dimmed layer (NULL: closed) */
static lv_obj_t *nd_ta;                   /**< the name */
static int32_t nd_rename_id;              /**< the player renamed (0: add a player) */
static void (*nd_done)(const char *name); /**< not a player's name: who takes it */

/** @brief Closes the name dialog. */
static void nd_close(void)
{
    if (!nd_ovl) {
        return;
    }
    lv_obj_delete_async(nd_ovl);
    nd_ovl = nd_ta = NULL;
}

/** @brief The name dialog's OK: the name to whoever asked (a player added or renamed, or nd_open_ex()'s done). */
static void nd_ok(void)
{
    const char *name = lv_textarea_get_text(nd_ta);
    if (nd_done) {
        nd_done(name);
    } else {
        players_name_entered(nd_rename_id, name);
    }
    nd_close();
}

/**
 * @brief The keyboard's Enter (OK) and close key (Cancel).
 * @param[in] e  the event
 */
static void nd_kb_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        nd_ok();
    } else if (code == LV_EVENT_CANCEL) {
        nd_close();
    }
}

/**
 * @brief Cancel or OK.
 * @param[in] e  the event (user data: 1 OK, 0 Cancel)
 */
static void nd_btn_cb(lv_event_t *e)
{
    if ((intptr_t)lv_event_get_user_data(e)) {
        nd_ok();
    } else {
        nd_close();
    }
}

/**
 * @brief The name dialog: a title, a text box with a keyboard, Cancel and OK.
 * @param[in] title    the title
 * @param[in] current  the text to start with (NULL: none)
 * @param[in] max_len  the longest name
 * @param[in] ok_text  the OK button's text
 * @param[out] done     gets the name (NULL: a player's, added or renamed: nd_open())
 */
void nd_open_ex(const char *title, const char *current, int max_len, const char *ok_text,
                void (*done)(const char *name))
{
    nd_close();
    nd_done = done;
    nd_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(nd_ovl);
    lv_obj_set_size(nd_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(nd_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(nd_ovl, LV_OPA_70, 0);
    lv_obj_set_clickable(nd_ovl, true);              /* block touches to the page */

    lv_obj_t *panel = lv_obj_create(nd_ovl);
    lv_obj_set_size(panel, LV_PCT(92), LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(panel, C_CARD, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(panel, false);
    lv_obj_t *t = make_label(panel, &lv_font_montserrat_24, C_TEXT);
    lv_label_set_text(t, title);

    nd_ta = lv_textarea_create(panel);
    lv_obj_set_width(nd_ta, LV_PCT(100));
    lv_textarea_set_one_line(nd_ta, true);
    lv_textarea_set_max_length(nd_ta, (uint32_t)max_len);
    lv_textarea_set_placeholder_text(nd_ta, "Name");
    lv_textarea_set_text(nd_ta, current ? current : "");
    lv_obj_set_style_text_font(nd_ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(nd_ta, C_TEXT, 0);
    lv_obj_set_style_text_color(nd_ta, C_MUTED, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(nd_ta, C_BG, 0);
    lv_obj_set_style_border_color(nd_ta, C_ACCENT, 0);
    lv_obj_set_style_radius(nd_ta, 10, 0);
    lv_obj_add_state(nd_ta, LV_STATE_FOCUSED);

    lv_obj_t *row = lv_obj_create(panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_t *cancel = make_btn(row, 0, 0, 150, 52, "Cancel", &lv_font_montserrat_18, nd_btn_cb, (void *)0);
    lv_obj_set_style_bg_color(cancel, C_CARD2, 0);
    lv_obj_t *ok = make_btn(row, 0, 0, 150, 52, ok_text, &lv_font_montserrat_18,
                            nd_btn_cb, (void *)1);
    lv_obj_set_style_bg_color(ok, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(ok, 0), lv_color_white(), 0);

    lv_obj_t *kb = lv_keyboard_create(nd_ovl);
    lv_obj_set_size(kb, LV_PCT(100), 330);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, nd_ta);
    lv_obj_set_style_bg_color(kb, C_BG, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 8, 0);
    lv_obj_set_style_pad_bottom(kb, 30, 0);
    lv_obj_set_style_pad_gap(kb, 8, 0);
    lv_obj_set_style_bg_color(kb, C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, C_CARD, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, C_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_22, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 10, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, nd_kb_cb, LV_EVENT_ALL, NULL);
}

/* (documented in ui_internal.h) */
void nd_open(int32_t rename_id, const char *current)
{
    nd_open_ex(rename_id ? "Rename player" : "New player", current, STORE_NAME_LEN - 1, rename_id ? "Rename" : "Add",
               NULL);
    nd_rename_id = rename_id;
}
