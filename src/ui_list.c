/* sudoku_ui.c split: the Puzzles screen. See sudoku_ui.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void list_btnm_draw_cb(lv_event_t *e);
static void list_row_cb(lv_event_t *e);
static void list_row_del_cb(lv_event_t *e);
static void list_add_row(int i, uint16_t id, const char *sub, lv_color_t sub_c, lv_event_cb_t cb, const char *act, lv_color_t act_c, lv_event_cb_t act_cb);
static void star_row_cb(lv_event_t *e);
static void star_row_off_cb(lv_event_t *e);
static void list_fill_starred(void);
static void list_set_starred(bool on);
static void list_star_cb(lv_event_t *e);
static void list_del_confirm_cb(lv_event_t *e);
static void list_del_cb(lv_event_t *e);
static void coll_click_cb(lv_event_t *e);
static void list_export_cb(lv_event_t *e);
static void list_tab_cb(lv_event_t *e);
static void home_dots(void);
static void btnm_cb(lv_event_t *e);
static void new_menu_cb(lv_event_t *e);

/* ============================================================= list screen */

/* the unfinished games, from sudoku.db (without it: just the one in RAM) */
void games_refresh(void)
{
    int n = stats_db_ready() ? stats_db_list_games(s_games) : -1;
    s_ngames = n < 0 ? 0 : n;
}

bool game_in_progress(uint16_t id)
{
    if (store_saved_game()->id == id) return true;
    for (int i = 0; i < s_ngames; i++) {
        if (s_games[i].puzzle == id) return true;
    }
    return false;
}

static void list_btnm_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (base->part != LV_PART_ITEMS) return;
    if (l_coll >= coll_total() || (int)base->id1 >= coll_size(l_coll)) {   /* a place past the end: nothing */
        if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_FILL) lv_draw_task_get_fill_dsc(t)->opa = LV_OPA_TRANSP;
        else if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_BORDER) lv_draw_task_get_border_dsc(t)->opa = LV_OPA_TRANSP;
        return;
    }
    uint16_t id = coll_puzzle(l_coll, (int)base->id1);
    bool solved = l_coll < PUZZLE_COLLECTION_COUNT ? store_best_time(id) != 0
                                                   : (base->id1 < 128 && (l_solved_bits[base->id1 >> 3] >> (base->id1 & 7) & 1));
    bool active = game_in_progress(id);
    if (!solved && !active) return;
    if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_FILL) {
        lv_draw_fill_dsc_t *f = lv_draw_task_get_fill_dsc(t);
        if (lv_buttonmatrix_get_selected_button(base->obj) == base->id1 &&
            lv_obj_has_state(base->obj, LV_STATE_PRESSED)) {
            return;   /* keep the pressed colour */
        }
        f->color = active ? C_LIST_ACTIVE : C_LIST_SOLVED;
    } else if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_LABEL) {
        lv_draw_label_dsc_t *l = lv_draw_task_get_label_dsc(t);
        l->color = lv_color_white();
    }
}

/* ---- the Saved tab's rows */
static void list_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= coll_size(l_coll)) return;
    g_from_coll = l_coll;
    open_puzzle(coll_puzzle(l_coll, i));
}

/* a saved collection's puzzle, taken out (Save on the game screen puts it back) */
static void list_row_del_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    int k = l_coll >= PUZZLE_COLLECTION_COUNT ? coll_saved_index(l_coll) : -1;
    if (k < 0 || i < 0 || i >= coll_size(l_coll)) return;
    char name[48], msg[128], cname[STATS_IMPORT_NAME];
    uint16_t id = coll_puzzle(l_coll, i);
    puzzle_name(id, name, sizeof(name));
    snprintf(cname, sizeof(cname), "%s", s_saved[k].name);
    bool last = s_saved[k].count == 1;
    if (!stats_db_saved_remove(s_saved[k].id, id)) {
        sudoku_ui_notice("Couldn't change sudoku.db.", 2500);
        return;
    }
    snprintf(msg, sizeof(msg), last ? "%s taken out of %s: it was the last, so the collection is gone."
                                    : "%s taken out of %s.", name, cname);
    sv_changed();
    list_reveal_coll(LV_ANIM_OFF);
    sudoku_ui_notice(msg, 3000);
}

/* one row of the right pane's list: number, puzzle name, a coloured line under it and
 * an optional icon button on the right (act) */
static void list_add_row(int i, uint16_t id, const char *sub, lv_color_t sub_c, lv_event_cb_t cb,
                         const char *act, lv_color_t act_c, lv_event_cb_t act_cb)
{
    lv_obj_t *r = lv_obj_create(l_list);
    lv_obj_set_size(r, LV_PCT(100), 68);
    lv_obj_set_style_bg_color(r, C_CARD, 0);
    lv_obj_set_style_bg_color(r, C_CARD2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_set_style_pad_ver(r, 0, 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *num = make_label(r, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text_fmt(num, "%d", i + 1);
    lv_obj_set_width(num, 36);
    lv_obj_align(num, LV_ALIGN_LEFT_MID, 0, 0);
    char name[48];
    puzzle_name(id, name, sizeof(name));
    const int w = PANE_W - 28 - 40 - (act ? 60 : 0);
    lv_obj_t *nm = make_label(r, &lv_font_montserrat_20, C_TEXT);
    lv_label_set_text(nm, name);
    lv_obj_set_width(nm, w);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 40, -12);
    lv_obj_t *sl = make_label(r, &lv_font_montserrat_14, sub_c);
    lv_label_set_text(sl, sub);
    lv_obj_set_width(sl, w);
    lv_label_set_long_mode(sl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(sl, LV_ALIGN_LEFT_MID, 40, 14);
    if (act) {
        lv_obj_t *d = make_btn(r, 0, 0, 52, 44, act, strcmp(act, SYMBOL_STAR) ? &lv_font_montserrat_20 : &font_icons_20,
                               act_cb, (void *)(intptr_t)i);
        lv_obj_set_style_bg_color(d, C_CARD2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(d, 0), act_c, 0);
        lv_obj_align(d, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

void list_fill_rows(int lc)
{
    lv_obj_clean(l_list);
    lv_obj_scroll_to_y(l_list, 0, LV_ANIM_OFF);
    bool saved = lc >= PUZZLE_COLLECTION_COUNT && coll_saved_index(lc) >= 0;
    for (int i = 0; i < coll_size(lc) && i < 128; i++) {
        uint16_t id = coll_puzzle(lc, i);
        char sub[96];
        int v = VAR_CLASSIC, lv = 0;
        gen_lookup(id, NULL, &v, &lv);
        bool solved = l_solved_bits[i >> 3] >> (i & 7) & 1, active = game_in_progress(id);
        snprintf(sub, sizeof(sub), "%s %s   -   %s", v >= 0 && v < VAR_COUNT ? VARIANT_NAMES[v] : "",
                 lv >= 0 && lv < GEN_LEVELS ? GEN_LEVEL_NAMES[lv] : "",
                 solved ? "Solved" : active ? "In progress" : "Not solved yet");
        list_add_row(i, id, sub, solved ? C_GREEN : active ? C_ORANGE : C_MUTED, list_row_cb,
                     saved ? LV_SYMBOL_TRASH : NULL, C_RED, list_row_del_cb);   /* take it out of the collection */
    }
}

/* ---- Starred (the star in the title row, a toggle): the player's bookmarks in the
 * right pane, newest first, instead of the collection's puzzles */
static bool      l_starred;
static lv_obj_t *l_star_btn, *l_new_btn;
static uint16_t  l_star_ids[STATS_MAX_BOOKMARKS];
static int       l_nstar;

static void star_row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= l_nstar) return;
    open_puzzle(l_star_ids[i]);
}

/* the row's star: unstarred, the row goes */
static void star_row_off_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= l_nstar) return;
    uint16_t id = l_star_ids[i];
    stats_db_set_bookmark(id, false);
    if (id == g_id) {                               /* (the game's own star) */
        g_starred = false;
        update_header();
    }
    char name[48], msg[80];
    puzzle_name(id, name, sizeof(name));
    snprintf(msg, sizeof(msg), "%s unstarred.", name);
    refresh_list();
    sudoku_ui_notice(msg, 2000);
}

static void list_fill_starred(void)
{
    lv_obj_clean(l_list);
    lv_obj_scroll_to_y(l_list, 0, LV_ANIM_OFF);
    int n = stats_db_bookmarks(l_star_ids, STATS_MAX_BOOKMARKS);
    l_nstar = n < 0 ? 0 : n;
    lv_label_set_text(l_name, "Starred");
    lv_label_set_text_fmt(l_stats, n == 1 ? "1 puzzle" : "%d puzzles", l_nstar);
    if (n <= 0) {
        lv_obj_t *t = make_label(l_list, &lv_font_montserrat_18, C_MUTED);
        lv_obj_set_width(t, LV_PCT(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(t, 40, 0);
        lv_label_set_text(t, n < 0 ? "Stars need sudoku.db on a microSD card."
                                   : "No starred puzzles yet: tap the star next to Share on the game screen to keep "
                                     "one here.");
    }
    for (int i = 0; i < l_nstar; i++) {
        uint16_t id = l_star_ids[i];
        uint32_t b = store_best_time(id);
        char sub[48], t[16];
        if (b) {
            fmt_time(t, sizeof(t), b);
            snprintf(sub, sizeof(sub), "Solved   -   best %s", t);
        } else {
            snprintf(sub, sizeof(sub), game_in_progress(id) ? "In progress" : "Not solved yet");
        }
        list_add_row(i, id, sub, b ? C_GREEN : game_in_progress(id) ? C_ORANGE : C_MUTED, star_row_cb,
                     SYMBOL_STAR, C_ORANGE, star_row_off_cb);
    }
}

static void list_set_starred(bool on)
{
    l_starred = on;
    icon_btn_set(l_star_btn, on, C_ORANGE);
}

static void list_star_cb(lv_event_t *e)
{
    list_set_starred(!l_starred);
    refresh_list();
    if (!l_starred) list_reveal_coll(LV_ANIM_OFF);
}

/* ---- deleting the collection shown (Saved tab) */
static void list_del_confirm_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (l_coll < PUZZLE_COLLECTION_COUNT || l_coll >= coll_total()) return;
    int k = coll_saved_index(l_coll);
    char name[STATS_IMPORT_NAME], msg[96];
    snprintf(name, sizeof(name), "%s", coll_name(l_coll));
    bool ok = k >= 0 ? stats_db_saved_delete(s_saved[k].id) : stats_db_import_delete(name);
    snprintf(msg, sizeof(msg), ok ? "%s deleted." : "Couldn't delete %s.", name);
    imports_reload();
    list_fill_colls();
    l_coll = PUZZLE_COLLECTION_COUNT;               /* the Saved tab's first, if any */
    l_tab_coll[1] = -1;
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    update_header();                                /* (the game screen's Save icon) */
    sudoku_ui_notice(msg, 2500);
}

static void list_del_cb(lv_event_t *e)
{
    if (l_coll < PUZZLE_COLLECTION_COUNT || l_coll >= coll_total()) return;
    bool imported = coll_saved_index(l_coll) < 0;
    int n = coll_size(l_coll);
    char title[64], text[256];
    snprintf(title, sizeof(title), "Delete %s?", coll_name(l_coll));
    char what[48];
    if (n == 1) snprintf(what, sizeof(what), "Its puzzle stays on the board: its");
    else snprintf(what, sizeof(what), "Its %d puzzles stay on the board: their", n);
    snprintf(text, sizeof(text), "The collection goes%s. %s solves, History and Gameplays are kept%s.",
             imported ? " (its file can be imported again)" : "", what,
             imported ? "" : n == 1 ? ", and Save can put it in a collection again"
                                    : ", and Save can put them in a collection again");
    lv_obj_t *m = make_msgbox(title, text);
    msgbox_button(m, "Cancel", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_TRASH "  Delete", list_del_confirm_cb, true);
    lv_obj_set_style_bg_color(lv_obj_get_child(lv_msgbox_get_footer(m), -1), C_RED, 0);
}

void refresh_list(void)
{
    games_refresh();
    if (l_coll >= coll_total()) l_coll = 0;
    if (coll_tab(l_coll) != l_tab) {               /* the tab shown decides: its last, or its first */
        int c = l_tab_coll[l_tab];
        if (c < 0 || c >= coll_total() || coll_tab(c) != l_tab) c = l_tab ? PUZZLE_COLLECTION_COUNT : 0;
        if (c < coll_total()) l_coll = c;
    }
    bool none = coll_tab(l_coll) != l_tab;          /* the Saved tab, with nothing in it */
    for (int i = 0; i < 2; i++) {
        lv_obj_set_state(l_tab_btn[i], LV_STATE_CHECKED, i == l_tab);
        lv_obj_set_style_text_color(lv_obj_get_child(l_tab_btn[i], 0), i == l_tab ? lv_color_white() : C_TEXT, 0);
    }
    lv_obj_set_hidden(l_empty, !none);
    lv_obj_set_hidden(l_btnm, none);
    lv_obj_set_hidden(l_name, none);
    lv_obj_set_hidden(l_stats, none);
    if (!none) l_tab_coll[l_tab] = l_coll;
    bool exportable = !none && coll_saved_index(l_coll) >= 0 && l_coll >= PUZZLE_COLLECTION_COUNT;
    bool listed = !none && l_tab == 1;              /* the Saved tab: rows, not numbers */
    lv_obj_set_hidden(l_export, !exportable);       /* a saved collection: to the card, for Import puzzles */
    lv_obj_set_hidden(l_del, !listed);              /* saved or imported: delete the collection */
    lv_obj_set_width(l_name, PANE_W - 4 - (listed ? 52 + 8 : 0) - (exportable ? 130 + 8 : 0));
    lv_obj_set_hidden(l_btnm, none || listed);
    lv_obj_set_hidden(l_list, !listed);
    if (l_starred) {                                /* the right pane shows Starred: no collection's things */
        lv_obj_set_hidden(l_empty, true);
        lv_obj_set_hidden(l_btnm, true);
        lv_obj_set_hidden(l_export, true);
        lv_obj_set_hidden(l_del, true);
        lv_obj_set_hidden(l_name, false);
        lv_obj_set_hidden(l_stats, false);
        lv_obj_set_hidden(l_list, false);
        lv_obj_set_width(l_name, PANE_W - 4);
    }
    const int lc = l_coll, lsize = coll_size(lc);
    uint16_t total = 0;
    for (int c = 0; c < coll_total(); c++) {
        int size = coll_size(c), k = coll_saved_index(c);
        lv_obj_set_hidden(l_coll_btn[c], coll_tab(c) != l_tab);
        uint16_t n = c < PUZZLE_COLLECTION_COUNT ? store_solved_count(coll_first(c), (uint16_t)size)
                     : k >= 0 ? (uint16_t)stats_db_solved_list(s_saved[k].puzzles, size, c == lc ? l_solved_bits : NULL)
                              : (uint16_t)stats_db_solved_in(coll_first(c), size, c == lc ? l_solved_bits : NULL);
        if (c < PUZZLE_COLLECTION_COUNT) total += n;
        lv_label_set_text_fmt(l_coll_cnt[c], "%u/%u", (unsigned)n, (unsigned)size);
        /* selected row is accent blue: white text there in both themes */
        bool sel = c == l_coll && !none && !l_starred;
        lv_obj_set_state(l_coll_btn[c], LV_STATE_CHECKED, sel);
        lv_obj_set_style_text_color(l_coll_name[c], sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(l_coll_cnt[c], sel ? lv_color_white()
                                                   : (n == size ? C_GREEN : C_MUTED), 0);
        lv_obj_set_style_text_opa(l_coll_cnt[c], sel ? LV_OPA_80 : LV_OPA_COVER, 0);
    }
    lv_label_set_text_fmt(l_total, "%u of %u solved", (unsigned)total, (unsigned)PUZZLE_COUNT);
    lv_obj_invalidate(l_colls);                     /* (rows hidden or shown: no stale scrollbar left) */

    /* collection panel (best times: the built-in ones, which the board keeps in RAM) */
    uint32_t sum = 0, best = 0;
    uint16_t n = 0;
    for (int i = 0; lc < PUZZLE_COLLECTION_COUNT && i < lsize; i++) {
        uint32_t s = store_best_time(coll_first(lc) + i);
        if (!s) continue;
        n++;
        sum += s;
        if (!best || s < best) best = s;
    }
    if (lc >= PUZZLE_COLLECTION_COUNT) {
        for (int i = 0; i < lsize && i < 128; i++) n += l_solved_bits[i >> 3] >> (i & 7) & 1;
    }
    if (listed && !l_starred) list_fill_rows(lc);   /* (after the counts: l_solved_bits is lc's) */
    lv_label_set_text_fmt(l_name, "%s%s", coll_name(lc),
                          lc >= PUZZLE_COLLECTION_COUNT && coll_saved_index(lc) < 0 ? "  (imported)" : "");
    if (n && best) {
        char b[16], a[16];
        fmt_time(b, sizeof(b), best);
        fmt_time(a, sizeof(a), sum / n);
        lv_label_set_text_fmt(l_stats, "%u / %u solved   -   fastest %s   -   average %s",
                              (unsigned)n, (unsigned)lsize, b, a);
    } else {
        lv_label_set_text_fmt(l_stats, "%u / %u solved", (unsigned)n, (unsigned)lsize);
    }

    /* puzzle numbers 1..count, 10 per row; always all 100 places (the ones past count
     * disabled and not drawn: list_btnm_draw_cb), so a small collection's buttons are as
     * big as everywhere else (hidden ones would give their room to the others) */
    int m = 0;
    int cnt = lsize > MAX_PER_COLL ? MAX_PER_COLL : lsize;
    for (int i = 0; i < MAX_PER_COLL; i++) {
        if (i && i % 10 == 0) l_map[m++] = "\n";
        if (i < cnt) snprintf(l_labels[i], sizeof(l_labels[i]), "%d", i + 1);
        else snprintf(l_labels[i], sizeof(l_labels[i]), " ");   /* ("" would end the map) */
        l_map[m++] = l_labels[i];
    }
    l_map[m] = "";
    lv_buttonmatrix_set_map(l_btnm, l_map);
    lv_buttonmatrix_clear_button_ctrl_all(l_btnm, LV_BUTTONMATRIX_CTRL_DISABLED);
    for (int i = cnt; i < MAX_PER_COLL; i++) {
        lv_buttonmatrix_set_button_ctrl(l_btnm, (uint32_t)i, LV_BUTTONMATRIX_CTRL_DISABLED);
    }
    if (l_starred) list_fill_starred();            /* (its name and count over the collection's) */
}

/* scrolls the collection column so the selected one is visible */
void list_reveal_coll(lv_anim_enable_t anim)
{
    lv_obj_update_layout(scr_list);
    if (l_coll < coll_total() && coll_tab(l_coll) == l_tab) lv_obj_scroll_to_view(l_coll_btn[l_coll], anim);
}

void show_list(void)
{
    int gc = g_id != STORE_NO_GAME ? coll_of(g_id) : -1;
    if (gc < 0 && g_id != STORE_NO_GAME && coll_find(g_from_coll, g_id) >= 0) gc = g_from_coll;   /* a saved one */
    if (gc >= 0) {                                  /* the collection of the puzzle just played */
        l_coll = gc;
        l_tab = coll_tab(gc);
        store_set_last_collection((uint8_t)l_coll);  /* (built-in ones only) */
    }
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    lv_screen_load(scr_list);
}

static void coll_click_cb(lv_event_t *e)
{
    l_coll = (int)(intptr_t)lv_event_get_user_data(e);
    list_set_starred(false);
    store_set_last_collection((uint8_t)l_coll);
    refresh_list();
    list_reveal_coll(LV_ANIM_ON);
}

/* Saved tab: the collection shown, written to the card (puzzles/<name>.sdx) */
static void list_export_cb(lv_event_t *e)
{
    int k = l_coll >= PUZZLE_COLLECTION_COUNT ? coll_saved_index(l_coll) : -1;
    if (k < 0) return;
    char msg[96];
    stats_db_saved_export(s_saved[k].id, msg, sizeof(msg));
    sudoku_ui_notice(msg, 4000);
}

static void list_tab_cb(lv_event_t *e)
{
    int t = (int)(intptr_t)lv_event_get_user_data(e);
    if (t == l_tab && !l_starred) return;
    list_set_starred(false);
    l_tab = t;
    lv_obj_scroll_to_y(l_colls, 0, LV_ANIM_OFF);
    refresh_list();                                 /* (takes the tab's last collection) */
    list_reveal_coll(LV_ANIM_OFF);
}

/* the dots under the menu (small circles, the page shown in the accent colour) */
static void home_dots(void)
{
    if (!h_dots) return;
    lv_obj_set_hidden(h_dots, h_npages < 2);
    if (h_npages < 2) return;
    int cur = (int)((lv_obj_get_scroll_x(h_menu) + HOME_GRID_W / 2) / HOME_GRID_W);
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        lv_obj_t *d = lv_obj_get_child(h_dots, p);
        lv_obj_set_hidden(d, p >= h_npages);
        lv_obj_set_style_bg_color(d, p == cur ? C_ACCENT : C_CARD2, 0);
    }
}

void home_scroll_cb(lv_event_t *e)
{
    home_dots();
}

/* Home menu: a column of full-width buttons while they fit between the header and the
 * bottom of the screen; otherwise a grid of two per row; and when even that doesn't fit,
 * pages of the grid side by side, swiped between, with dots under them. */
void home_layout(void)
{
    lv_obj_t *vis[16];
    int n = 0;
    for (int i = 0; i < h_nitems; i++) if (!lv_obj_is_hidden(h_items[i])) vis[n++] = h_items[i];
    const int rows = (HOME_MENU_MAX_H + HOME_GAP) / (HOME_BTN_H + HOME_GAP);   /* that fit under the header */
    bool grid = n > rows;
    int per_page = grid ? rows * 2 : rows;
    int npages = (n + per_page - 1) / per_page;
    if (npages > HOME_PAGES_MAX) npages = HOME_PAGES_MAX;
    int sig = (grid ? 1000 : 0) + n * 10 + npages;      /* (the same layout: nothing to do) */
    if (sig == h_menu_grid) return;
    h_menu_grid = sig;
    h_npages = npages;
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        lv_obj_set_flex_flow(h_page[p], grid ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(h_page[p], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_hidden(h_page[p], p >= npages);
    }
    for (int i = 0; i < h_nitems; i++) {               /* hidden ones wait on page 1 */
        if (lv_obj_is_hidden(h_items[i])) lv_obj_set_parent(h_items[i], h_page[0]);
    }
    for (int k = 0; k < n; k++) {
        int p = k / per_page;
        if (p >= npages) p = npages - 1;
        lv_obj_set_parent(vis[k], h_page[p]);
        lv_obj_move_to_index(vis[k], -1);              /* (in order on its page) */
        lv_obj_set_width(vis[k], grid ? (HOME_GRID_W - HOME_GAP) / 2 : HOME_COL_W);
    }
    lv_obj_set_scrollable(h_menu, npages > 1);
    lv_obj_update_layout(h_menu);
    if (lv_obj_get_scroll_x(h_menu) > (npages - 1) * HOME_GRID_W) lv_obj_scroll_to_x(h_menu, 0, LV_ANIM_OFF);
    home_dots();
}

static void btnm_cb(lv_event_t *e)
{
    uint32_t idx = lv_buttonmatrix_get_selected_button(l_btnm);
    if (idx == LV_BUTTONMATRIX_BUTTON_NONE) return;
    if ((int)idx >= coll_size(l_coll)) return;
    g_from_coll = l_coll;
    open_puzzle(coll_puzzle(l_coll, (int)idx));
}

/* Left-hand column card shared by the list and statistics screens. */
lv_obj_t *make_coll_column(lv_obj_t *parent)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_pos(list, 12, 76);
    lv_obj_set_size(list, COLL_W, SCR_W - 76 - 12);
    lv_obj_set_style_bg_color(list, C_CARD, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 12, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    return list;
}

/* One row of that column: name on the left, a small count on the right. */
lv_obj_t *make_coll_btn(lv_obj_t *list, const char *name, lv_obj_t **name_lbl, lv_obj_t **cnt_lbl,
                               lv_event_cb_t cb, int user)
{
    lv_obj_t *b = lv_button_create(list);
    lv_obj_set_size(b, LV_PCT(100), 50);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    *name_lbl = make_label(b, &lv_font_montserrat_16, C_TEXT);
    lv_label_set_text(*name_lbl, name);
    lv_obj_align(*name_lbl, LV_ALIGN_LEFT_MID, 0, 0);
    *cnt_lbl = make_label(b, &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(*cnt_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)user);
    return b;
}

/* ---- the title row's context menu (the vertical dots): New puzzle, Enter, Learn */
static void new_menu_cb(lv_event_t *e)
{
    static const ctx_item_t ITEMS[] = {
        {LV_SYMBOL_PLUS, "New puzzle", new_puzzle_cb},
        {LV_SYMBOL_KEYBOARD, "Enter puzzle", enter_cb},      /* type one in from a book */
        {LV_SYMBOL_EYE_OPEN, "Learn", learn_cb},             /* practise a technique */
    };
    ctx_menu_toggle(l_new_btn, ITEMS, (int)(sizeof(ITEMS) / sizeof(ITEMS[0])));
}

/* the rows of the collection column (name + solved count), one per collection; again
 * when a collection comes or goes (Save) */
void list_fill_colls(void)
{
    lv_obj_clean(l_colls);
    memset(l_coll_btn, 0, sizeof(l_coll_btn));
    imports_reload();                               /* the imported collections follow the built-in ones */
    for (int c = 0; c < coll_total(); c++) {
        l_coll_btn[c] = make_coll_btn(l_colls, coll_name(c), &l_coll_name[c], &l_coll_cnt[c], coll_click_cb, c);
        lv_obj_set_size(l_coll_name[c], COLL_W - 12 - 24 - 58,      /* one line, room for "100/100" */
                        lv_font_get_line_height(&lv_font_montserrat_16));
        lv_label_set_long_mode(l_coll_name[c], LV_LABEL_LONG_MODE_DOTS);
    }
}

void build_list(void)
{
    scr_list = make_screen();

    make_btn(scr_list, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_list, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Puzzles");
    lv_obj_set_pos(t, 150, 20);
    l_total = make_label(scr_list, &lv_font_montserrat_16, C_MUTED);
    /* right of the title: the solved count, Starred (a toggle: the starred puzzles in the
     * right pane) and the context menu (New puzzle, Enter: type one in from a book, Learn) */
    l_new_btn = make_btn(scr_list, SCR_W - 12 - L_ICON_W, 10, L_ICON_W, 52, SYMBOL_MORE, &font_icons_24,
                         new_menu_cb, NULL);
    l_star_btn = make_btn(scr_list, SCR_W - 12 - 2 * L_ICON_W - 8, 10, L_ICON_W, 52, SYMBOL_STAR, &font_icons_24,
                          list_star_cb, NULL);
    icon_btn_set(l_star_btn, l_starred, C_ORANGE);
    lv_obj_align(l_total, LV_ALIGN_TOP_RIGHT, -(12 + 2 * L_ICON_W + 8 + 14), 28);

    /* left pane: the tabs (the built-in collections, or the player's: imported and saved),
     * and under them that tab's collections in a column that scrolls */
    static const char *const TAB[2] = {"Built-in", "Saved"};
    const int tw = (COLL_W - 6) / 2;
    for (int i = 0; i < 2; i++) {
        l_tab_btn[i] = make_btn(scr_list, 12 + i * (COLL_W - tw), L_PANE_Y, tw, L_TAB_H, TAB[i],
                                &lv_font_montserrat_16, list_tab_cb, (void *)(intptr_t)i);
        lv_obj_set_style_bg_color(l_tab_btn[i], C_CARD, 0);
        lv_obj_set_style_bg_color(l_tab_btn[i], C_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_radius(l_tab_btn[i], 10, 0);
    }
    l_colls = make_coll_column(scr_list);
    lv_obj_set_pos(l_colls, 12, L_PANE_Y + L_TAB_H + 8);
    lv_obj_set_size(l_colls, COLL_W, SCR_W - (L_PANE_Y + L_TAB_H + 8) - 12);
    lv_obj_set_scroll_dir(l_colls, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l_colls, LV_SCROLLBAR_MODE_AUTO);
    list_fill_colls();

    /* right pane: the selected collection's name and stats, Export / Delete, its puzzles */
    l_name = make_label(scr_list, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_long_mode(l_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l_name, PANE_X + 4, L_PANE_Y + 2);
    lv_obj_set_width(l_name, PANE_W - 4);
    l_stats = make_label(scr_list, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(l_stats, PANE_X + 4, L_PANE_Y + 34);
    lv_obj_set_width(l_stats, PANE_W - 4);
    lv_label_set_long_mode(l_stats, LV_LABEL_LONG_MODE_DOTS);
    l_export = make_btn(scr_list, 0, 0, 130, 40, LV_SYMBOL_UPLOAD "  Export", &lv_font_montserrat_16, list_export_cb,
                        NULL);                      /* a saved collection to puzzles/<name>.sdx */
    lv_obj_align(l_export, LV_ALIGN_TOP_RIGHT, -12 - 52 - 8, L_PANE_Y - 4);
    l_del = make_btn(scr_list, 0, 0, 52, 40, LV_SYMBOL_TRASH, &lv_font_montserrat_18, list_del_cb, NULL);
    lv_obj_set_style_bg_color(l_del, C_CARD, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(l_del, 0), C_RED, 0);
    lv_obj_align(l_del, LV_ALIGN_TOP_RIGHT, -12, L_PANE_Y - 4);
    lv_obj_set_hidden(l_del, true);
    lv_obj_set_style_text_color(lv_obj_get_child(l_export, 0), C_ACCENT, 0);
    lv_obj_set_hidden(l_export, true);

    l_btnm = lv_buttonmatrix_create(scr_list);
    lv_obj_set_pos(l_btnm, PANE_X, L_GRID_Y);
    lv_obj_set_size(l_btnm, PANE_W, SCR_W - L_GRID_Y - 24);   /* cells about 39 x 45; the credit below */
    lv_obj_set_style_bg_opa(l_btnm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l_btnm, 0, 0);
    lv_obj_set_style_pad_all(l_btnm, 0, 0);
    lv_obj_set_style_pad_gap(l_btnm, 5, 0);
    lv_obj_set_style_bg_color(l_btnm, C_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(l_btnm, C_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(l_btnm, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(l_btnm, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(l_btnm, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(l_btnm, 6, LV_PART_ITEMS);
    lv_obj_set_send_draw_task_events(l_btnm, true);
    lv_obj_add_event_cb(l_btnm, list_btnm_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_event_cb(l_btnm, btnm_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* the Saved tab: the collection's puzzles as rows (name, variant, level, solved) */
    l_list = lv_obj_create(scr_list);
    lv_obj_remove_style_all(l_list);
    lv_obj_set_pos(l_list, PANE_X, L_GRID_Y);
    lv_obj_set_size(l_list, PANE_W, SCR_W - L_GRID_Y - 24);
    lv_obj_set_flex_flow(l_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l_list, 6, 0);
    lv_obj_set_scroll_dir(l_list, LV_DIR_VER);
    lv_obj_set_hidden(l_list, true);

    /* the Saved tab with nothing in it */
    l_empty = make_label(scr_list, &lv_font_montserrat_20, C_MUTED);
    lv_obj_set_width(l_empty, PANE_W - 24);
    lv_label_set_long_mode(l_empty, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(l_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l_empty, "Nothing saved yet.\n\nOpen a puzzle made with New puzzle or Enter and tap "
                               LV_SYMBOL_SAVE " next to the star to put it in a collection of your own. "
                               "Collections imported from the card (Settings > Data > Import puzzles) are here "
                               "too.");
    lv_obj_set_pos(l_empty, PANE_X + 12, L_GRID_Y + 20);
    lv_obj_set_hidden(l_empty, true);

    /* attribution */
    lv_obj_t *src = make_label(scr_list, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(src, "Puzzles: opensudoku.gitlab.io");
    lv_obj_align(src, LV_ALIGN_BOTTOM_RIGHT, -14, -2);
}
