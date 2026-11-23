/**
 * @file ui_home_week.c
 * @ingroup ui_home
 * @brief Home's week: this week's solves and best time on the "puzzles solved" line, and its tap: this
 *        week against last (stats_db_query_weeksum(): needs the clock, so only once it's set).
 */
#include "ui_internal.h"

static void week_row(lv_obj_t *parent, const char *name, const char *a, const char *b, bool head);

/* ------------------------------------------------------------- the week (Home) */
/** @brief The "puzzles solved" line also says how this week is going; a tap compares it
 * with last week (stats_db_query_weeksum: needs the clock, so only once it's set). */
static stats_weeksum_t h_week;
static bool h_week_ok;                  /**< h_week holds this week and last (once the clock is set) */

/**
 * @brief A row of the week's comparison.
 * @param[in] parent  where
 * @param[in] name  the row's name
 * @param[in] a  this week
 * @param[in] b  last week
 * @param[in] head  the header row
 */
static void week_row(lv_obj_t *parent, const char *name, const char *a, const char *b, bool head)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), 34);
    const lv_font_t *f = &lv_font_montserrat_18;
    lv_obj_t *l = make_label(r, f, head ? C_MUTED : C_TEXT);
    lv_label_set_text(l, name);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *x = make_label(r, f, head ? C_MUTED : C_TEXT);
    lv_label_set_text(x, a);
    lv_obj_align(x, LV_ALIGN_RIGHT_MID, -150, 0);
    lv_obj_t *y = make_label(r, f, C_MUTED);
    lv_label_set_text(y, b);
    lv_obj_align(y, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* (documented in ui_internal.h) */
void week_cb(lv_event_t *e)
{
    if (!h_week_ok) {
        return;
    }
    const stats_weeksum_t *w = &h_week;
    lv_obj_t *m = make_msgbox(SYMBOL_CALENDAR "  This week", NULL);
    lv_obj_set_width(m, 600);
    lv_obj_set_style_text_font(lv_msgbox_get_header(m), &font_icons_20, 0);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    char a[24], b[24];
    week_row(c, "", "This week", "Last week", true);
    snprintf(a, sizeof(a), "%d", w->n[0]);
    snprintf(b, sizeof(b), "%d", w->n[1]);
    week_row(c, "Solved", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) {
            fmt_time(o, 24, w->best[k]);
        } else {
            snprintf(o, 24, "-");
        }
    }
    week_row(c, "Fastest", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) {
            fmt_time(o, 24, w->avg[k]);
        } else {
            snprintf(o, 24, "-");
        }
    }
    week_row(c, "Average time", a, b, false);
    for (int k = 0; k < 2; k++) {
        char *o = k ? b : a;
        if (w->n[k]) {
            snprintf(o, 24, "%u.%u", w->mist10[k] / 10, w->mist10[k] % 10);
        } else {
            snprintf(o, 24, "-");
        }
    }
    week_row(c, "Mistakes per puzzle", a, b, false);
    snprintf(a, sizeof(a), "%lu", (unsigned long)w->hints[0]);
    snprintf(b, sizeof(b), "%lu", (unsigned long)w->hints[1]);
    week_row(c, "Hints", a, b, false);
    char verdict[128] = "";
    if (w->n[0] && w->n[1]) {
        int dm = (int)w->mist10[1] - (int)w->mist10[0];
        int dt = (int)w->avg[1] - (int)w->avg[0];
        char t[16];
        fmt_time(t, sizeof(t), (uint32_t)(dt < 0 ? -dt : dt));
        snprintf(verdict, sizeof(verdict), "%s%d.%d %s mistakes per puzzle, and %s %s on average than last week.",
                 "", (dm < 0 ? -dm : dm) / 10, (dm < 0 ? -dm : dm) % 10, dm >= 0 ? "fewer" : "more",
                 t, dt >= 0 ? "faster" : "slower");
    } else if (w->n[0]) {
        snprintf(verdict, sizeof(verdict), "Nothing solved last week to compare with.");
    } else {
        snprintf(verdict, sizeof(verdict), "Nothing solved yet this week: the week starts on Monday.");
    }
    lv_obj_t *v = make_wrap_text(c, &lv_font_montserrat_18, C_ACCENT, verdict);
    lv_obj_set_style_margin_top(v, 10, 0);
    msgbox_button(m, "Close", mbox_close_cb, false);
}

/* (documented in ui_internal.h) */
bool home_week_poll(bool clock_valid)
{
    if (!stats_db_take_weeksum(&h_week)) {
        return false;
    }
    h_week_ok = clock_valid;
    return true;
}

/* (documented in ui_internal.h) */
const stats_weeksum_t *home_week(void)
{
    return h_week_ok ? &h_week : NULL;
}
