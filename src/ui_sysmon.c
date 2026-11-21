/**
 * @file ui_sysmon.c
 * @ingroup ui_sysmon
 * @brief System monitor screen: the frame around the tabs, the refresh and the history timer.
 *
 * The title, the tab bar and a pane per tab (sysmon_ui_create()), the refresh every second while the
 * page is shown and the history sample every 10 s from start-up (each tab gets its turn), and the
 * cards' look shared by the tabs (card(), kv(), bar_row(), graph_card()). Each tab is a file of its
 * own (see ui_sysmon_tab.h); the numbers come from sysmon_core.c.
 */
#include "ui_sysmon.h"
#include "ui_sysmon_tab.h"
#include "ui_palette.h"
#include <stdio.h>
#include <string.h>

#define REFRESH_MS   1000                           /**< the page's refresh, in ms */

static lv_obj_t *s_scr;                             /**< the page's screen */

enum { SM_SYSTEM = 0, SM_MEMORY, SM_STORAGE, SM_GRAPHICS, SM_TASKS, SM_TABS };
#define TAB_Y   72                                  /**< the tab bar's top */
#define TAB_H   52                                  /**< a tab button's height */
#define PANE_Y  (TAB_Y + TAB_H + 12)                /**< the panes' top: under the tab bar */
static lv_obj_t *s_tab_btn[SM_TABS];                /**< the tab buttons */
static lv_obj_t *s_pane[SM_TABS];                   /**< each tab's pane (a scrolling column of cards) */
static int s_tab;                                   /**< the open tab; kept between visits */
static lv_timer_t *s_timer;                         /**< the refresh (paused while the page isn't shown) */
static bool s_shown;                                /**< the page is shown */
static int s_ticks;                                 /**< refreshes since the page was shown */
/** The history sample: from start-up, whether the page is open or not. */
static lv_timer_t *h_timer;

/* ------------------------------------------------------------- helpers */
/* (documented in ui_sysmon_tab.h) */
void fmt_bytes(char *buf, size_t n, uint64_t b)
{
    if (b < 1024) {
        snprintf(buf, n, "%u B", (unsigned)b);
    } else if (b < 1024 * 1024) {
        snprintf(buf, n, "%u.%u KB", (unsigned)(b / 1024), (unsigned)(b % 1024 * 10 / 1024));
    } else if (b < 1024ULL * 1024 * 1024) {
        uint64_t k = b / 1024;
        snprintf(buf, n, "%u.%u MB", (unsigned)(k / 1024), (unsigned)(k % 1024 * 10 / 1024));
    } else {
        uint64_t m = b / (1024 * 1024);
        snprintf(buf, n, "%u.%02u GB", (unsigned)(m / 1024), (unsigned)(m % 1024 * 100 / 1024));
    }
}

/**
 * @brief A row that spreads its children out (a title on the left, a value on the right).
 * @param parent  the card
 * @return the row
 */
static lv_obj_t *plain_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

/* (documented in ui_sysmon_tab.h) */
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    return l;
}

/* (documented in ui_sysmon_tab.h) */
void section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *l = label(parent, &lv_font_montserrat_16, C_MUTED, title);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_pad_hor(l, 8, 0);
}

/* (documented in ui_sysmon_tab.h) */
lv_obj_t *card(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_pad_all(c, 18, 0);
    lv_obj_set_style_pad_row(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(c, false);
    return c;
}

/* (documented in ui_sysmon_tab.h) */
lv_obj_t *kv(lv_obj_t *c, const char *title)
{
    lv_obj_t *row = plain_row(c);
    label(row, &lv_font_montserrat_18, C_MUTED, title);
    lv_obj_t *v = label(row, &lv_font_montserrat_20, C_TEXT, "-");
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    return v;
}

/* (documented in ui_sysmon_tab.h) */
void bar_row(lv_obj_t *c, const char *title, lv_obj_t **bar, lv_obj_t **val, lv_obj_t **sub)
{
    lv_obj_t *col = lv_obj_create(c);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 8, 0);
    *val = kv(col, title);
    *bar = lv_bar_create(col);
    lv_obj_set_size(*bar, LV_PCT(100), 14);
    lv_bar_set_range(*bar, 0, 1000);
    lv_obj_set_style_radius(*bar, 7, 0);
    lv_obj_set_style_radius(*bar, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(*bar, C_CARD2, 0);
    lv_obj_set_style_bg_opa(*bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(*bar, C_ACCENT, LV_PART_INDICATOR);
    if (sub) {
        *sub = label(col, &lv_font_montserrat_16, C_MUTED, "");
        lv_obj_set_width(*sub, LV_PCT(100));
        lv_label_set_long_mode(*sub, LV_LABEL_LONG_MODE_DOTS);
    }
}

/* (documented in ui_sysmon_tab.h) */
void set_bar(lv_obj_t *bar, uint64_t used, uint64_t total)
{
    int32_t v = total ? (int32_t)(used * 1000 / total) : 0;
    lv_bar_set_value(bar, v, LV_ANIM_OFF);
    lv_color_t c = v >= 900 ? C_RED : (v >= 750 ? C_ORANGE : C_ACCENT);
    lv_obj_set_style_bg_color(bar, c, LV_PART_INDICATOR);
}

/* (documented in ui_sysmon_tab.h) */
void set_text(lv_obj_t *l, const char *txt)
{
    if (strcmp(lv_label_get_text(l), txt) != 0) {
        lv_label_set_text(l, txt);
    }
}

/* (documented in ui_sysmon_tab.h) */
lv_obj_t *graph_card(lv_obj_t *pane, lv_obj_t **title)
{
    lv_obj_t *c = card(pane);
    lv_obj_set_style_pad_row(c, 8, 0);
    *title = label(c, &lv_font_montserrat_18, C_TEXT, "Collecting (a point every 10 s)...");
    lv_obj_t *ch = lv_chart_create(c);
    lv_obj_set_size(ch, LV_PCT(100), 140);
    lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ch, SYSMON_HIST_N);
    lv_chart_set_update_mode(ch, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(ch, 5, 7);
    lv_obj_set_style_bg_color(ch, C_BG, 0);
    lv_obj_set_style_bg_opa(ch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ch, 0, 0);
    lv_obj_set_style_radius(ch, 8, 0);
    lv_obj_set_style_pad_all(ch, 6, 0);
    lv_obj_set_style_line_color(ch, C_CARD2, LV_PART_MAIN);     /* the grid */
    lv_obj_set_style_line_width(ch, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(ch, 0, LV_PART_INDICATOR);           /* no dots: 360 points */
    lv_obj_set_style_height(ch, 0, LV_PART_INDICATOR);
    lv_obj_set_clickable(ch, false);                            /* drags scroll the page */
    lv_obj_set_scrollable(ch, false);
    lv_obj_t *row = lv_obj_create(c);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(row, &lv_font_montserrat_14, C_MUTED, "1 hour ago");
    label(row, &lv_font_montserrat_14, C_MUTED, "now");
    return ch;
}

/* (documented in ui_sysmon_tab.h) */
int32_t hist_pt(int16_t v) { return v == SYSMON_NONE ? LV_CHART_POINT_NONE : v; }

/**
 * @brief Every second while the page is shown: each tab's numbers again (the Tasks tab: only while it's shown).
 * @param t  the timer
 */
static void tick(lv_timer_t *t)
{
    sysmon_tab_storage_tick(s_ticks++);
    sysmon_tab_system_tick();
    sysmon_tab_memory_tick();
    sysmon_tab_graphics_tick();
    if (s_tab == SM_TASKS) {
        sysmon_tab_tasks_tick();    /* (only on its tab: see sysmon_core.h) */
    }
}

/**
 * @brief Every SYSMON_HIST_MS from start-up: a sample, and one more point on each tab's graphs (shifted left).
 * @param t  the timer, or NULL for the first idle reading
 */
static void hist_sample(lv_timer_t *t)
{
    sysmon_sample_t k;
    if (!sysmon_hist_sample(&k)) {
        return;    /* (the first idle reading) */
    }
    if (s_scr) {
        sysmon_tab_system_hist_add(&k);
        sysmon_tab_memory_hist_add(&k);
    }
}

/* ------------------------------------------------------------- tabs */
/**
 * @brief Shows a tab and hides the others; the Tasks tab asks for a task list at once.
 * @param tab  SM_SYSTEM .. SM_TASKS
 */
static void select_tab(int tab)
{
    s_tab = tab;
    for (int i = 0; i < SM_TABS; i++) {
        lv_obj_set_state(s_tab_btn[i], LV_STATE_CHECKED, i == tab);
        lv_obj_set_style_text_color(lv_obj_get_child(s_tab_btn[i], 0), i == tab ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_hidden(s_pane[i], i != tab);
    }
    lv_obj_scroll_to_y(s_pane[tab], 0, LV_ANIM_OFF);
    if (s_shown && tab == SM_TASKS) {               /* at once, not at the next tick */
        sysmon_tab_tasks_opened();
    }
}

/**
 * @brief A tab button: shows its tab.
 * @param e  the event (its user data is the tab)
 */
static void on_tab(lv_event_t *e)
{
    select_tab((int)(intptr_t)lv_event_get_user_data(e));
}
/* ------------------------------------------------------------- build */
/* (documented in ui_sysmon.h) */
void sysmon_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb)
{
    if (s_timer) {                                  /* rebuilt on a theme change */
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_scr = scr;

    /* top bar, as on the Wi-Fi page */
    lv_obj_t *back = lv_button_create(scr);
    lv_obj_set_pos(back, 12, 10);
    lv_obj_set_size(back, 150, 52);
    lv_obj_set_style_bg_color(back, C_CARD, 0);
    lv_obj_set_style_bg_color(back, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = label(back, &lv_font_montserrat_18, C_TEXT, LV_SYMBOL_LEFT "  Home");
    lv_obj_center(bl);
    lv_obj_t *h = label(scr, &lv_font_montserrat_28, C_TEXT, "System monitor");
    lv_obj_set_pos(h, 180, 20);

    /* the tabs, under the title (as on Settings); each tab's cards: a scrolling column */
    static const char *const TABS[SM_TABS] = {LV_SYMBOL_SETTINGS "  System", LV_SYMBOL_DRIVE "  Memory",
                                              LV_SYMBOL_SD_CARD "  Storage", LV_SYMBOL_IMAGE "  Graphics",
                                              LV_SYMBOL_LIST "  Tasks"
                                             };
    const int gap = 8, tw = (720 - 24 - (SM_TABS - 1) * gap) / SM_TABS;
    for (int i = 0; i < SM_TABS; i++) {
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, 12 + i * (tw + gap), TAB_Y);
        lv_obj_set_size(b, tw, TAB_H);
        lv_obj_set_style_bg_color(b, C_CARD, 0);
        lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_add_event_cb(b, on_tab, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_center(label(b, &lv_font_montserrat_16, C_TEXT, TABS[i]));
        s_tab_btn[i] = b;

        lv_obj_t *pane = lv_obj_create(scr);
        lv_obj_remove_style_all(pane);
        lv_obj_set_pos(pane, 40, PANE_Y);
        lv_obj_set_size(pane, 720 - 80, 720 - PANE_Y);
        lv_obj_set_flex_flow(pane, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(pane, 10, 0);
        lv_obj_set_style_pad_bottom(pane, 24, 0);
        lv_obj_set_scroll_dir(pane, LV_DIR_VER);
        s_pane[i] = pane;
    }
    sysmon_tab_system_build(s_pane[SM_SYSTEM]);
    sysmon_tab_memory_build(s_pane[SM_MEMORY]);
    sysmon_tab_storage_build(s_pane[SM_STORAGE]);
    sysmon_tab_graphics_build(s_pane[SM_GRAPHICS]);
    sysmon_tab_tasks_build(s_pane[SM_TASKS]);

    select_tab(s_tab);                              /* (a rebuild keeps the tab that was open) */
    sysmon_tab_system_hist_fill();
    sysmon_tab_memory_hist_fill();
    if (!h_timer) {                                 /* the history runs from start-up, page or not */
        h_timer = lv_timer_create(hist_sample, SYSMON_HIST_MS, NULL);
        hist_sample(NULL);                          /* (the first idle reading) */
    }

    s_timer = lv_timer_create(tick, REFRESH_MS, NULL);
    if (!s_shown) {
        lv_timer_pause(s_timer);    /* (a rebuild while open keeps running) */
    }
}

/* (documented in ui_sysmon.h) */
void sysmon_ui_set_shown(bool shown)
{
    if (!s_timer || shown == s_shown) {
        return;
    }
    s_shown = shown;
    if (shown) {
        sysmon_tab_system_shown(true);
        sysmon_tasks_restart();
        s_ticks = 0;
        sysmon_tab_graphics_shown();
        tick(NULL);                                 /* fill at once (task CPU from the next tick) */
        select_tab(s_tab);                          /* the tab last open, scrolled to its top */
        lv_timer_resume(s_timer);
    } else {
        lv_timer_pause(s_timer);
        sysmon_tab_system_shown(false);
    }
}
