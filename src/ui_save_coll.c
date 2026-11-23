/**
 * @file ui_save_coll.c
 * @ingroup ui_game
 * @brief The game's Save: putting the puzzle in a saved collection, or taking it out, or a new collection.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void sv_close_cb(lv_event_t *e);
static void sv_toggle_cb(lv_event_t *e);
static void sv_new_done(const char *text);
static void sv_new_cb(lv_event_t *e);
static void sv_open(void);

/* ------------------------------------------------------------- save to a collection */
/**
 * @brief The game screen's Save (generated and typed-in puzzles): a dialog lists the saved
 * collections, ticked where the puzzle is; a tap puts it in or takes it out, and
 * "New collection..." asks for a name (the keyboard) and puts it there. They show in
 * Puzzles > Saved. A collection goes when its last puzzle is taken out.
 */
static lv_obj_t *sv_box;

/**
 * @brief Closes the Save dialog.
 * @param[in] e  the event (NULL from code)
 */
static void sv_close_cb(lv_event_t *e)
{
    if (sv_box) {
        lv_msgbox_close_async(sv_box);
    }
    sv_box = NULL;
}

/* (documented in ui_internal.h) */
void sv_changed(void)
{
    int before = ui_nsaved();
    imports_reload();
    if (ui_nsaved() != before) {
        list_fill_colls();
    }
    refresh_list();
    update_header();
}

/**
 * @brief A collection tapped: the puzzle put in or taken out.
 * @param[in] e  the event (user data: the collection)
 */
static void sv_toggle_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (k < 0 || k >= ui_nsaved()) {
        return;
    }
    char name[STATS_IMPORT_NAME], msg[96], why[96];
    snprintf(name, sizeof(name), "%s", ui_saved()[k].name);
    if (coll_find(PUZZLE_COLLECTION_COUNT + ui_nimports() + k, game_id()) >= 0) {
        bool last = ui_saved()[k].count == 1;
        stats_db_saved_remove(ui_saved()[k].id, game_id());
        snprintf(msg, sizeof(msg), last ? "Taken out of %s (it was the last: the collection is gone)."
                 : "Taken out of %s.", name);
    } else if (stats_db_saved_add(name, game_id(), why, sizeof(why))) {
        snprintf(msg, sizeof(msg), "Saved to %s (Puzzles > Saved).", name);
    } else {
        snprintf(msg, sizeof(msg), "%s", why);
    }
    sv_changed();
    sudoku_ui_notice(msg, 2500);
    sv_close_cb(NULL);
    sv_open();                                      /* (the ticks again) */
}

/**
 * @brief The new collection's name typed: made, with the puzzle in it.
 * @param[in] text  the name
 */
static void sv_new_done(const char *text)
{
    char name[STATS_IMPORT_NAME];
    while (*text == ' ') {
        text++;
    }
    snprintf(name, sizeof(name), "%s", text);
    for (size_t l = strlen(name); l && name[l - 1] == ' ';) {
        name[--l] = 0;
    }
    if (!name[0]) {
        sudoku_ui_notice("A collection needs a name.", 2500);
        return;
    }
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT + ui_nimports(); c++) {
        if (!strcasecmp(coll_name(c), name)) {
            sudoku_ui_notice("That name is taken by a built-in or imported collection.", 3000);
            return;
        }
    }
    char msg[96], why[96];
    if (stats_db_saved_add(name, game_id(), why, sizeof(why))) {
        snprintf(msg, sizeof(msg), "Saved to %s (Puzzles > Saved).", name);
    } else {
        snprintf(msg, sizeof(msg), "%s", why);
    }
    sv_changed();
    sudoku_ui_notice(msg, 2500);
}

/**
 * @brief New collection...: asks for its name.
 * @param[in] e  the event
 */
static void sv_new_cb(lv_event_t *e)
{
    sv_close_cb(NULL);
    nd_open_ex("New collection", "", STATS_IMPORT_NAME - 1, "Save", sv_new_done);
}

/**
 * @brief Opens the Save dialog: the saved collections, ticked where the puzzle is.
 */
static void sv_open(void)
{
    lv_obj_t *m = make_msgbox(LV_SYMBOL_SAVE "  Save to a collection", NULL);
    sv_box = m;
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 8, 0);
    lv_obj_set_style_max_height(c, 400, 0);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    if (!ui_nsaved()) {
        make_wrap_text(c, NULL, C_MUTED, "No collections yet: make one.");
    }
    for (int k = 0; k < ui_nsaved(); k++) {
        bool in = coll_find(PUZZLE_COLLECTION_COUNT + ui_nimports() + k, game_id()) >= 0;
        lv_obj_t *b = make_btn(c, 0, 0, 0, 56, NULL, NULL, sv_toggle_cb, (void *)(intptr_t)k);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_style_bg_color(b, in ? C_ACCENT : C_CARD2, 0);
        lv_obj_t *l = make_label(b, &lv_font_montserrat_20, in ? lv_color_white() : C_TEXT);
        lv_label_set_text_fmt(l, "%s%s", in ? LV_SYMBOL_OK "  " : "", ui_saved()[k].name);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *n = make_label(b, &lv_font_montserrat_16, in ? lv_color_white() : C_MUTED);
        lv_label_set_text_fmt(n, "%d", ui_saved()[k].count);
        lv_obj_align(n, LV_ALIGN_RIGHT_MID, -8, 0);
    }
    if (ui_nsaved() < STATS_MAX_SAVED) {
        lv_obj_t *nb = make_btn(c, 0, 0, 0, 56, LV_SYMBOL_PLUS "  New collection...", &lv_font_montserrat_20,
                                sv_new_cb, NULL);
        lv_obj_set_width(nb, LV_PCT(100));
        lv_obj_set_style_bg_color(nb, C_CARD2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(nb, 0), C_ACCENT, 0);
    }
    msgbox_button(m, "Close", sv_close_cb, false);
}

/* (documented in ui_internal.h) */
void save_cb(lv_event_t *e)
{
    if (game_practice() >= 0 || !puzzle_is_generated(game_id()) || game_id() >= PUZZLE_GEN_RAM_BASE) {
        return;
    }
    if (!is_stats_db_ready()) {
        sudoku_ui_notice("Collections are kept in sudoku.db on a microSD card: there's none.", 3000);
        return;
    }
    if (!CLOCK_VALID(time(NULL))) {                 /* (each collection is dated: stats_db_saved_add() refuses) */
        sudoku_ui_notice("Saving needs the clock: connect to Wi-Fi first (Settings > Network).", 3500);
        return;
    }
    imports_reload();                               /* (current: another player may have changed them) */
    sv_open();
}
