/**
 * @file ui_sysmon_graphics.c
 * @ingroup ui_sysmon
 * @brief System monitor > Graphics: LVGL's frame rate, how busy it is, draw times, objects and timers.
 */
#include "ui_sysmon_tab.h"
#include "ui_palette.h"
#include <stdio.h>
#include "lvgl_private.h"            /* the display's perf subject, its screens */
#include "esp_timer.h"

static lv_obj_t *s_fps;                             /**< frames drawn per second */
static lv_obj_t *s_lv_cpu;                          /**< how busy LVGL is */
static lv_obj_t *s_lv_times;                        /**< draw time, average and longest */
static lv_obj_t *s_lv_objs;                         /**< the objects, and on how many screens */
static lv_obj_t *s_lv_timers;                       /**< the timers */
static lv_obj_t *s_scr;                             /**< the page's screen (its display) */

/* LVGL: its sysmon's busy % (perf_cb, every 300 ms), averaged per tick; frames
 * and draw times counted here (disp_ev): sysmon's FPS counts refresh-timer
 * runs, drawn or not, so an idle screen shows "50". */
static uint32_t s_cpu_sum;                          /**< LVGL busy %, added up since the last tick */
static uint32_t s_cpu_n;                            /**< ... how many reports */
static int64_t s_render_t0;                         /**< when the frame being drawn started (0: none) */
static int64_t s_window_t0;                         /**< when the counting window started (0: from the next tick) */
static uint32_t s_frames;                           /**< frames drawn in the window */
static uint32_t s_draw_us_sum;                      /**< their draw times, added up (us) */
static uint32_t s_draw_us_max;                      /**< the longest (us) */

/* ------------------------------------------------------------- sources */
/**
 * @brief LVGL's sysmon report (every 300 ms): its busy %, added up for the next tick's average.
 * @param o        the observer
 * @param subject  the display's perf subject
 */
static void perf_cb(lv_observer_t *o, lv_subject_t *subject)
{
    const lv_sysmon_perf_info_t *p = lv_subject_get_pointer(subject);
    if (!p) {
        return;
    }
    s_cpu_sum += p->calculated.cpu;
    s_cpu_n++;
}

/**
 * @brief A frame drawn (RENDER_START, then RENDER_READY, only when something was redrawn): counted, with its time.
 * @param e  the display's event
 */
static void disp_ev(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();
    if (lv_event_get_code(e) == LV_EVENT_RENDER_START) {
        s_render_t0 = now;
    } else if (s_render_t0) {
        uint32_t us = (uint32_t)(now - s_render_t0);
        s_render_t0 = 0;
        s_frames++;
        s_draw_us_sum += us;
        if (us > s_draw_us_max) {
            s_draw_us_max = us;
        }
    }
}

/**
 * @brief The objects in a tree.
 * @param o  its root (may be NULL)
 * @return the root and all under it
 */
static uint32_t count_tree(lv_obj_t *o)
{
    if (!o) {
        return 0;
    }
    uint32_t n = 1, c = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < c; i++) {
        n += count_tree(lv_obj_get_child(o, (int32_t)i));
    }
    return n;
}

/**
 * @brief The last second's frames per second, draw times and LVGL busy %; the objects on every screen
 *        and layer, and the timers.
 */
static void show_lvgl(void)
{
    char buf[64];
    int64_t now = esp_timer_get_time();
    uint32_t ms = (uint32_t)((now - s_window_t0) / 1000);
    if (s_window_t0 && ms) {
        uint32_t f10 = (uint32_t)((uint64_t)s_frames * 10000 / ms);          /* frames/s x10 */
        snprintf(buf, sizeof(buf), "%u.%u", (unsigned)(f10 / 10), (unsigned)(f10 % 10));
        set_text(s_fps, buf);
        if (s_frames) {
            uint32_t avg = s_draw_us_sum / s_frames;
            snprintf(buf, sizeof(buf), "%u.%u / %u.%u ms", (unsigned)(avg / 1000), (unsigned)(avg % 1000 / 100),
                     (unsigned)(s_draw_us_max / 1000), (unsigned)(s_draw_us_max % 1000 / 100));
            set_text(s_lv_times, buf);
        }
    }
    if (s_cpu_n) {
        snprintf(buf, sizeof(buf), "%u %%", (unsigned)(s_cpu_sum / s_cpu_n));
        set_text(s_lv_cpu, buf);
    }
    s_window_t0 = now;
    s_frames = s_draw_us_sum = s_draw_us_max = 0;
    s_cpu_sum = s_cpu_n = 0;
    lv_display_t *d = lv_obj_get_display(s_scr);
    uint32_t objs = 0;
    for (uint32_t i = 0; i < d->screen_cnt; i++) {
        objs += count_tree(d->screens[i]);
    }
    objs += count_tree(d->top_layer) + count_tree(d->sys_layer) + count_tree(d->bottom_layer);
    snprintf(buf, sizeof(buf), "%u on %u screens", (unsigned)objs, (unsigned)d->screen_cnt);
    set_text(s_lv_objs, buf);
    uint32_t timers = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) {
        timers++;
    }
    snprintf(buf, sizeof(buf), "%u", (unsigned)timers);
    set_text(s_lv_timers, buf);
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_graphics_build(lv_obj_t *pane)
{
    lv_obj_t *c;
    lv_obj_t *scr = lv_obj_get_screen(pane);
    s_scr = scr;
    c = card(pane);
    s_fps = kv(c, "Frames drawn per second");
    s_lv_cpu = kv(c, "LVGL busy");
    s_lv_times = kv(c, "Draw time, avg / max");
    s_lv_objs = kv(c, "Objects");
    s_lv_timers = kv(c, "Timers");

    /* LVGL's sysmon report (every 300 ms; its on-screen box stays hidden) */
    lv_display_t *d = lv_obj_get_display(scr);
    if (d && d->perf_sysmon_backend.subject) {
        lv_subject_add_observer_obj(d->perf_sysmon_backend.subject, perf_cb, scr, NULL);
    }
    static bool s_disp_cb;                          /* the display outlives the page: once */
    if (d && !s_disp_cb) {
        s_disp_cb = true;
        lv_display_add_event_cb(d, disp_ev, LV_EVENT_RENDER_START, NULL);
        lv_display_add_event_cb(d, disp_ev, LV_EVENT_RENDER_READY, NULL);
    }
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_graphics_tick(void)
{
    show_lvgl();
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_graphics_shown(void)
{
    s_window_t0 = 0;                                /* frame counts from the next tick */
}
