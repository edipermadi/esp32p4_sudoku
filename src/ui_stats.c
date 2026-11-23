/**
 * @file ui_stats.c
 * @ingroup ui_stats
 * @brief Statistics (Home): the list of levels, the table of the one picked (or the progress chart),
 *        the summary lines and whose statistics they are. The data: statistics_core.h.
 */
#include "ui_internal.h"
#include "statistics_core.h"

/**
 * @defgroup ui_stats Statistics screen
 * @ingroup topic_statistics
 * @brief The Statistics screen: a level per row on the left; its table or chart on the right.
 * @{
 */

/* this file's own functions, in any order */
static void fmt_stat_times(char *fast, char *avg, size_t n, const stats_level_t *st);
static void stats_summary(const stats_level_t *st, uint16_t count);
static void table_columns(int n, const int32_t *w, const char *const *head);
static lv_obj_t *player_avatar_obj(lv_obj_t *parent, const stats_player_t *pl);
static void stats_player_list_draw_cb(lv_event_t *e);
static void stats_player_dd_cb(lv_event_t *e);
static void stats_select_rows(void);
static void render_stats(const stats_view_t *v, bool from_db);
static void chart_btn_cb(lv_event_t *e);
static void stats_info_cb(lv_event_t *e);
static void stats_row_cb(lv_event_t *e);
static void stats_table_draw_cb(lv_event_t *e);

static lv_obj_t *x_btn[X_ROWS];           /**< the list's rows: all levels, the collections, the kinds */
static lv_obj_t *x_name_lbl[X_ROWS];      /**< their names */
static lv_obj_t *x_cnt_lbl[X_ROWS];       /**< their counts ("12/100") */
static lv_obj_t *x_title;                 /**< the row picked's name */
static lv_obj_t *x_sum1;                  /**< the summary's first line */
static lv_obj_t *x_sum2;                  /**< ... and second */
static lv_obj_t *x_table;                 /**< the table */
static lv_obj_t *x_empty;                 /**< instead of an empty table */
static lv_obj_t *x_player_dd;             /**< whose statistics */
static lv_obj_t *x_menu_btn;              /**< the ⋮ menu: Insights, Chart / Table, About */
static bool      x_chart_mode;            /**< the chart is shown (kept until power-off) */
static char      x_player_txt[STORE_NAME_LEN + 8];    /**< the closed dropdown: icon + name */
static lv_obj_t *x_player_av;                         /**< ... or the avatar left of the name */

/**
 * @brief A level's fastest and average time as text ("-" for both when nothing is solved).
 * @param[out] fast  gets the fastest
 * @param[out] avg   gets the average
 * @param[in] n     their size
 * @param[in] st    the level
 */
static void fmt_stat_times(char *fast, char *avg, size_t n, const stats_level_t *st)
{
    if (st->solved) {
        fmt_time(fast, n, st->fastest);
        fmt_time(avg, n, st->time_sum / st->solved);
    } else {
        snprintf(fast, n, "-");
        snprintf(avg, n, "-");
    }
}

/**
 * @brief The summary's two lines: solved of how many, without mistakes, solves; fastest, average,
 *        mistakes per puzzle.
 * @param[in] st     the level (or all of them)
 * @param[in] count  its puzzles
 */
static void stats_summary(const stats_level_t *st, uint16_t count)
{
    char f[16], a[16], m[16];
    fmt_stat_times(f, a, sizeof(f), st);
    statistics_avg_mistakes(m, sizeof(m), st);
    lv_label_set_text_fmt(x_sum1, "Solved %u of %u   -   %u without mistakes   -   %lu solves",
                          (unsigned)st->solved, (unsigned)count, (unsigned)st->perfect,
                          (unsigned long)st->solves);
    lv_label_set_text_fmt(x_sum2, "Fastest %s   -   average %s   -   mistakes %s per puzzle", f, a, m);
}

/**
 * @brief The table's columns: their number, widths and headings.
 * @param[in] n     how many
 * @param[in] w     their widths
 * @param[in] head  their headings
 */
static void table_columns(int n, const int32_t *w, const char *const *head)
{
    lv_table_set_column_count(x_table, n);
    for (int c = 0; c < n; c++) {
        lv_table_set_column_width(x_table, c, w[c]);
        lv_table_set_cell_value(x_table, 0, c, head[c]);
    }
}

/**
 * @brief A player's avatar (an image) or, without one, a dot of their colour; not tappable.
 * @param[in] parent  where
 * @param[in] pl      the player (NULL: the first colour's dot)
 * @return it
 */
static lv_obj_t *player_avatar_obj(lv_obj_t *parent, const stats_player_t *pl)
{
    lv_obj_t *o;
    if (pl && pl->avatar && pl->avatar <= AVATAR_COUNT) {
        o = lv_image_create(parent);
        lv_image_set_src(o, avatar_image(pl->avatar - 1));
    } else {
        o = lv_obj_create(parent);                  /* the dot, centred in the avatar's place */
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, AVATAR_SIZE, AVATAR_SIZE);
        lv_obj_center(player_dot(o, pl ? pl->color : 0, 20));
    }
    lv_obj_set_clickable(o, false);
    return o;
}
/**
 * @brief The player dropdown: everyone from sudoku.db (else who plays, alone), the closed box
 *        showing who plays now with their avatar (or the person icon); not tappable with one player.
 */
void stats_player_dd_update(void)
{
    if (!x_player_dd) {
        return;
    }
    static char opts[STATS_MAX_PLAYERS * STORE_NAME_LEN];
    bool db = is_stats_db_ready() && players_count() > 0;
    int sel = 0;
    opts[0] = 0;
    if (db) {
        size_t len = 0;
        for (int i = 0; i < players_count(); i++) {
            len += snprintf(opts + len, sizeof(opts) - len, "%s%s", i ? "\n" : "", players_all()[i].name);
            if (players_all()[i].id == store_player_id()) {
                sel = i;
            }
        }
    } else {
        snprintf(opts, sizeof(opts), "%s", store_player_name());
    }
    lv_dropdown_set_options(x_player_dd, opts);
    lv_dropdown_set_selected(x_player_dd, sel);

    /* the closed box: the avatar of who plays now left of the name, or the person icon */
    const stats_player_t *cur = db ? &players_all()[sel] : NULL;
    bool av = cur && cur->avatar && cur->avatar <= AVATAR_COUNT;
    if (x_player_av) {
        lv_obj_delete(x_player_av);
    }
    x_player_av = NULL;
    if (av) {
        x_player_av = player_avatar_obj(x_player_dd, cur);
        lv_obj_align(x_player_av, LV_ALIGN_LEFT_MID, -(AVATAR_SIZE + 10), 0);   /* (in the padding) */
    }
    lv_obj_set_style_pad_left(x_player_dd, av ? X_DD_PAD + AVATAR_SIZE + 10 : X_DD_PAD, 0);
    snprintf(x_player_txt, sizeof(x_player_txt), av ? "%s" : SYMBOL_USER "  %s", store_player_name());
    lv_dropdown_set_text(x_player_dd, x_player_txt);

    /* the open list: room for the avatars left of the names (stats_player_list_draw_cb) */
    lv_obj_set_style_pad_left(lv_dropdown_get_list(x_player_dd), db ? 12 + AVATAR_SIZE + 12 : 12, 0);
    lv_obj_set_state(x_player_dd, LV_STATE_DISABLED, !db || players_count() < 2);
}

/**
 * @brief The open list's avatars (or dots), each left of its name. Drawn after the list (DRAW_POST,
 *        after the dropdown's own: the selected line's highlight is drawn then too, and would cover
 *        children).
 * @param[in] e  the draw event
 */
static void stats_player_list_draw_cb(lv_event_t *e)
{
    if (!(is_stats_db_ready() && players_count() > 0)) {
        return;
    }
    lv_obj_t *pdl = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(pdl, &a);
    int fh = lv_font_get_line_height(&lv_font_montserrat_20);
    int x = a.x1 + 12, y0 = a.y1 + lv_obj_get_style_pad_top(pdl, 0) - lv_obj_get_scroll_y(pdl);
    for (int i = 0; i < players_count(); i++) {
        const stats_player_t *pl = &players_all()[i];
        int y = y0 + i * (fh + X_DD_LS) + (fh - AVATAR_SIZE) / 2;
        if (pl->avatar && pl->avatar <= AVATAR_COUNT) {
            lv_draw_image_dsc_t d;
            lv_draw_image_dsc_init(&d);
            d.src = avatar_image(pl->avatar - 1);
            lv_area_t im = {x, y, x + AVATAR_SIZE - 1, y + AVATAR_SIZE - 1};
            lv_draw_image(layer, &d, &im);
        } else {                                    /* their colour's dot */
            lv_draw_rect_dsc_t r;
            lv_draw_rect_dsc_init(&r);
            r.bg_color = player_color(pl->color);
            r.radius = LV_RADIUS_CIRCLE;
            int o = (AVATAR_SIZE - 20) / 2;
            lv_area_t dot = {x + o, y + o, x + o + 19, y + o + 19};
            lv_draw_rect(layer, &r, &dot);
        }
    }
}

/**
 * @brief A player picked: switch to them (the statistics reload when the switch is done).
 * @param[in] e  the event
 */
static void stats_player_dd_cb(lv_event_t *e)
{
    uint32_t i = lv_dropdown_get_selected(x_player_dd);
    if ((int)i < players_count() && players_all()[i].id != store_player_id()) {
        stats_db_switch_player(players_all()[i].id);   /* statistics reload when the switch is done */
    }
}

/** @brief Marks the row picked in the list. */
static void stats_select_rows(void)
{
    for (int r = 0; r < X_ROWS; r++) {
        bool sel = r == statistics_sel();
        lv_obj_set_state(x_btn[r], LV_STATE_CHECKED, sel);
        lv_obj_set_style_text_color(x_name_lbl[r], sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(x_cnt_lbl[r], sel ? lv_color_white() : C_MUTED, 0);
    }
}

/**
 * @brief Shows a view: the list's counts, the dropdown, the title, the summary, and the table (a row
 *        per level for all levels or a kind; a row per solved puzzle for a collection) or the chart.
 * @param[in] v        the view
 * @param[in] from_db  it came from sudoku.db (else from RAM)
 */
static void render_stats(const stats_view_t *v, bool from_db)
{
    chart_render(v, from_db);
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        lv_label_set_text_fmt(x_cnt_lbl[c + 1], "%u/%u", (unsigned)v->levels[c].solved,
                              (unsigned)puzzle_collection(c)->count);
    }
    lv_label_set_text_fmt(x_cnt_lbl[0], "%u/%u", (unsigned)v->total.solved, (unsigned)PUZZLE_COUNT);
    stats_level_t kt[KIND_COUNT];
    for (int k = 0; k < KIND_COUNT; k++) {          /* kinds made on the board: puzzles solved */
        statistics_kind_total(v, k, &kt[k]);
        lv_label_set_text_fmt(x_cnt_lbl[1 + PUZZLE_COLLECTION_COUNT + k], "%u", (unsigned)kt[k].solved);
    }
    stats_player_dd_update();
    stats_select_rows();

    char f[16], a[16], m[16];
    lv_obj_scroll_to_y(x_table, 0, LV_ANIM_OFF);
    if (v->level < 0) {
        /* one row per level */
        lv_label_set_text(x_title, "All levels");
        stats_summary(&v->total, PUZZLE_COUNT);
        static const int32_t w[] = {120, 70, 72, 86, 84};   /* sums to PANE_W; every header on one line */
        static const char *const h[] = {"Level", "Solved", "Fastest", "Average", "Mistakes"};
        lv_table_set_row_count(x_table, PUZZLE_COLLECTION_COUNT + KIND_COUNT + 1);
        table_columns(5, w, h);
        for (int c = 0; c < PUZZLE_COLLECTION_COUNT + KIND_COUNT; c++) {   /* collections, then kinds */
            bool coll = c < PUZZLE_COLLECTION_COUNT;
            const stats_level_t *st = coll ? &v->levels[c] : &kt[c - PUZZLE_COLLECTION_COUNT];
            fmt_stat_times(f, a, sizeof(f), st);
            statistics_avg_mistakes(m, sizeof(m), st);
            lv_table_set_cell_value(x_table, c + 1, 0, coll ? puzzle_collection(c)->name
                                    : stats_kind_name(c - PUZZLE_COLLECTION_COUNT));
            lv_table_set_cell_value_fmt(x_table, c + 1, 1, "%u", (unsigned)st->solved);
            lv_table_set_cell_value(x_table, c + 1, 2, f);
            lv_table_set_cell_value(x_table, c + 1, 3, a);
            lv_table_set_cell_value(x_table, c + 1, 4, m);
        }
        lv_obj_set_hidden(x_table, x_chart_mode);
        lv_obj_set_hidden(x_empty, true);
        return;
    }

    if (v->level >= PUZZLE_COLLECTION_COUNT) {     /* a kind made on the board: one row per level */
        int k = v->level - PUZZLE_COLLECTION_COUNT;
        lv_label_set_text(x_title, stats_kind_name(k));
        const stats_level_t *t = &kt[k];
        fmt_stat_times(f, a, sizeof(f), t);
        statistics_avg_mistakes(m, sizeof(m), t);
        lv_label_set_text_fmt(x_sum1, "Solved %u puzzle%s   -   %u without mistakes   -   %lu solves",
                              (unsigned)t->solved, t->solved == 1 ? "" : "s", (unsigned)t->perfect,
                              (unsigned long)t->solves);
        lv_label_set_text_fmt(x_sum2, "Fastest %s   -   average %s   -   mistakes %s per puzzle", f, a, m);
        static const int32_t w[] = {120, 70, 72, 86, 84};
        static const char *const h[] = {"Level", "Solved", "Fastest", "Average", "Mistakes"};
        lv_table_set_row_count(x_table, GEN_LEVELS + 1);
        table_columns(5, w, h);
        for (int l = 0; l < GEN_LEVELS; l++) {
            const stats_level_t *st = &v->kinds[k][l];
            fmt_stat_times(f, a, sizeof(f), st);
            statistics_avg_mistakes(m, sizeof(m), st);
            lv_table_set_cell_value(x_table, l + 1, 0, gen_level_name(l));
            lv_table_set_cell_value_fmt(x_table, l + 1, 1, "%u", (unsigned)st->solved);
            lv_table_set_cell_value(x_table, l + 1, 2, f);
            lv_table_set_cell_value(x_table, l + 1, 3, a);
            lv_table_set_cell_value(x_table, l + 1, 4, m);
        }
        lv_label_set_text_fmt(x_empty, from_db ? "No %s solved yet." : "%s needs sudoku.db on a microSD card.",
                              stats_kind_name(k));
        lv_obj_set_hidden(x_table, x_chart_mode || t->solves == 0);
        lv_obj_set_hidden(x_empty, x_chart_mode || t->solves != 0);
        return;
    }

    /* one row per solved puzzle of the level */
    lv_label_set_text(x_empty, "No puzzles solved in this level yet.");
    const puzzle_collection_t *pc = puzzle_collection(v->level);
    lv_label_set_text(x_title, pc->name);
    stats_summary(&v->levels[v->level], pc->count);
    static const int32_t w[] = {90, 130, 110, 102};     /* sums to PANE_W */
    static const char *const h[] = {"Puzzle", "Best time", "Mistakes", "Solved"};
    lv_table_set_row_count(x_table, v->n_puzzles + 1);
    table_columns(4, w, h);
    for (int r = 0; r < v->n_puzzles; r++) {
        const stats_puzzle_t *p = &v->puzzles[r];
        fmt_time(f, sizeof(f), p->best);
        lv_table_set_cell_value_fmt(x_table, r + 1, 0, "#%u", (unsigned)p->number);
        lv_table_set_cell_value(x_table, r + 1, 1, f);
        if (p->fewest == STORE_MISTAKES_UNKNOWN) {
            lv_table_set_cell_value(x_table, r + 1, 2, "-");
        } else {
            lv_table_set_cell_value_fmt(x_table, r + 1, 2, "%u", (unsigned)p->fewest);
        }
        lv_table_set_cell_value_fmt(x_table, r + 1, 3, "%u", (unsigned)p->solves);
    }
    lv_obj_set_hidden(x_table, x_chart_mode || v->n_puzzles == 0);
    lv_obj_set_hidden(x_empty, x_chart_mode || v->n_puzzles != 0);
}

/**
 * @brief The ⋮ menu's Chart / Table: the other one shown.
 * @param[in] e  the event
 */
static void chart_btn_cb(lv_event_t *e)
{
    x_chart_mode = !x_chart_mode;
    render_stats(statistics_view(), is_statistics_from_db());
}

/**
 * @brief The ⋮ menu's About these statistics: what the numbers mean, and where they come from.
 * @param[in] e  the event
 */
static void stats_info_cb(lv_event_t *e)
{
    static char text[640];
    snprintf(text, sizeof(text), "%s\n\n"
             "Mistake: a placed digit that broke a row, column or box rule (with Mistake check on Solution, "
             "also one that wasn't the solution's). Undo doesn't take it back.\n\n"
             "Per puzzle: the best time, the fewest mistakes in any solve, and how many times it was "
             "solved. \"-\" means mistakes weren't counted yet when it was solved.\n\n"
             "Per level: puzzles solved, the fastest and the average of the best times, and the "
             "average of the fewest mistakes.",
             is_statistics_from_db() ? "These are the statistics of the player chosen at the top, from sudoku.db "
             "on the microSD card."
             : "No sudoku.db: insert a FAT32 microSD card before switching on to keep statistics. "
             "This shows what the board has in memory.");
    lv_obj_t *m = make_msgbox("About these statistics", text);
    lv_obj_set_width(m, 600);
    msgbox_button(m, "OK", mbox_close_cb, true);
}

/** @brief Asks sudoku.db for the row picked's view (drawn when the answer arrives: stats_take_view());
 *         without the database, draws it from RAM at once. */
void refresh_stats(void)
{
    stats_select_rows();
    if (!statistics_ask()) {
        render_stats(statistics_view(), false);
    }
}

/**
 * @brief A row of the list: its statistics.
 * @param[in] e  the event (user data: the row)
 */
static void stats_row_cb(lv_event_t *e)
{
    statistics_select((int)(intptr_t)lv_event_get_user_data(e));
    refresh_stats();
}

/**
 * @brief The table's look: the header row in muted text on a darker band; numbers right-aligned.
 * @param[in] e  the draw task event
 */
static void stats_table_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (base->part != LV_PART_ITEMS) {
        return;
    }
    uint32_t row = base->id1, col = base->id2;
    if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_FILL) {
        lv_draw_fill_dsc_t *fd = lv_draw_task_get_fill_dsc(t);
        fd->color = row == 0 ? C_CARD2 : C_CARD;
    } else if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_LABEL) {
        lv_draw_label_dsc_t *ld = lv_draw_task_get_label_dsc(t);
        if (row == 0) {
            ld->color = C_MUTED;
        }
        if (col > 0) {
            ld->align = LV_TEXT_ALIGN_RIGHT;
        }
    }
}

/** @brief Builds the screen: Home, the title, the ⋮ menu, the list, the player dropdown, Insights, Chart, the summary, the table and the chart. */
void build_stats(void)
{
    screen_set_stats(make_screen());

    make_btn(screen_stats(), 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(screen_stats(), &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Statistics");
    lv_obj_set_pos(t, 150, 20);
    x_menu_btn = make_btn(screen_stats(), SCR_W - 12 - 52, 10, 52, 52, SYMBOL_MORE, &font_icons_24, stats_menu_cb,
                          NULL);                     /* top right, on the title's line (as the Puzzles screen's) */

    lv_obj_t *list = make_coll_column(screen_stats());
    lv_obj_set_pos(list, 12, X_BODY_Y);
    lv_obj_set_size(list, COLL_W, SCR_W - X_BODY_Y - 12);
    x_btn[0] = make_coll_btn(list, "All levels", &x_name_lbl[0], &x_cnt_lbl[0], stats_row_cb, 0);
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        x_btn[c + 1] = make_coll_btn(list, puzzle_collection(c)->name, &x_name_lbl[c + 1], &x_cnt_lbl[c + 1],
                                     stats_row_cb, c + 1);
    }
    for (int k = 0; k < KIND_COUNT; k++) {          /* made on the board (the list scrolls) */
        int r = 1 + PUZZLE_COLLECTION_COUNT + k;
        x_btn[r] = make_coll_btn(list, stats_kind_name(k), &x_name_lbl[r], &x_cnt_lbl[r], stats_row_cb, r);
    }

    int px = PANE_X, pw = PANE_W;
    /* the title row, right: whose statistics, as a dropdown, left of the ⋮ menu */
    x_player_dd = lv_dropdown_create(screen_stats());
    x_player_av = NULL;                                /* (a rebuild: the old one went with the old screen) */
    lv_obj_set_pos(x_player_dd, SCR_W - 12 - 52 - 10 - X_DD_W, 10);   /* whose statistics: next to the ⋮ menu */
    lv_obj_set_size(x_player_dd, X_DD_W, 52);
    lv_obj_set_style_text_font(x_player_dd, &font_icons_20, 0);
    lv_obj_set_style_text_color(x_player_dd, C_TEXT, 0);
    lv_obj_set_style_bg_color(x_player_dd, C_CARD, 0);
    lv_obj_set_style_border_width(x_player_dd, 0, 0);
    lv_obj_set_style_shadow_width(x_player_dd, 0, 0);
    lv_obj_set_style_radius(x_player_dd, 8, 0);
    lv_obj_set_style_pad_hor(x_player_dd, X_DD_PAD, 0);
    lv_obj_set_style_pad_ver(x_player_dd, 15, 0);
    lv_obj_t *pdl = lv_dropdown_get_list(x_player_dd);
    lv_obj_set_style_text_font(pdl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pdl, C_TEXT, 0);
    lv_obj_set_style_text_line_space(pdl, X_DD_LS, 0);
    lv_obj_set_style_pad_ver(pdl, 14, 0);
    lv_obj_add_event_cb(pdl, stats_player_list_draw_cb, LV_EVENT_DRAW_POST, NULL);
    lv_obj_set_style_bg_color(pdl, C_CARD2, 0);
    lv_obj_set_style_border_width(pdl, 0, 0);
    lv_obj_set_style_radius(pdl, 8, 0);
    lv_obj_set_style_max_height(pdl, 480, 0);
    lv_obj_set_style_bg_color(pdl, C_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(pdl, lv_color_white(), LV_PART_SELECTED | LV_STATE_CHECKED);
    lv_obj_add_event_cb(x_player_dd, stats_player_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);
    stats_player_dd_update();
    x_title = make_label(screen_stats(), &lv_font_montserrat_22, C_TEXT);
    lv_obj_set_pos(x_title, px + 4, X_BODY_Y + 4);
    x_sum1 = make_label(screen_stats(), &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(x_sum1, px + 4, X_BODY_Y + 34);
    x_sum2 = make_label(screen_stats(), &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(x_sum2, px + 4, X_BODY_Y + 54);

    x_table = lv_table_create(screen_stats());
    lv_obj_set_pos(x_table, px, X_BODY_Y + 82);
    lv_obj_set_size(x_table, pw, SCR_W - X_BODY_Y - 82 - 12);
    lv_obj_set_style_bg_color(x_table, C_CARD, 0);
    lv_obj_set_style_border_width(x_table, 0, 0);
    lv_obj_set_style_radius(x_table, 12, 0);
    lv_obj_set_style_pad_all(x_table, 0, 0);
    lv_obj_set_style_clip_corner(x_table, true, 0);
    lv_obj_set_style_text_font(x_table, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_text_color(x_table, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(x_table, C_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(x_table, C_CARD, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(x_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(x_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_color(x_table, C_BG, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(x_table, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(x_table, 9, LV_PART_ITEMS);
    lv_obj_set_send_draw_task_events(x_table, true);
    lv_obj_add_event_cb(x_table, stats_table_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);

    x_empty = make_label(screen_stats(), &lv_font_montserrat_18, C_MUTED);
    lv_label_set_text(x_empty, "No puzzles solved in this level yet.");
    lv_obj_set_pos(x_empty, px + 4, X_BODY_Y + 104);

    /* the chart card, in the table's place (the ⋮ menu's Chart / Table switches) */
    chart_build(px, pw);                            /* the progress chart (ui_stats_chart.c) */

}

/**
 * @brief sudoku.db's answer for the row picked, shown (Settings' card poll hands it over).
 * @param[in] v  the view
 */
void stats_take_view(const stats_view_t *v)
{
    statistics_take(v);
    render_stats(statistics_view(), true);
}


/** @} */

/* (documented in ui_internal.h) */
void stats_menu_cb(lv_event_t *e)
{
    const ctx_item_t items[] = {
        {SYMBOL_BULB, "Insights", insights_cb, false, false},
        {x_chart_mode ? LV_SYMBOL_LIST : SYMBOL_CHART, x_chart_mode ? "Table" : "Chart", chart_btn_cb, false, false},
        {SYMBOL_INFO, "About statistics", stats_info_cb, false, false},
    };
    ctx_menu_toggle(x_menu_btn, items, (int)(sizeof(items) / sizeof(items[0])));
}

/* (documented in ui_internal.h) */
bool is_stats_chart_mode(void)
{
    return x_chart_mode;
}
