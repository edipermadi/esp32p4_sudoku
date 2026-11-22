/**
 * @file ui_insights.c
 * @brief Mistake insights (Statistics): what the move logs say about the player's mistakes, in a
 *        dialog: how many digits were wrong, which digits, where on the board, and the commonest
 *        mix-ups (worked out by sudoku.db: stats_db_query_insights()).
 */
#include "ui_internal.h"

static void ins_close_cb(lv_event_t *e);
static lv_obj_t *ins_heading(const char *text);
static void ins_show(const stats_insights_t *in);
static void ins_poll_cb(lv_timer_t *t);

static lv_obj_t *ins_box;               /**< the dialog (NULL: closed) */
static lv_obj_t *ins_body;              /**< what it shows, scrolling */
static lv_timer_t *ins_timer;           /**< waits for sudoku.db's answer (100 ms) */

/**
 * @brief Close: the dialog, and the wait for sudoku.db if it's still on.
 * @param[in] e  the event
 */
static void ins_close_cb(lv_event_t *e)
{
    if (ins_timer) {
        lv_timer_delete(ins_timer);
    }
    ins_timer = NULL;
    if (ins_box) {
        lv_msgbox_close_async(ins_box);
    }
    ins_box = ins_body = NULL;
}

/**
 * @brief A heading in the dialog.
 * @param[in] text  its text
 * @return the label
 */
static lv_obj_t *ins_heading(const char *text)
{
    lv_obj_t *l = make_label(ins_body, &lv_font_montserrat_18, C_TEXT);
    lv_label_set_text(l, text);
    lv_obj_set_style_margin_top(l, 14, 0);
    return l;
}

/**
 * @brief Shows the insights: the share of wrong digits; a bar per digit put in the wrong place; the
 *        nine boxes, the busier the redder; the three commonest mix-ups ("A 3 where a 5 belongs").
 * @param[in] in  sudoku.db's answer
 */
static void ins_show(const stats_insights_t *in)
{
    lv_obj_clean(ins_body);
    char buf[160];
    if (in->solves == 0 || in->placed == 0) {
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED,
                       "Nothing to look at yet: insights come from the move logs of solved 9x9 puzzles.");
        return;
    }
    snprintf(buf, sizeof(buf), "From your last %d solve%s: %lu of the %lu digits you placed by hand were wrong "
             "(%lu.%lu %%).", in->solves, in->solves == 1 ? "" : "s", (unsigned long)in->wrong,
             (unsigned long)in->placed, (unsigned long)(in->wrong * 100 / in->placed),
             (unsigned long)(in->wrong * 1000 / in->placed % 10));
    make_wrap_text(ins_body, &lv_font_montserrat_18, C_TEXT, buf);
    if (!in->wrong) {
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_GREEN, "Not one wrong digit. Nothing to work on here!");
        return;
    }

    /* which digits: a bar each */
    ins_heading("Digits you put in the wrong place");
    int maxd = 1;
    for (int d = 1; d <= 9; d++) {
        if (in->by_digit[d] > maxd) {
            maxd = in->by_digit[d];
        }
    }
    for (int d = 1; d <= 9; d++) {
        lv_obj_t *row = lv_obj_create(ins_body);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 26);
        lv_obj_t *n = make_label(row, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text_fmt(n, "%d", d);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_t *bar = lv_obj_create(row);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, in->by_digit[d] ? 4 + 420 * in->by_digit[d] / maxd : 2, 16);
        lv_obj_set_style_bg_color(bar, in->by_digit[d] == maxd ? C_RED : C_ORANGE, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_align(bar, LV_ALIGN_LEFT_MID, 30, 0);
        lv_obj_t *c = make_label(row, &lv_font_montserrat_16, C_MUTED);
        lv_label_set_text_fmt(c, "%u", (unsigned)in->by_digit[d]);
        lv_obj_align(c, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    /* where: the nine boxes, the busier the redder */
    ins_heading("Where on the board");
    int maxb = 1;
    for (int b = 0; b < 9; b++) {
        if (in->by_box[b] > maxb) {
            maxb = in->by_box[b];
        }
    }
    lv_obj_t *g = lv_obj_create(ins_body);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, 3 * 76 + 2 * 6, 3 * 56 + 2 * 6);
    for (int b = 0; b < 9; b++) {
        lv_obj_t *c = lv_obj_create(g);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 76, 56);
        lv_obj_set_pos(c, (b % 3) * 82, (b / 3) * 62);
        lv_obj_set_style_radius(c, 6, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        int pct = in->by_box[b] * 100 / maxb;
        lv_obj_set_style_bg_color(c, lv_color_hex(mix_hex(ui_palette()->red, ui_palette()->card2, pct * 85 / 100)), 0);
        lv_obj_t *l = make_label(c, &lv_font_montserrat_18, pct > 55 ? lv_color_white() : C_TEXT);
        lv_label_set_text_fmt(l, "%u", (unsigned)in->by_box[b]);
        lv_obj_center(l);
    }

    /* mix-ups: the most frequent digit-for-digit swaps */
    ins_heading("Mix-ups");
    uint16_t done[10][10] = {{0}};
    for (int k = 0; k < 3; k++) {
        int bp = 0, bb = 0, bn = 0;
        for (int p2 = 1; p2 <= 9; p2++) {
            for (int q = 1; q <= 9; q++) {
                if (!done[p2][q] && in->pair[p2][q] > bn) {
                    bn = in->pair[p2][q];
                    bp = p2;
                    bb = q;
                }
            }
        }
        if (!bn) {
            break;
        }
        done[bp][bb] = 1;
        snprintf(buf, sizeof(buf), "A %d where a %d belongs: %d time%s", bp, bb, bn, bn == 1 ? "" : "s");
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED, buf);
    }
}

/**
 * @brief Waits for sudoku.db's answer, then shows it (if the dialog is still open).
 * @param[in] t  the timer
 */
static void ins_poll_cb(lv_timer_t *t)
{
    static stats_insights_t in;                     /* (~250 bytes: off the LVGL stack) */
    if (!stats_db_take_insights(&in)) {
        return;
    }
    lv_timer_delete(ins_timer);
    ins_timer = NULL;
    if (ins_body) {
        ins_show(&in);
    }
}

/**
 * @brief Statistics' Insights button: the dialog, "Reading your move logs..." until sudoku.db answers
 *        (without it: why not).
 * @param[in] e  the event
 */
void insights_cb(lv_event_t *e)
{
    if (ins_box) {
        return;
    }
    lv_obj_t *m = make_msgbox(SYMBOL_BULB "  Mistake insights", NULL);
    ins_box = m;
    lv_obj_set_width(m, 640);
    lv_obj_set_style_text_font(lv_msgbox_get_header(m), &font_icons_20, 0);
    ins_body = lv_msgbox_get_content(m);
    lv_obj_set_height(ins_body, 520);
    lv_obj_set_flex_flow(ins_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ins_body, 4, 0);
    lv_obj_set_scroll_dir(ins_body, LV_DIR_VER);
    make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED, "Reading your move logs...");
    msgbox_button(m, "Close", ins_close_cb, false);
    if (!is_stats_db_ready()) {
        lv_obj_clean(ins_body);
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED, "This needs sudoku.db on a microSD card.");
        return;
    }
    stats_db_query_insights();
    ins_timer = lv_timer_create(ins_poll_cb, 100, NULL);
}
