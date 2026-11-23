/**
 * @file ui_players.c
 * @ingroup ui_players
 * @brief Players (Home): a row per player (tap one to play as them; their avatar or colour dot, tapped,
 *        gives the next colour), the ⋮ menu (Add, Rename, Avatar, Delete), the avatar picker, and the
 *        name dialog other screens use too. The data: players_core.h.
 */
#include "ui_internal.h"
#include "players_core.h"

/**
 * @defgroup ui_players Players screen
 * @ingroup topic_players
 * @brief The Players screen, its avatar picker, and the name dialog.
 * @{
 */

/* this file's own functions, in any order */
static void player_row_cb(lv_event_t *e);
static void player_rename_cb(lv_event_t *e);
static void player_delete_confirm_cb(lv_event_t *e);
static void player_delete_cb(lv_event_t *e);
static void player_add_cb(lv_event_t *e);
static void player_menu_cb(lv_event_t *e);
static void player_color_cb(lv_event_t *e);

static lv_obj_t *p_list;                  /**< the players' rows */
static lv_obj_t *p_menu_btn;              /**< the title row's ⋮ menu */
static lv_obj_t *p_note;                  /**< the line at the bottom */


/**
 * @brief A player's row: play as them.
 * @param[in] e  the event (user data: players.id)
 */
static void player_row_cb(lv_event_t *e)
{
    players_switch((int32_t)(intptr_t)lv_event_get_user_data(e));
}

/**
 * @brief The menu's Rename: who plays now.
 * @param[in] e  the event
 */
static void player_rename_cb(lv_event_t *e)
{
    if (is_stats_db_ready()) {
        nd_open(store_player_id(), store_player_name());    /* who plays now */
    }
}

/**
 * @brief The Delete dialog's Delete.
 * @param[in] e  the event (user data: the dialog)
 */
static void player_delete_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    players_delete_confirmed();
}

/**
 * @brief The menu's Delete: who plays now (another takes over; to delete someone else, switch
 *        first), asked first.
 * @param[in] e  the event
 */
static void player_delete_cb(lv_event_t *e)
{
    const stats_player_t *pl = players_delete_ask();
    if (!pl) {
        return;
    }
    char title[48], text[256];
    snprintf(title, sizeof(title), "Delete %s?", pl->name);
    snprintf(text, sizeof(text),
             "Their statistics (%u solved puzzle%s), solve history (%lu solve%s) and game in progress "
             "are erased from sudoku.db. This can't be undone.%s",
             (unsigned)pl->solved, pl->solved == 1 ? "" : "s",
             (unsigned long)pl->solves, pl->solves == 1 ? "" : "s",
             pl->id == store_player_id() ? "\nAnother player takes over." : "");
    lv_obj_t *m = make_msgbox(title, text);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_TRASH "  Delete", player_delete_confirm_cb, true);
    lv_obj_t *del = lv_obj_get_child(lv_msgbox_get_footer(m), -1);
    lv_obj_set_style_bg_color(del, C_RED, 0);
}

/**
 * @brief The menu's Add.
 * @param[in] e  the event
 */
static void player_add_cb(lv_event_t *e)
{
    if (is_stats_db_ready()) {
        nd_open(0, NULL);
    }
}


/**
 * @brief The title row's menu: Add, Rename and Avatar (who plays now), Delete (who plays now: not
 *        the only one).
 * @param[in] e  the event
 */
static void player_menu_cb(lv_event_t *e)
{
    bool db = is_stats_db_ready() && players_count() >= 0;
    const ctx_item_t items[] = {
        {LV_SYMBOL_PLUS, "Add player", player_add_cb, !db, false},
        {LV_SYMBOL_EDIT, "Rename player", player_rename_cb, !db, false},
        {LV_SYMBOL_IMAGE, "Avatar", player_avatar_cb, !db || !players_current(), false},
        {LV_SYMBOL_TRASH, "Delete player", player_delete_cb, !db || players_count() < 2, true},
    };
    ctx_menu_toggle(p_menu_btn, items, (int)(sizeof(items) / sizeof(items[0])));
}

/**
 * @brief A player's avatar or dot: their next colour.
 * @param[in] e  the event (user data: players.id)
 */
static void player_color_cb(lv_event_t *e)
{
    players_next_color((int32_t)(intptr_t)lv_event_get_user_data(e));
    render_players();
}

/**
 * @brief The player list sudoku.db sent, shown.
 * @param[in] list  the players
 * @param[in] n     how many
 */
void players_take(const stats_player_t *list, int n)
{
    players_take_list(list, n);
    render_players();
}

/** @brief Draws the list: a row per player (without sudoku.db just who the board thinks plays), and Home's Players button. */
void render_players(void)
{
    home_player_update();                           /* (the avatar on Home's Players button) */
    if (!p_list) {
        return;
    }
    lv_obj_clean(p_list);
    bool db = is_stats_db_ready() && players_count() >= 0;
    int n = db ? players_count() : 1;
    for (int i = 0; i < n; i++) {
        stats_player_t ram = {.id = store_player_id()};
        if (!db) {
            snprintf(ram.name, sizeof(ram.name), "%s", store_player_name());
        }
        const stats_player_t *pl = db ? &players_all()[i] : &ram;
        bool cur = !db || pl->id == store_player_id();

        lv_obj_t *r = lv_obj_create(p_list);
        lv_obj_set_size(r, LV_PCT(100), 80);
        lv_obj_set_style_bg_color(r, C_CARD, 0);
        lv_obj_set_style_bg_color(r, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(r, cur ? 2 : 0, 0);
        lv_obj_set_style_border_color(r, C_ACCENT, 0);
        lv_obj_set_style_radius(r, 12, 0);
        lv_obj_set_style_pad_hor(r, 22, 0);
        lv_obj_set_scrollable(r, false);
        lv_obj_set_clickable(r, !cur);
        lv_obj_add_event_cb(r, player_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)pl->id);

        /* their avatar (on a circle of their colour) or, without one, a dot of it: tap it for the next colour */
        int av = db && pl->avatar <= AVATAR_COUNT ? pl->avatar : 0;
        lv_obj_t *cb = make_btn(r, 0, 0, 56, 56, NULL, NULL, db ? player_color_cb : NULL, (void *)(intptr_t)pl->id);
        lv_obj_set_style_radius(cb, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(cb, player_color(db ? pl->color : 0), 0);
        lv_obj_set_style_bg_opa(cb, av ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(cb, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cb, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_align(cb, LV_ALIGN_LEFT_MID, -10, 0);
        if (av) {
            lv_obj_t *im = lv_image_create(cb);
            lv_image_set_src(im, avatar_image(av - 1));
            lv_obj_center(im);
        } else {
            lv_obj_center(player_dot(cb, db ? pl->color : 0, 22));
        }
        lv_obj_t *nm = make_label(r, &lv_font_montserrat_22, C_TEXT);
        lv_label_set_text(nm, pl->name);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 58, -12);
        lv_obj_t *sub = make_label(r, &lv_font_montserrat_14, cur ? C_ACCENT : C_MUTED);
        if (cur && db)
            lv_label_set_text_fmt(sub, "Playing now   -   %u solved, %lu solves", (unsigned)pl->solved,
                                  (unsigned long)pl->solves);
        else if (cur) {
            lv_label_set_text(sub, "Playing now");
        } else
            lv_label_set_text_fmt(sub, "%u solved, %lu solves   -   tap to play", (unsigned)pl->solved,
                                  (unsigned long)pl->solves);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 58, 16);
    }
    lv_label_set_text(p_note, is_stats_db_ready()
                      ? "Statistics and the game in progress are kept per player in sudoku.db."
                      : "No sudoku.db: insert a FAT32 microSD card before switching on to keep players.");
}

/** @brief Builds the screen: Home, the title, the menu, the list, the note. */
void build_profile(void)
{
    screen_set_profile(make_screen());
    make_btn(screen_profile(), 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(screen_profile(), &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Players");
    lv_obj_set_pos(t, 150, 20);
    p_menu_btn = make_btn(screen_profile(), SCR_W - 12 - 52, 10, 52, 52, SYMBOL_MORE, &font_icons_24, player_menu_cb,
                          NULL);                    /* Add, Rename, Delete */

    p_list = lv_obj_create(screen_profile());
    lv_obj_remove_style_all(p_list);
    lv_obj_set_pos(p_list, 40, 84);
    lv_obj_set_size(p_list, SCR_W - 80, SCR_W - 84 - 56);
    lv_obj_set_flex_flow(p_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p_list, 12, 0);
    lv_obj_set_scroll_dir(p_list, LV_DIR_VER);

    p_note = make_label(screen_profile(), &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(p_note, SCR_W - 80);
    lv_label_set_long_mode(p_note, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(p_note, 44, SCR_W - 44);
    render_players();
}

/* ---- accessors */
/** @brief The Players screen's ⋮ button (the avatar picker opens under it). @return it */
lv_obj_t *players_menu_btn(void) { return p_menu_btn; }

/** @} */
