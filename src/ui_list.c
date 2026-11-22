/**
 * @file ui_list.c
 * @ingroup ui_list
 * @brief Puzzles: the screen (title row, tabs, the collection column, the panel, the grid of numbers),
 *        and refreshing it. Its parts: ui_list_part.h; the data: puzzles_core.h.
 */
#include "ui_internal.h"
#include "ui_list_part.h"
#include "puzzles_core.h"

static void list_btnm_draw_cb(lv_event_t *e);
static void coll_click_cb(lv_event_t *e);
static void list_tab_cb(lv_event_t *e);
static void btnm_cb(lv_event_t *e);

static lv_obj_t *l_total;                 /**< "19 of 1600 solved" */
static lv_obj_t *l_colls;                 /**< the collection column */
static lv_obj_t *l_coll_btn[L_COLLS];     /**< its rows */
static lv_obj_t *l_coll_cnt[L_COLLS];     /**< their counts */
static lv_obj_t *l_coll_name[L_COLLS];    /**< their names */
static lv_obj_t *l_tab_btn[COLL_TABS];    /**< Built-in, Saved, Imported */
static lv_obj_t *l_empty;                 /**< the Saved or Imported tab with nothing in it: what goes there */
static lv_obj_t *l_list;                  /**< the right pane's rows (ui_list_rows.c) */
static lv_obj_t *l_name;                  /**< the collection's name */
static lv_obj_t *l_stats;                 /**< its solved, fastest, average */
static lv_obj_t *l_btnm;                  /**< the grid of numbers (built-in collections) */
static lv_obj_t *l_new_btn;               /**< the title row's ⋮ menu */
static char      l_labels[MAX_PER_COLL][4];                    /**< the grid's numbers */
static const char *l_map[MAX_PER_COLL + MAX_PER_COLL / 10 + 1];   /**< the grid's map */

/**
 * @brief Whether a puzzle is one of the current player's unfinished games.
 * @param[in] id  the puzzle
 * @return true: in progress
 */
bool is_game_in_progress(uint16_t id)
{
    if (store_saved_game()->id == id) {
        return true;
    }
    for (int i = 0; i < ui_ngames(); i++) {
        if (ui_games()[i].puzzle == id) {
            return true;
        }
    }
    return false;
}

/**
 * @brief The grid's look: solved puzzles green, the ones in progress orange (white numbers); the
 *        places past the collection's end not drawn.
 * @param[in] e  the draw task event
 */
static void list_btnm_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (base->part != LV_PART_ITEMS) {
        return;
    }
    int lc = puzzles_coll();
    if (lc >= coll_total() || (int)base->id1 >= coll_size(lc)) {   /* a place past the end: nothing */
        if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_FILL) {
            lv_draw_task_get_fill_dsc(t)->opa = LV_OPA_TRANSP;
        } else if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_BORDER) {
            lv_draw_task_get_border_dsc(t)->opa = LV_OPA_TRANSP;
        }
        return;
    }
    uint16_t id = coll_puzzle(lc, (int)base->id1);
    bool solved = lc < PUZZLE_COLLECTION_COUNT ? store_best_time(id) != 0 : is_puzzles_solved((int)base->id1);
    bool active = is_game_in_progress(id);
    if (!solved && !active) {
        return;
    }
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

/** @brief Draws the screen for what's shown: the tabs, the collection column's counts, the panel, the grid or rows (or Starred). */
void refresh_list(void)
{
    games_refresh();
    bool none = !puzzles_settle();                  /* the Saved or Imported tab, with nothing in it */
    const int l_tab = puzzles_tab(), l_coll = puzzles_coll();
    const bool l_starred = is_puzzles_starred();
    for (int i = 0; i < COLL_TABS; i++) {
        lv_obj_set_state(l_tab_btn[i], LV_STATE_CHECKED, i == l_tab);
        lv_obj_set_style_text_color(lv_obj_get_child(l_tab_btn[i], 0), i == l_tab ? lv_color_white() : C_TEXT, 0);
    }
    lv_label_set_text(l_empty, l_tab == COLL_TAB_IMPORTED
                      ? "Nothing imported yet.\n\nPut puzzle files (sdm, sdx or OpenSudoku) in the puzzles folder "
                      "on the card, then Settings > Data > Import puzzles, or import one from Files."   /* (no '.' to break at) */
                      : "Nothing saved yet.\n\nOpen a puzzle made with New puzzle or Enter and tap "
                      LV_SYMBOL_SAVE " next to the star to put it in a collection of your own.");
    lv_obj_set_hidden(l_empty, !none);
    lv_obj_set_hidden(l_btnm, none);
    lv_obj_set_hidden(l_name, none);
    lv_obj_set_hidden(l_stats, none);
    bool listed = !none && l_tab != COLL_TAB_BUILTIN;   /* Saved, Imported: rows, not numbers */
    lv_obj_set_hidden(l_btnm, none || listed);       /* (Export, Delete: the ⋮ menu, list_coll_actions()) */
    lv_obj_set_hidden(l_list, !listed);
    if (l_starred) {                                /* the right pane shows Starred: no collection's things */
        lv_obj_set_hidden(l_empty, true);
        lv_obj_set_hidden(l_btnm, true);
        lv_obj_set_hidden(l_name, false);
        lv_obj_set_hidden(l_stats, false);
        lv_obj_set_hidden(l_list, false);
        lv_obj_set_width(l_name, PANE_W - 4);
    }
    const int lc = l_coll, lsize = coll_size(lc);
    uint16_t total = 0;
    for (int c = 0; c < coll_total(); c++) {
        int size = coll_size(c);
        lv_obj_set_hidden(l_coll_btn[c], coll_tab(c) != l_tab);
        uint16_t n = puzzles_solved_count(c, lc);
        if (c < PUZZLE_COLLECTION_COUNT) {
            total += n;
        }
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
    uint32_t sum, best;
    uint16_t n;
    puzzles_panel(lc, &n, &best, &sum);
    if (listed && !l_starred) {
        list_fill_rows(lc);    /* (after the counts: the solved ones are lc's) */
    }
    lv_label_set_text(l_name, coll_name(lc));       /* (the tab says whose it is) */
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
        if (i && i % 10 == 0) {
            l_map[m++] = "\n";
        }
        if (i < cnt) {
            snprintf(l_labels[i], sizeof(l_labels[i]), "%d", i + 1);
        } else {
            snprintf(l_labels[i], sizeof(l_labels[i]), " ");    /* ("" would end the map) */
        }
        l_map[m++] = l_labels[i];
    }
    l_map[m] = "";
    lv_buttonmatrix_set_map(l_btnm, l_map);
    lv_buttonmatrix_clear_button_ctrl_all(l_btnm, LV_BUTTONMATRIX_CTRL_DISABLED);
    for (int i = cnt; i < MAX_PER_COLL; i++) {
        lv_buttonmatrix_set_button_ctrl(l_btnm, (uint32_t)i, LV_BUTTONMATRIX_CTRL_DISABLED);
    }
    if (l_starred) {                                /* its name and count over the collection's */
        int ns = list_fill_starred();
        lv_label_set_text(l_name, "Starred");
        lv_label_set_text_fmt(l_stats, ns == 1 ? "1 puzzle" : "%d puzzles", puzzles_starred_count());
    }
}

/**
 * @brief Scrolls the collection column so the one shown is visible.
 * @param[in] anim  animated or not
 */
void list_reveal_coll(lv_anim_enable_t anim)
{
    lv_obj_update_layout(screen_list());
    int lc = puzzles_coll();
    if (lc < coll_total() && coll_tab(lc) == puzzles_tab()) {
        lv_obj_scroll_to_view(l_coll_btn[lc], anim);
    }
}

/** @brief Opens the screen, on the collection of the puzzle just played if there's one. */
void show_list(void)
{
    int gc = game_id() != STORE_NO_GAME ? coll_of(game_id()) : -1;
    if (gc < 0 && game_id() != STORE_NO_GAME && coll_find(game_from_coll(), game_id()) >= 0) {
        gc = game_from_coll();    /* a saved one */
    }
    if (gc >= 0) {                                  /* the collection of the puzzle just played */
        puzzles_follow(gc);
        store_set_last_collection((uint8_t)gc);      /* (built-in ones only) */
    }
    refresh_list();
    list_reveal_coll(LV_ANIM_OFF);
    lv_screen_load(screen_list());
}

/**
 * @brief A collection in the column: shown.
 * @param[in] e  the event (user data: the collection)
 */
static void coll_click_cb(lv_event_t *e)
{
    int c = (int)(intptr_t)lv_event_get_user_data(e);
    puzzles_set_coll(c);
    puzzles_set_starred(false);
    store_set_last_collection((uint8_t)c);
    refresh_list();
    list_reveal_coll(LV_ANIM_ON);
}

/**
 * @brief Built-in / Saved / Imported: that tab, on its last collection.
 * @param[in] e  the event (user data: the tab)
 */
static void list_tab_cb(lv_event_t *e)
{
    int t = (int)(intptr_t)lv_event_get_user_data(e);
    if (t == puzzles_tab() && !is_puzzles_starred()) {
        return;
    }
    puzzles_set_starred(false);
    puzzles_set_tab(t);
    lv_obj_scroll_to_y(l_colls, 0, LV_ANIM_OFF);
    refresh_list();                                 /* (takes the tab's last collection) */
    list_reveal_coll(LV_ANIM_OFF);
}

/**
 * @brief A number in the grid: that puzzle, to play.
 * @param[in] e  the event
 */
static void btnm_cb(lv_event_t *e)
{
    uint32_t idx = lv_buttonmatrix_get_selected_button(l_btnm);
    if (idx == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    int lc = puzzles_coll();
    if ((int)idx >= coll_size(lc)) {
        return;
    }
    game_set_from_coll(lc);
    open_puzzle(coll_puzzle(lc, (int)idx));
}

/**
 * @brief The left-hand column card the Puzzles and Statistics screens share.
 * @param[in] parent  the screen
 * @return it
 */
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

/**
 * @brief A row of that column: a name on the left, a small count on the right.
 * @param[in] list      the column
 * @param[in] name      the name
 * @param[out] name_lbl  gets its label
 * @param[out] cnt_lbl   gets the count's label
 * @param[in] cb        a tap on it
 * @param[in] user      the tap's user data
 * @return the row
 */
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

/** @brief The collection column's rows (name + solved count), one per collection; again when one comes or goes (Save, Delete). */
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

/** @brief Builds the screen. */
void build_list(void)
{
    screen_set_list(make_screen());

    make_btn(screen_list(), 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(screen_list(), &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Puzzles");
    lv_obj_set_pos(t, 150, 20);
    l_total = make_label(screen_list(), &lv_font_montserrat_16, C_MUTED);
    /* right of the title: the solved count and the context menu (New puzzle, Enter: type one in
     * from a book, Learn, and Starred puzzles: a toggle, the starred puzzles in the right pane) */
    l_new_btn = make_btn(screen_list(), SCR_W - 12 - L_ICON_W, 10, L_ICON_W, 52, SYMBOL_MORE, &font_icons_24,
                         new_menu_cb, NULL);
    lv_obj_align(l_total, LV_ALIGN_TOP_RIGHT, -(12 + L_ICON_W + 14), 28);

    /* left pane: the tabs (the built-in collections, the player's own made on the board, the ones
     * imported from the card), and under them that tab's collections in a column that scrolls */
    /* as icons: a 3x3 grid (the board's own), the game screen's Save, the Import buttons' download */
    static const char *const TAB[COLL_TABS] = {SYMBOL_GRID, LV_SYMBOL_SAVE, LV_SYMBOL_DOWNLOAD};
    const int gap = 8;                              /* square buttons, from the column's left edge */
    for (int i = 0; i < COLL_TABS; i++) {
        l_tab_btn[i] = make_btn(screen_list(), 12 + i * (L_TAB_H + gap), L_PANE_Y, L_TAB_H, L_TAB_H, TAB[i], &font_icons_24,
                                list_tab_cb, (void *)(intptr_t)i);
        lv_obj_set_style_bg_color(l_tab_btn[i], C_CARD, 0);
        lv_obj_set_style_bg_color(l_tab_btn[i], C_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_radius(l_tab_btn[i], 10, 0);
    }
    l_colls = make_coll_column(screen_list());
    lv_obj_set_pos(l_colls, 12, L_PANE_Y + L_TAB_H + 8);
    lv_obj_set_size(l_colls, COLL_W, SCR_W - (L_PANE_Y + L_TAB_H + 8) - 12);
    lv_obj_set_scroll_dir(l_colls, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l_colls, LV_SCROLLBAR_MODE_AUTO);
    list_fill_colls();

    /* right pane: the selected collection's name and stats, Export / Delete, its puzzles */
    l_name = make_label(screen_list(), &lv_font_montserrat_22, C_TEXT);
    lv_label_set_long_mode(l_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l_name, PANE_X + 4, L_PANE_Y + 2);
    lv_obj_set_width(l_name, PANE_W - 4);
    l_stats = make_label(screen_list(), &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(l_stats, PANE_X + 4, L_PANE_Y + 34);
    lv_obj_set_width(l_stats, PANE_W - 4);
    lv_label_set_long_mode(l_stats, LV_LABEL_LONG_MODE_DOTS);

    l_btnm = lv_buttonmatrix_create(screen_list());
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

    l_list = list_rows_build(screen_list());       /* Saved, Imported: the collection's puzzles as rows */

    /* the Saved or Imported tab with nothing in it (its text: refresh_list()) */
    l_empty = make_label(screen_list(), &lv_font_montserrat_20, C_MUTED);
    lv_obj_set_width(l_empty, PANE_W - 24);
    lv_label_set_long_mode(l_empty, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(l_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l_empty, PANE_X + 12, L_GRID_Y + 20);
    lv_obj_set_hidden(l_empty, true);
    /* (the puzzles' source, opensudoku.gitlab.io, is credited on Home) */
}

/* ---- accessors: the other UI files reach this file's state through these */
/** @brief The title row's ⋮ menu button (its menu opens under it). @return it */
lv_obj_t *list_new_btn(void) { return l_new_btn; }
