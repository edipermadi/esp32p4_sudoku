/* ui_sudoku.c split: the name dialog, Players, avatars. See ui_sudoku.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void nd_close(void);
static void nd_ok(void);
static void nd_kb_cb(lv_event_t *e);
static void nd_btn_cb(lv_event_t *e);
static void nd_open(int32_t rename_id, const char *current);
static void player_row_cb(lv_event_t *e);
static void player_rename_cb(lv_event_t *e);
static void player_delete_confirm_cb(lv_event_t *e);
static void player_delete_cb(lv_event_t *e);
static void player_add_cb(lv_event_t *e);
static stats_player_t *player_current(void);
static int av_group_end(int g);
static void av_rows_init(void);
static int av_row_group(int row);
static void player_avatar_set(int k);
static void av_none_cb(lv_event_t *e);
static void av_grid_click_cb(lv_event_t *e);
static void av_grid_draw_cb(lv_event_t *e);
static void av_scroll_cb(lv_event_t *e);
static void av_group_cb(lv_event_t *e);
static void player_avatar_cb(lv_event_t *e);
static void player_menu_cb(lv_event_t *e);
static void player_color_cb(lv_event_t *e);

/* players screen */
static lv_obj_t *p_list, *p_menu_btn, *p_note;
static stats_player_t p_players[STATS_MAX_PLAYERS];
static int p_count = -1;                 /* last list from sudoku.db, -1 = none yet */

/* ------------------------------------------------------------- players */

/* name dialog (add or rename), on the top layer with an on-screen keyboard */
static lv_obj_t *nd_ovl, *nd_ta;
static int32_t nd_rename_id;             /* 0 = add a player */
static void (*nd_done)(const char *name); /* not a player's name: who takes it */

static void nd_close(void)
{
    if (!nd_ovl) {
        return;
    }
    lv_obj_delete_async(nd_ovl);
    nd_ovl = nd_ta = NULL;
}

static void nd_ok(void)
{
    const char *name = lv_textarea_get_text(nd_ta);
    if (nd_done) {
        nd_done(name);
    } else if (nd_rename_id) {
        stats_db_rename_player(nd_rename_id, name);
    } else {
        stats_db_add_player(name);
    }
    nd_close();
}

static void nd_kb_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        nd_ok();
    } else if (code == LV_EVENT_CANCEL) {
        nd_close();
    }
}

static void nd_btn_cb(lv_event_t *e)
{
    if ((intptr_t)lv_event_get_user_data(e)) {
        nd_ok();
    } else {
        nd_close();
    }
}

/* the name dialog: a title, a text box with a keyboard, Cancel and ok_text; done gets the name
 * (NULL: a player's, added or renamed: nd_open) */
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

static void nd_open(int32_t rename_id, const char *current)
{
    nd_open_ex(rename_id ? "Rename player" : "New player", current, STORE_NAME_LEN - 1, rename_id ? "Rename" : "Add",
               NULL);
    nd_rename_id = rename_id;
}

static void player_row_cb(lv_event_t *e)
{
    int32_t id = (int32_t)(intptr_t)lv_event_get_user_data(e);
    if (id && id != store_player_id()) {
        stats_db_switch_player(id);
    }
}

static void player_rename_cb(lv_event_t *e)
{
    if (is_stats_db_ready()) {
        nd_open(store_player_id(), store_player_name());    /* who plays now */
    }
}

static int32_t p_delete_id;

static void player_delete_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (p_delete_id) {
        stats_db_delete_player(p_delete_id);
    }
    p_delete_id = 0;
}

/* the menu's Delete: who plays now (another takes over); to delete someone else, switch first */
static void player_delete_cb(lv_event_t *e)
{
    int32_t id = store_player_id();
    const stats_player_t *pl = NULL;
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == id) {
            pl = &p_players[i];
        }
    }
    if (!pl) {
        return;
    }
    p_delete_id = id;
    char title[48], text[256];
    snprintf(title, sizeof(title), "Delete %s?", pl->name);
    snprintf(text, sizeof(text),
             "Their statistics (%u solved puzzle%s), solve history (%lu solve%s) and game in progress "
             "are erased from sudoku.db. This can't be undone.%s",
             (unsigned)pl->solved, pl->solved == 1 ? "" : "s",
             (unsigned long)pl->solves, pl->solves == 1 ? "" : "s",
             id == store_player_id() ? "\nAnother player takes over." : "");
    lv_obj_t *m = make_msgbox(title, text);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_TRASH "  Delete", player_delete_confirm_cb, true);
    lv_obj_t *del = lv_obj_get_child(lv_msgbox_get_footer(m), -1);
    lv_obj_set_style_bg_color(del, C_RED, 0);
}

static void player_add_cb(lv_event_t *e)
{
    if (is_stats_db_ready()) {
        nd_open(0, NULL);
    }
}

/* who plays now, in the list from sudoku.db (NULL: not there yet) */
static stats_player_t *player_current(void)
{
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == store_player_id()) {
            return &p_players[i];
        }
    }
    return NULL;
}
static lv_obj_t *av_scroll, *av_grid, *av_group_btn[AVATAR_GROUPS];
static int av_cur;                                  /* the player's, 0 = none */
static int av_row0[AVATAR_GROUPS + 1];              /* each group starts a row: its first; [n] = all rows */

static int av_group_end(int g) { return g + 1 < AVATAR_GROUPS ? avatar_group_first(g + 1) : AVATAR_COUNT; }

static void av_rows_init(void)
{
    for (int g = 0, r = 0; g <= AVATAR_GROUPS; g++) {
        av_row0[g] = r;
        if (g < AVATAR_GROUPS) {
            r += (av_group_end(g) - avatar_group_first(g) + AV_COLS - 1) / AV_COLS;
        }
    }
}

/* the group of a row */
static int av_row_group(int row)
{
    int g = 0;
    while (g + 1 < AVATAR_GROUPS && av_row0[g + 1] <= row) {
        g++;
    }
    return g;
}

static void player_avatar_set(int k)
{
    stats_player_t *pl = player_current();
    ctx_menu_close();
    av_scroll = av_grid = NULL;
    if (!pl) {
        return;
    }
    stats_db_set_player_avatar(pl->id, k);           /* the list comes back with it */
    pl->avatar = (uint16_t)k;
    render_players();
}

static void av_none_cb(lv_event_t *e)
{
    player_avatar_set(0);
}

static void av_grid_click_cb(lv_event_t *e)
{
    lv_point_t p;
    lv_area_t a;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_obj_get_coords(av_grid, &a);
    int col = (p.x - a.x1) / AV_CELL, row = (p.y - a.y1) / AV_CELL;
    if (col < 0 || col >= AV_COLS || row < 0 || row >= av_row0[AVATAR_GROUPS]) {
        return;
    }
    int g = av_row_group(row), k = avatar_group_first(g) + (row - av_row0[g]) * AV_COLS + col;
    if (k < av_group_end(g)) {
        player_avatar_set(k + 1);    /* (not the empty end of a group's last row) */
    }
}

static void av_grid_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(av_grid, &a);
    int top = lv_obj_get_scroll_y(av_scroll), h = lv_obj_get_height(av_scroll);
    int r0 = top / AV_CELL - 1, r1 = (top + h) / AV_CELL + 1;   /* just the rows in view */
    if (r0 < 0) {
        r0 = 0;
    }
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    for (int r = r0; r <= r1 && r < av_row0[AVATAR_GROUPS]; r++) {
        int g = av_row_group(r);
        for (int c = 0; c < AV_COLS; c++) {
            int k = avatar_group_first(g) + (r - av_row0[g]) * AV_COLS + c;
            if (k >= av_group_end(g)) {
                break;
            }
            lv_area_t cell = {a.x1 + c * AV_CELL, a.y1 + r * AV_CELL, a.x1 + (c + 1) * AV_CELL - 1,
                              a.y1 + (r + 1) * AV_CELL - 1
                             };
            if (k + 1 == av_cur) {                   /* the one they have */
                lv_draw_rect_dsc_t rd;
                lv_draw_rect_dsc_init(&rd);
                rd.bg_opa = LV_OPA_TRANSP;
                rd.border_color = C_ACCENT;
                rd.border_width = 3;
                rd.radius = 12;
                lv_draw_rect(layer, &rd, &cell);
            }
            int o = (AV_CELL - AVATAR_SIZE) / 2;
            lv_area_t im = {cell.x1 + o, cell.y1 + o, cell.x1 + o + AVATAR_SIZE - 1, cell.y1 + o + AVATAR_SIZE - 1};
            d.src = avatar_image(k);
            lv_draw_image(layer, &d, &im);
        }
    }
}

/* the group at the top of the view, checked in the row of groups */
static void av_scroll_cb(lv_event_t *e)
{
    int g = av_row_group((lv_obj_get_scroll_y(av_scroll) + AV_CELL / 2) / AV_CELL);
    for (int i = 0; i < AVATAR_GROUPS; i++) {
        lv_obj_set_state(av_group_btn[i], LV_STATE_CHECKED, i == g);
    }
}

static void av_group_cb(lv_event_t *e)
{
    int g = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_scroll_to_y(av_scroll, av_row0[g] * AV_CELL, LV_ANIM_ON);
}

/* the menu's Avatar, for who plays now */
static void player_avatar_cb(lv_event_t *e)
{
    stats_player_t *pl = player_current();
    if (!pl) {
        return;
    }
    av_cur = pl->avatar <= AVATAR_COUNT ? pl->avatar : 0;
    av_rows_init();
    const int inner = AV_COLS * AV_CELL, card_h = SCR_W - 68 - 16;
    lv_obj_t *card = ctx_card_open(p_menu_btn, inner + 2 * 6 + 2);
    lv_obj_set_height(card, card_h);

    /* the title and None */
    lv_obj_t *top = lv_obj_create(card);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, inner, 44);
    lv_obj_t *t = make_label(top, &lv_font_montserrat_18, C_TEXT);
    lv_label_set_text_fmt(t, "Avatar for %s", pl->name);
    lv_obj_set_width(t, inner - 120);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_t *none = make_btn(top, 0, 0, 104, 40, LV_SYMBOL_CLOSE "  None", &lv_font_montserrat_16, av_none_cb, NULL);
    lv_obj_set_style_bg_color(none, C_CARD2, 0);
    if (!av_cur) {
        lv_obj_set_style_border_color(none, C_ACCENT, 0);
        lv_obj_set_style_border_width(none, 2, 0);
    }
    lv_obj_align(none, LV_ALIGN_RIGHT_MID, 0, 0);

    /* the groups: each one's first emoji */
    lv_obj_t *gr = lv_obj_create(card);
    lv_obj_remove_style_all(gr);
    lv_obj_set_size(gr, inner, 48);
    lv_obj_set_flex_flow(gr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int g = 0; g < AVATAR_GROUPS; g++) {
        lv_obj_t *b = make_btn(gr, 0, 0, 46, 46, NULL, NULL, av_group_cb, (void *)(intptr_t)g);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_CHECKED);   /* (not the accent: the emoji's colours) */
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
        lv_obj_t *im = lv_image_create(b);
        lv_image_set_src(im, avatar_image(avatar_group_first(g)));
        lv_obj_center(im);
        av_group_btn[g] = b;
    }
    lv_obj_t *line = lv_obj_create(card);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, inner, 1);
    lv_obj_set_style_bg_color(line, C_CARD2, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

    /* the emoji */
    av_scroll = lv_obj_create(card);
    lv_obj_remove_style_all(av_scroll);
    lv_obj_set_width(av_scroll, inner);
    lv_obj_set_flex_grow(av_scroll, 1);
    lv_obj_set_scroll_dir(av_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(av_scroll, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_event_cb(av_scroll, av_scroll_cb, LV_EVENT_SCROLL, NULL);
    av_grid = lv_obj_create(av_scroll);
    lv_obj_remove_style_all(av_grid);
    lv_obj_set_size(av_grid, inner, av_row0[AVATAR_GROUPS] * AV_CELL);
    lv_obj_set_scrollable(av_grid, false);
    lv_obj_set_clickable(av_grid, true);
    lv_obj_add_event_cb(av_grid, av_grid_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(av_grid, av_grid_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_update_layout(card);
    if (av_cur) {                                    /* to theirs */
        int g = 0;
        while (g + 1 < AVATAR_GROUPS &avatar_group_first(g + 1) <= av_cur - 1) {
            g++;
        }
        lv_obj_scroll_to_y(av_scroll, (av_row0[g] + (av_cur - 1 - avatar_group_first(g)) / AV_COLS) * AV_CELL,
                           LV_ANIM_OFF);
    }
    av_scroll_cb(NULL);
}

/* the title row's context menu: Add, Rename and Avatar (who plays now), Delete (who plays now: not
 * the only one) */
static void player_menu_cb(lv_event_t *e)
{
    bool db = is_stats_db_ready() && p_count >= 0;
    const ctx_item_t items[] = {
        {LV_SYMBOL_PLUS, "Add player", player_add_cb, !db},
        {LV_SYMBOL_EDIT, "Rename player", player_rename_cb, !db},
        {LV_SYMBOL_IMAGE, "Avatar", player_avatar_cb, !db || !player_current()},
        {LV_SYMBOL_TRASH, "Delete player", player_delete_cb, !db || p_count < 2, true},
    };
    ctx_menu_toggle(p_menu_btn, items, (int)(sizeof(items) / sizeof(items[0])));
}

/* one row per player; without sudoku.db just who the board thinks plays */
static void player_color_cb(lv_event_t *e)
{
    int32_t id = (int32_t)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < p_count; i++) {
        if (p_players[i].id == id) {
            stats_db_set_player_color(id, p_players[i].color + 1);   /* the list comes back with it */
            p_players[i].color = (uint8_t)((p_players[i].color + 1) % STATS_PLAYER_COLORS);
        }
    }
    render_players();
}

void players_take(const stats_player_t *list, int n)
{
    memcpy(p_players, list, sizeof(stats_player_t) * n);
    p_count = n;
    render_players();
}

void render_players(void)
{
    home_player_update();                           /* (the avatar on Home's Players button) */
    if (!p_list) {
        return;
    }
    lv_obj_clean(p_list);
    bool db = is_stats_db_ready() && p_count >= 0;
    int n = db ? p_count : 1;
    for (int i = 0; i < n; i++) {
        stats_player_t ram = {.id = store_player_id()};
        if (!db) {
            snprintf(ram.name, sizeof(ram.name), "%s", store_player_name());
        }
        const stats_player_t *pl = db ? &p_players[i] : &ram;
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

/* ---- accessors: the other UI files reach this file's state through these */
const stats_player_t *players_all(void) { return p_players; }
int players_count(void) { return p_count; }
void players_set_count(int v) { p_count = v; }
