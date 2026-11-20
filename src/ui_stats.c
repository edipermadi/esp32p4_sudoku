/* sudoku_ui.c split: Statistics and the progress chart. See sudoku_ui.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void add_puzzle_stat(level_stat_t *st, uint16_t id);
static void view_from_ram(int level, stats_view_t *v);
static void fmt_avg_mistakes(char *buf, size_t n, const level_stat_t *st);
static void fmt_stat_times(char *fast, char *avg, size_t n, const level_stat_t *st);
static void stats_summary(const level_stat_t *st, uint16_t count);
static void table_columns(int n, const int32_t *w, const char *const *head);
static lv_obj_t *player_avatar_obj(lv_obj_t *parent, const stats_player_t *pl);
static void stats_player_list_draw_cb(lv_event_t *e);
static void stats_player_dd_cb(lv_event_t *e);
static void stats_select_rows(void);
static void kind_total(const stats_view_t *v, int k, stats_level_t *t);
static uint32_t chart_step(uint32_t max);
static uint32_t chart_avg(const stats_view_t *v, int i);
static void plot_text(lv_layer_t *layer, const char *text, lv_color_t color, int x1, int y1, int x2, lv_text_align_t align);
static void plot_line(lv_layer_t *layer, int x1, int y1, int x2, int y2, lv_color_t color, int width, lv_opa_t opa);
static void plot_dot(lv_layer_t *layer, int x, int y, int r, lv_color_t color);
static void plot_draw_cb(lv_event_t *e);
static void chart_render(const stats_view_t *v, bool from_db);
static void chart_btn_cb(lv_event_t *e);
static void stats_info_cb(lv_event_t *e);
static void stats_row_cb(lv_event_t *e);
static void stats_table_draw_cb(lv_event_t *e);

static void add_puzzle_stat(level_stat_t *st, uint16_t id)
{
    uint32_t t = store_best_time(id);
    if (!t) return;
    st->solved++;
    st->solves += store_solve_count(id);
    st->time_sum += t;
    if (!st->fastest || t < st->fastest) st->fastest = t;
    uint8_t m = store_fewest_mistakes(id);
    if (m != STORE_MISTAKES_UNKNOWN) {
        st->known++;
        st->miss_sum += m;
        if (m == 0) st->perfect++;
    }
}

/* fallback when there is no sudoku.db: this session's RAM statistics */
static void view_from_ram(int level, stats_view_t *v)
{
    memset(v, 0, sizeof(*v));
    v->level = level;
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        const puzzle_collection_t *pc = &puzzle_collections[c];
        for (uint16_t i = 0; i < pc->count; i++) {
            add_puzzle_stat(&v->levels[c], pc->first + i);
            add_puzzle_stat(&v->total, pc->first + i);
            if (c != level || !store_best_time(pc->first + i) || v->n_puzzles >= STATS_MAX_PUZZLES) continue;
            stats_puzzle_t *p = &v->puzzles[v->n_puzzles++];
            p->number = i + 1;
            p->best = (uint16_t)store_best_time(pc->first + i);
            p->fewest = store_fewest_mistakes(pc->first + i);
            p->solves = store_solve_count(pc->first + i);
        }
    }
}

/* "1.4", or "-" when nothing is known */
static void fmt_avg_mistakes(char *buf, size_t n, const level_stat_t *st)
{
    if (!st->known) {
        snprintf(buf, n, "-");
        return;
    }
    uint32_t x10 = (st->miss_sum * 10 + st->known / 2) / st->known;
    snprintf(buf, n, "%lu.%lu", (unsigned long)(x10 / 10), (unsigned long)(x10 % 10));
}

static void fmt_stat_times(char *fast, char *avg, size_t n, const level_stat_t *st)
{
    if (st->solved) {
        fmt_time(fast, n, st->fastest);
        fmt_time(avg, n, st->time_sum / st->solved);
    } else {
        snprintf(fast, n, "-");
        snprintf(avg, n, "-");
    }
}

static void stats_summary(const level_stat_t *st, uint16_t count)
{
    char f[16], a[16], m[16];
    fmt_stat_times(f, a, sizeof(f), st);
    fmt_avg_mistakes(m, sizeof(m), st);
    lv_label_set_text_fmt(x_sum1, "Solved %u of %u   -   %u without mistakes   -   %lu solves",
                          (unsigned)st->solved, (unsigned)count, (unsigned)st->perfect,
                          (unsigned long)st->solves);
    lv_label_set_text_fmt(x_sum2, "Fastest %s   -   average %s   -   mistakes %s per puzzle", f, a, m);
}

static void table_columns(int n, const int32_t *w, const char *const *head)
{
    lv_table_set_column_count(x_table, n);
    for (int c = 0; c < n; c++) {
        lv_table_set_column_width(x_table, c, w[c]);
        lv_table_set_cell_value(x_table, 0, c, head[c]);
    }
}

/* a player's avatar (an image) or, without one, a dot of their colour; not tappable */
static lv_obj_t *player_avatar_obj(lv_obj_t *parent, const stats_player_t *pl)
{
    lv_obj_t *o;
    if (pl && pl->avatar && pl->avatar <= AVATAR_COUNT) {
        o = lv_image_create(parent);
        lv_image_set_src(o, &avatar_images[pl->avatar - 1]);
    } else {
        o = lv_obj_create(parent);                  /* the dot, centred in the avatar's place */
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, AVATAR_SIZE, AVATAR_SIZE);
        lv_obj_center(player_dot(o, pl ? pl->color : 0, 20));
    }
    lv_obj_set_clickable(o, false);
    return o;
}
void stats_player_dd_update(void)
{
    if (!x_player_dd) return;
    static char opts[STATS_MAX_PLAYERS * STORE_NAME_LEN];
    bool db = stats_db_ready() && p_count > 0;
    int sel = 0;
    opts[0] = 0;
    if (db) {
        size_t len = 0;
        for (int i = 0; i < p_count; i++) {
            len += snprintf(opts + len, sizeof(opts) - len, "%s%s", i ? "\n" : "", p_players[i].name);
            if (p_players[i].id == store_player_id()) sel = i;
        }
    } else {
        snprintf(opts, sizeof(opts), "%s", store_player_name());
    }
    lv_dropdown_set_options(x_player_dd, opts);
    lv_dropdown_set_selected(x_player_dd, sel);

    /* the closed box: the avatar of who plays now left of the name, or the person icon */
    const stats_player_t *cur = db ? &p_players[sel] : NULL;
    bool av = cur && cur->avatar && cur->avatar <= AVATAR_COUNT;
    if (x_player_av) lv_obj_delete(x_player_av);
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
    lv_obj_set_state(x_player_dd, LV_STATE_DISABLED, !db || p_count < 2);
}

/* the open list's avatars (or dots), each left of its name. Drawn after the list (DRAW_POST, after the
 * dropdown's own: the selected line's highlight is drawn then too, and would cover children). */
static void stats_player_list_draw_cb(lv_event_t *e)
{
    if (!(stats_db_ready() && p_count > 0)) return;
    lv_obj_t *pdl = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(pdl, &a);
    int fh = lv_font_get_line_height(&lv_font_montserrat_20);
    int x = a.x1 + 12, y0 = a.y1 + lv_obj_get_style_pad_top(pdl, 0) - lv_obj_get_scroll_y(pdl);
    for (int i = 0; i < p_count; i++) {
        const stats_player_t *pl = &p_players[i];
        int y = y0 + i * (fh + X_DD_LS) + (fh - AVATAR_SIZE) / 2;
        if (pl->avatar && pl->avatar <= AVATAR_COUNT) {
            lv_draw_image_dsc_t d;
            lv_draw_image_dsc_init(&d);
            d.src = &avatar_images[pl->avatar - 1];
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

static void stats_player_dd_cb(lv_event_t *e)
{
    uint32_t i = lv_dropdown_get_selected(x_player_dd);
    if ((int)i < p_count && p_players[i].id != store_player_id()) {
        stats_db_switch_player(p_players[i].id);   /* statistics reload when the switch is done */
    }
}

stats_view_t *x_view_p;           /* PSRAM (internal RAM is tight): the view shown */

static void stats_select_rows(void)
{
    for (int r = 0; r < X_ROWS; r++) {
        bool sel = r == x_sel;
        lv_obj_set_state(x_btn[r], LV_STATE_CHECKED, sel);
        lv_obj_set_style_text_color(x_name_lbl[r], sel ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_style_text_color(x_cnt_lbl[r], sel ? lv_color_white() : C_MUTED, 0);
    }
}

/* a kind's four levels added up (fastest: the lowest) */
static void kind_total(const stats_view_t *v, int k, stats_level_t *t)
{
    memset(t, 0, sizeof(*t));
    for (int l = 0; l < GEN_LEVELS; l++) {
        const stats_level_t *x = &v->kinds[k][l];
        t->solved += x->solved;
        t->perfect += x->perfect;
        t->known += x->known;
        t->solves += x->solves;
        t->time_sum += x->time_sum;
        t->miss_sum += x->miss_sum;
        if (x->fastest && (!t->fastest || x->fastest < t->fastest)) t->fastest = x->fastest;
    }
}

void render_stats(const stats_view_t *v, bool from_db)
{
    chart_render(v, from_db);
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        lv_label_set_text_fmt(x_cnt_lbl[c + 1], "%u/%u", (unsigned)v->levels[c].solved,
                              (unsigned)puzzle_collections[c].count);
    }
    lv_label_set_text_fmt(x_cnt_lbl[0], "%u/%u", (unsigned)v->total.solved, (unsigned)PUZZLE_COUNT);
    stats_level_t kt[KIND_COUNT];
    for (int k = 0; k < KIND_COUNT; k++) {          /* kinds made on the board: puzzles solved */
        kind_total(v, k, &kt[k]);
        lv_label_set_text_fmt(x_cnt_lbl[1 + PUZZLE_COLLECTION_COUNT + k], "%u", (unsigned)kt[k].solved);
    }
    stats_player_dd_update();
    stats_select_rows();
    x_from_db = from_db;

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
            const level_stat_t *st = coll ? &v->levels[c] : &kt[c - PUZZLE_COLLECTION_COUNT];
            fmt_stat_times(f, a, sizeof(f), st);
            fmt_avg_mistakes(m, sizeof(m), st);
            lv_table_set_cell_value(x_table, c + 1, 0, coll ? puzzle_collections[c].name
                                                            : STATS_KIND_NAMES[c - PUZZLE_COLLECTION_COUNT]);
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
        lv_label_set_text(x_title, STATS_KIND_NAMES[k]);
        const level_stat_t *t = &kt[k];
        fmt_stat_times(f, a, sizeof(f), t);
        fmt_avg_mistakes(m, sizeof(m), t);
        lv_label_set_text_fmt(x_sum1, "Solved %u puzzle%s   -   %u without mistakes   -   %lu solves",
                              (unsigned)t->solved, t->solved == 1 ? "" : "s", (unsigned)t->perfect,
                              (unsigned long)t->solves);
        lv_label_set_text_fmt(x_sum2, "Fastest %s   -   average %s   -   mistakes %s per puzzle", f, a, m);
        static const int32_t w[] = {120, 70, 72, 86, 84};
        static const char *const h[] = {"Level", "Solved", "Fastest", "Average", "Mistakes"};
        lv_table_set_row_count(x_table, GEN_LEVELS + 1);
        table_columns(5, w, h);
        for (int l = 0; l < GEN_LEVELS; l++) {
            const level_stat_t *st = &v->kinds[k][l];
            fmt_stat_times(f, a, sizeof(f), st);
            fmt_avg_mistakes(m, sizeof(m), st);
            lv_table_set_cell_value(x_table, l + 1, 0, GEN_LEVEL_NAMES[l]);
            lv_table_set_cell_value_fmt(x_table, l + 1, 1, "%u", (unsigned)st->solved);
            lv_table_set_cell_value(x_table, l + 1, 2, f);
            lv_table_set_cell_value(x_table, l + 1, 3, a);
            lv_table_set_cell_value(x_table, l + 1, 4, m);
        }
        lv_label_set_text_fmt(x_empty, from_db ? "No %s solved yet." : "%s needs sudoku.db on a microSD card.",
                              STATS_KIND_NAMES[k]);
        lv_obj_set_hidden(x_table, x_chart_mode || t->solves == 0);
        lv_obj_set_hidden(x_empty, x_chart_mode || t->solves != 0);
        return;
    }

    /* one row per solved puzzle of the level */
    lv_label_set_text(x_empty, "No puzzles solved in this level yet.");
    const puzzle_collection_t *pc = &puzzle_collections[v->level];
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
        if (p->fewest == STORE_MISTAKES_UNKNOWN) lv_table_set_cell_value(x_table, r + 1, 2, "-");
        else lv_table_set_cell_value_fmt(x_table, r + 1, 2, "%u", (unsigned)p->fewest);
        lv_table_set_cell_value_fmt(x_table, r + 1, 3, "%u", (unsigned)p->solves);
    }
    lv_obj_set_hidden(x_table, x_chart_mode || v->n_puzzles == 0);
    lv_obj_set_hidden(x_empty, x_chart_mode || v->n_puzzles != 0);
}

/* a scale step that gives at most 5 lines up to max seconds */
static uint32_t chart_step(uint32_t max)
{
    static const uint32_t STEPS[] = {30, 60, 120, 300, 600, 900, 1200, 1800, 3600, 7200};
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); i++) {
        if (max <= STEPS[i] * 5) return STEPS[i];
    }
    return (max / 5 / 3600 + 1) * 3600;
}

/* trailing average of up to AVG_WIN solves ending at i */
static uint32_t chart_avg(const stats_view_t *v, int i)
{
    int from = i - AVG_WIN + 1 < 0 ? 0 : i - AVG_WIN + 1;
    uint32_t sum = 0;
    for (int k = from; k <= i; k++) sum += v->recent[k];
    return sum / (uint32_t)(i - from + 1);
}

static void plot_text(lv_layer_t *layer, const char *text, lv_color_t color, int x1, int y1, int x2,
                      lv_text_align_t align)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = &lv_font_montserrat_14;
    d.color = color;
    d.align = align;
    d.text = text;
    d.text_local = 1;                               /* copied: text is on the stack */
    lv_area_t a = {.x1 = x1, .y1 = y1, .x2 = x2, .y2 = y1 + 18};
    lv_draw_label(layer, &d, &a);
}

static void plot_line(lv_layer_t *layer, int x1, int y1, int x2, int y2, lv_color_t color, int width, lv_opa_t opa)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    d.color = color;
    d.width = width;
    d.opa = opa;
    d.round_start = d.round_end = 1;
    lv_draw_line(layer, &d);
}

static void plot_dot(lv_layer_t *layer, int x, int y, int r, lv_color_t color)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a = {.x1 = x - r, .y1 = y - r, .x2 = x + r, .y2 = y + r};
    lv_draw_rect(layer, &d, &a);
}

static void plot_draw_cb(lv_event_t *e)
{
    const stats_view_t *v = &x_view;
    if (v->n_recent < 2) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(x_plot, &co);
    int left = co.x1 + PLOT_L, right = co.x2 - PLOT_R, top = co.y1 + PLOT_T, bottom = co.y2 - PLOT_B;
    uint32_t max = 0;
    for (int i = 0; i < v->n_recent; i++) {
        if (v->recent[i] > max) max = v->recent[i];
    }
    uint32_t step = chart_step(max), lines = (max + step - 1) / step, top_s = lines * step;
    if (!top_s) top_s = step;

    /* scale: a line per step, labelled in minutes */
    char buf[16];
    for (uint32_t k = 0; k <= lines; k++) {
        int y = bottom - (int)((int64_t)(bottom - top) * k * step / top_s);
        plot_line(layer, left, y, right, y, C_CARD2, 1, LV_OPA_COVER);
        fmt_time(buf, sizeof(buf), k * step);
        plot_text(layer, buf, C_MUTED, co.x1, y - 9, left - 8, LV_TEXT_ALIGN_RIGHT);
    }

    /* the solves: dots joined by a faint line, then the average on top */
    const int n = v->n_recent;
    int px = 0, py = 0, ax = 0, ay = 0;
    for (int i = 0; i < n; i++) {
        int x = left + (right - left) * i / (n - 1);
        int y = bottom - (int)((int64_t)(bottom - top) * v->recent[i] / top_s);
        if (i) plot_line(layer, px, py, x, y, C_ACCENT, 2, LV_OPA_40);
        px = x;
        py = y;
    }
    for (int i = 0; i < n; i++) {
        int x = left + (right - left) * i / (n - 1);
        int y = bottom - (int)((int64_t)(bottom - top) * v->recent[i] / top_s);
        plot_dot(layer, x, y, 4, C_ACCENT);
    }
    for (int i = 0; i < n; i++) {
        int x = left + (right - left) * i / (n - 1);
        int y = bottom - (int)((int64_t)(bottom - top) * chart_avg(v, i) / top_s);
        if (i) plot_line(layer, ax, ay, x, y, C_ORANGE, 4, LV_OPA_COVER);
        ax = x;
        ay = y;
    }

    /* legend under the plot */
    int ly = co.y2 - 26;
    plot_dot(layer, left + 6, ly + 9, 5, C_ACCENT);
    plot_text(layer, "Each solve", C_MUTED, left + 18, ly, left + 140, LV_TEXT_ALIGN_LEFT);
    plot_line(layer, left + 150, ly + 9, left + 176, ly + 9, C_ORANGE, 4, LV_OPA_COVER);
    plot_text(layer, "Average of the last 5", C_MUTED, left + 186, ly, right, LV_TEXT_ALIGN_LEFT);
}

static void chart_render(const stats_view_t *v, bool from_db)
{
    lv_obj_set_hidden(x_chart, !x_chart_mode);
    lv_label_set_text(lv_obj_get_child(x_chart_btn, 0), x_chart_mode ? LV_SYMBOL_LIST "  Table"
                                                                     : SYMBOL_CHART "  Chart");
    if (!x_chart_mode) return;
    char text[200];
    const int n = v->n_recent;
    if (!from_db) {
        snprintf(text, sizeof(text), "The chart needs sudoku.db on a microSD card: it is drawn from the "
                                     "solves logged there.");
    } else if (n < 2) {
        snprintf(text, sizeof(text), "Solve at least two puzzles%s to see a chart.",
                 v->level < 0 ? "" : " at this level");
    } else {
        uint32_t fastest = 0xFFFFFFFF;
        for (int i = 0; i < n; i++) {
            if (v->recent[i] < fastest) fastest = v->recent[i];
        }
        char f[16], a1[16], a2[16];
        fmt_time(f, sizeof(f), fastest);
        int len = snprintf(text, sizeof(text), "Your last %d solve%s%s, oldest on the left. Fastest %s.", n,
                           n == 1 ? "" : "s", v->level < 0 ? "" : " at this level", f);
        if (n >= 2 * AVG_WIN) {                         /* newest 5 against the first 5 shown */
            uint32_t first = 0, last = 0;
            for (int i = 0; i < AVG_WIN; i++) {
                first += v->recent[i];
                last += v->recent[n - AVG_WIN + i];
            }
            first /= AVG_WIN;
            last /= AVG_WIN;
            fmt_time(a1, sizeof(a1), first);
            fmt_time(a2, sizeof(a2), last);
            int pct = first ? (int)(((int64_t)first - (int64_t)last) * 100 / (int64_t)first) : 0;
            if (pct > 0) {
                snprintf(text + len, sizeof(text) - len, "\nThe newest 5 average %s, the first 5 %s: "
                         "%d%% faster.", a2, a1, pct);
            } else if (pct < 0) {
                snprintf(text + len, sizeof(text) - len, "\nThe newest 5 average %s, the first 5 %s: "
                         "%d%% slower.", a2, a1, -pct);
            } else {
                snprintf(text + len, sizeof(text) - len, "\nThe newest 5 average %s, the same as the first 5.", a2);
            }
        }
    }
    lv_label_set_text(x_trend, text);
    lv_obj_set_hidden(x_plot, !from_db || n < 2);
    lv_obj_invalidate(x_plot);
}

static void chart_btn_cb(lv_event_t *e)
{
    x_chart_mode = !x_chart_mode;
    render_stats(&x_view, x_from_db);
}

/* the (i) button next to the title: what the numbers mean */
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
             x_from_db ? "These are the statistics of the player chosen at the top right, from sudoku.db "
                         "on the microSD card."
                       : "No sudoku.db: insert a FAT32 microSD card before switching on to keep statistics. "
                         "This shows what the board has in memory.");
    lv_obj_t *m = make_msgbox("About these statistics", text);
    lv_obj_set_width(m, 600);
    msgbox_button(m, "OK", mbox_close_cb, true);
}

/* Asks sudoku.db for the selected view (drawn when the answer arrives, see
 * sd_poll_cb); without the database, draws from RAM at once. */
void refresh_stats(void)
{
    stats_select_rows();
    if (stats_db_ready()) {
        stats_db_query(x_sel - 1);
    } else {
        view_from_ram(x_sel - 1, &x_view);
        render_stats(&x_view, false);
    }
}

void stats_cb(lv_event_t *e)
{
    if (stats_db_ready()) stats_db_list_players();   /* fresh list for the player dropdown */
    refresh_stats();
    lv_screen_load(scr_stats);
}

static void stats_row_cb(lv_event_t *e)
{
    x_sel = (int)(intptr_t)lv_event_get_user_data(e);
    refresh_stats();
}

/* header row in muted text on a darker band; numbers right-aligned */
static void stats_table_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(t);
    if (base->part != LV_PART_ITEMS) return;
    uint32_t row = base->id1, col = base->id2;
    if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_FILL) {
        lv_draw_fill_dsc_t *fd = lv_draw_task_get_fill_dsc(t);
        fd->color = row == 0 ? C_CARD2 : C_CARD;
    } else if (lv_draw_task_get_type(t) == LV_DRAW_TASK_TYPE_LABEL) {
        lv_draw_label_dsc_t *ld = lv_draw_task_get_label_dsc(t);
        if (row == 0) ld->color = C_MUTED;
        if (col > 0) ld->align = LV_TEXT_ALIGN_RIGHT;
    }
}

void gibberlink_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    store_set_gibberlink(on);
    gibberlink_set_enabled(on);
}

void sound_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(st_sound_sw, LV_STATE_CHECKED);
    sfx_set_enabled(on);
    store_set_sound_enabled(on);
    if (on) sfx_play(SFX_TOUCH);   /* the tick for this press was skipped while muted */
}

/* brightness and volume: applied while the slider moves, saved when it's let go */
void bright_sl_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(st_bright_sl);
    int nb = store_night_brightness();
    bsp_display_brightness_set(s_night && store_night_mode() && nb < v ? nb : v);   /* (at night: at most that) */
    lv_label_set_text_fmt(st_bright_lbl, "%d%%", v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) store_set_brightness((uint8_t)v);
}

void vol_sl_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(st_vol_sl);
    bsp_audio_set_volume(v);
    lv_label_set_text_fmt(st_vol_lbl, "%d%%", v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        store_set_volume((uint8_t)v);
        sfx_play(SFX_TOUCH);                       /* hear the new level */
    }
}

/* a slider + its value on the right of a settings row */
lv_obj_t *settings_slider(lv_obj_t *row, int min, int max, int value, lv_event_cb_t cb, lv_obj_t **lbl)
{
    lv_obj_t *sl = lv_slider_create(row);
    lv_obj_set_size(sl, 220, 14);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_slider_set_range(sl, min, max);
    lv_slider_set_value(sl, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, C_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 8, LV_PART_KNOB);              /* a knob a finger can find */
    lv_obj_set_ext_click_area(sl, 24);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_RELEASED, NULL);
    *lbl = make_label(row, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_width(*lbl, 56);
    lv_obj_set_style_text_align(*lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_fmt(*lbl, "%d%%", value);
    lv_obj_align(*lbl, LV_ALIGN_RIGHT_MID, -250, 0);
    return sl;
}

void build_stats(void)
{
    scr_stats = make_screen();

    make_btn(scr_stats, 12, 10, 120, 52, LV_SYMBOL_LEFT "  Home", &lv_font_montserrat_18, home_cb, NULL);
    lv_obj_t *t = make_label(scr_stats, &lv_font_montserrat_28, C_TEXT);
    lv_label_set_text(t, "Statistics");
    lv_obj_set_pos(t, 150, 20);
    lv_obj_t *info = make_btn(scr_stats, SCR_W - 12 - 52, 10, 52, 52, SYMBOL_INFO, &font_icons_24, stats_info_cb,
                              NULL);                 /* top right, on the title's line */
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(info, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lv_obj_get_child(info, 0), C_ACCENT, 0);

    lv_obj_t *list = make_coll_column(scr_stats);
    lv_obj_set_pos(list, 12, X_BODY_Y);
    lv_obj_set_size(list, COLL_W, SCR_W - X_BODY_Y - 12);
    x_btn[0] = make_coll_btn(list, "All levels", &x_name_lbl[0], &x_cnt_lbl[0], stats_row_cb, 0);
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        x_btn[c + 1] = make_coll_btn(list, puzzle_collections[c].name, &x_name_lbl[c + 1], &x_cnt_lbl[c + 1],
                                     stats_row_cb, c + 1);
    }
    for (int k = 0; k < KIND_COUNT; k++) {          /* made on the board (the list scrolls) */
        int r = 1 + PUZZLE_COLLECTION_COUNT + k;
        x_btn[r] = make_coll_btn(list, STATS_KIND_NAMES[k], &x_name_lbl[r], &x_cnt_lbl[r], stats_row_cb, r);
    }

    int px = PANE_X, pw = PANE_W;
    /* top bar, right: whose statistics, as a dropdown */
    x_player_dd = lv_dropdown_create(scr_stats);
    x_player_av = NULL;                                /* (a rebuild: the old one went with the old screen) */
    lv_obj_set_pos(x_player_dd, 12, X_ROW_Y);          /* whose statistics: first in the row */
    lv_obj_set_size(x_player_dd, 320, 52);
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
    x_title = make_label(scr_stats, &lv_font_montserrat_22, C_TEXT);
    lv_obj_set_pos(x_title, px + 4, X_BODY_Y + 4);
    x_sum1 = make_label(scr_stats, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(x_sum1, px + 4, X_BODY_Y + 34);
    x_sum2 = make_label(scr_stats, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(x_sum2, px + 4, X_BODY_Y + 54);

    x_table = lv_table_create(scr_stats);
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

    x_empty = make_label(scr_stats, &lv_font_montserrat_18, C_MUTED);
    lv_label_set_text(x_empty, "No puzzles solved in this level yet.");
    lv_obj_set_pos(x_empty, px + 4, X_BODY_Y + 104);

    /* Chart / Table switch (top right of the pane) and the chart card in the table's place */
    lv_obj_t *ib = make_btn(scr_stats, SCR_W - 12 - 160 - 10 - 170, X_ROW_Y, 170, 52, SYMBOL_BULB "  Insights",
                            &font_icons_20, insights_cb, NULL);   /* mistake insights, from the move logs */
    lv_obj_set_style_text_color(lv_obj_get_child(ib, 0), C_ACCENT, 0);
    x_chart_btn = make_btn(scr_stats, SCR_W - 12 - 160, X_ROW_Y, 160, 52, SYMBOL_CHART "  Chart", &font_icons_20,
                           chart_btn_cb, NULL);
    lv_obj_set_style_text_color(lv_obj_get_child(x_chart_btn, 0), C_ACCENT, 0);
    x_chart = lv_obj_create(scr_stats);
    lv_obj_set_pos(x_chart, px, X_BODY_Y + 82);
    lv_obj_set_size(x_chart, pw, SCR_W - X_BODY_Y - 82 - 12);
    lv_obj_set_style_bg_color(x_chart, C_CARD, 0);
    lv_obj_set_style_border_width(x_chart, 0, 0);
    lv_obj_set_style_radius(x_chart, 12, 0);
    lv_obj_set_style_pad_all(x_chart, 12, 0);
    lv_obj_set_scrollable(x_chart, false);
    lv_obj_set_flex_flow(x_chart, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(x_chart, 10, 0);
    x_plot = lv_obj_create(x_chart);
    lv_obj_remove_style_all(x_plot);
    lv_obj_set_size(x_plot, LV_PCT(100), 320);       /* (the trend line under it) */
    lv_obj_add_event_cb(x_plot, plot_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    x_trend = make_label(x_chart, &lv_font_montserrat_16, C_TEXT);
    lv_obj_set_width(x_trend, LV_PCT(100));
    lv_label_set_long_mode(x_trend, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_hidden(x_chart, true);

}
lv_obj_t *st_tab_btn[ST_TABS], *st_pane[ST_TABS];
int st_tab;                                  /* the open tab; Settings opens on Network */
