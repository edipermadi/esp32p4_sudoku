/**
 * @file ui_stats_chart.c
 * @ingroup ui_stats
 * @brief Statistics' progress chart: the newest solves at the level picked as dots, the average of the last 5 as a line, the fastest and the trend under it.
 */
#include "ui_internal.h"

static lv_obj_t *x_chart;                 /**< the progress chart's card (instead of the table) */
static lv_obj_t *x_plot;                  /**< its plot */
static lv_obj_t *x_trend;                 /**< the text under it */

/* this file's own functions, in any order */
static void plot_text(lv_layer_t *layer, const char *text, lv_color_t color, int x1, int y1, int x2, lv_text_align_t align);
static void plot_line(lv_layer_t *layer, int x1, int y1, int x2, int y2, lv_color_t color, int width, lv_opa_t opa);
static void plot_dot(lv_layer_t *layer, int x, int y, int r, lv_color_t color);

/**
 * @brief Text on the plot.
 * @param[in] layer  where
 * @param[in] text   the text (copied)
 * @param[in] color  its colour
 * @param[in] x1     its box's left
 * @param[in] y1     its top
 * @param[in] x2     its right
 * @param[in] align  in the box
 */
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

/**
 * @brief A line on the plot, round-ended.
 * @param[in] layer  where
 * @param[in] x1     from x
 * @param[in] y1     from y
 * @param[in] x2     to x
 * @param[in] y2     to y
 * @param[in] color  its colour
 * @param[in] width  its width
 * @param[in] opa    its opacity
 */
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

/**
 * @brief A dot on the plot.
 * @param[in] layer  where
 * @param[in] x      its centre
 * @param[in] y      ...
 * @param[in] r      its radius
 * @param[in] color  its colour
 */
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

/* (documented in ui_internal.h) */
void plot_draw_cb(lv_event_t *e)
{
    const stats_view_t *v = statistics_view();
    if (v->n_recent < 2) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(x_plot, &co);
    int left = co.x1 + PLOT_L, right = co.x2 - PLOT_R, top = co.y1 + PLOT_T, bottom = co.y2 - PLOT_B;
    uint32_t max = 0;
    for (int i = 0; i < v->n_recent; i++) {
        if (v->recent[i] > max) {
            max = v->recent[i];
        }
    }
    uint32_t step = statistics_chart_step(max), lines = (max + step - 1) / step, top_s = lines * step;
    if (!top_s) {
        top_s = step;
    }

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
        if (i) {
            plot_line(layer, px, py, x, y, C_ACCENT, 2, LV_OPA_40);
        }
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
        int y = bottom - (int)((int64_t)(bottom - top) * statistics_chart_avg(v, i) / top_s);
        if (i) {
            plot_line(layer, ax, ay, x, y, C_ORANGE, 4, LV_OPA_COVER);
        }
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

/* (documented in ui_internal.h) */
void chart_render(const stats_view_t *v, bool from_db)
{
    lv_obj_set_hidden(x_chart, !is_stats_chart_mode());      /* (the ⋮ menu's item says Chart or Table: stats_menu_cb()) */
    if (!is_stats_chart_mode()) {
        return;
    }
    char text[200];
    const int n = v->n_recent;
    if (!from_db) {
        snprintf(text, sizeof(text), "The chart needs sudoku.db on a microSD card: it is drawn from the "
                 "solves logged there.");
    } else if (n < 2) {
        snprintf(text, sizeof(text), "Solve at least two puzzles%s to see a chart.",
                 v->level < 0 ? "" : " at this level");
    } else {
        char f[16], a1[16], a2[16];
        fmt_time(f, sizeof(f), statistics_fastest(v));
        int len = snprintf(text, sizeof(text), "Your last %d solve%s%s, oldest on the left. Fastest %s.", n,
                           n == 1 ? "" : "s", v->level < 0 ? "" : " at this level", f);
        uint32_t first, last;
        int pct;
        if (statistics_trend(v, &first, &last, &pct)) {   /* newest 5 against the first 5 shown */
            fmt_time(a1, sizeof(a1), first);
            fmt_time(a2, sizeof(a2), last);
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

/* (documented in ui_internal.h) */
void chart_build(int px, int pw)
{
    x_chart = lv_obj_create(screen_stats());
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
