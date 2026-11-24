/**
 * @file ui_saver_perf.c
 * @ingroup ui_sudoku
 * @brief Where a screen saver's time goes, for devctl's perf: each frame's tick split into working out
 *        (calc), painting the picture (draw) and flushing it from the cache and marking it for redraw
 *        (flush), and LVGL's screen refreshes that put it on the panel.
 *
 * The savers create their frame timer with saver_timer_create(), which times each tick, and call
 * saver_perf_mark() between working out and painting; ui_saver_pic.c adds its flushing time
 * (saver_perf_flush_add()). The refreshes are timed by the display's refresh events. Nothing is
 * counted outside a measurement, and the counting itself costs a few esp_timer reads a frame.
 */
#include "ui_internal.h"
#include "esp_timer.h"

static sudoku_ui_perf_t pf;               /**< this measurement's sums */
static bool pf_on;                        /**< measuring */
static int64_t pf_tick_start;             /**< the tick being timed began, us (0: none) */
static int64_t pf_mark;                   /**< its calc ended, us (0: not marked) */
static uint32_t pf_tick_flush;            /**< its flushing so far, us */
static int64_t pf_refr_start;             /**< the refresh being timed began, us (0: none) */
static bool pf_hooked;                    /**< the refresh events are hooked */

/**
 * @brief A tick timed: the saver's own callback run between two clock reads.
 * @param[in] t  the timer (its user data: the saver's callback)
 */
static void timed_tick(lv_timer_t *t)
{
    lv_timer_cb_t cb = (lv_timer_cb_t)lv_timer_get_user_data(t);
    if (!pf_on) {
        cb(t);
        return;
    }
    pf_tick_start = esp_timer_get_time();
    pf_mark = 0;
    pf_tick_flush = 0;
    cb(t);
    int64_t end = esp_timer_get_time();
    uint32_t total = (uint32_t)(end - pf_tick_start);
    uint32_t calc = pf_mark ? (uint32_t)(pf_mark - pf_tick_start) : 0;
    uint32_t flush = LV_MIN(pf_tick_flush, total - calc);
    pf.frames++;
    pf.tick_us += total;
    pf.calc_us += calc;
    pf.flush_us += flush;
    pf.draw_us += total - calc - flush;
    pf.tick_max_us = LV_MAX(pf.tick_max_us, total);
    pf_tick_start = 0;
}

/**
 * @brief The display's refresh starting and finished: timed.
 * @param[in] e  LV_EVENT_REFR_START or LV_EVENT_REFR_READY
 */
static void refr_cb(lv_event_t *e)
{
    if (!pf_on) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) {
        pf_refr_start = esp_timer_get_time();
    } else if (pf_refr_start) {
        uint32_t d = (uint32_t)(esp_timer_get_time() - pf_refr_start);
        pf.refreshes++;
        pf.refresh_us += d;
        pf.refresh_max_us = LV_MAX(pf.refresh_max_us, d);
        pf_refr_start = 0;
    }
}

/* (documented in ui_internal.h) */
lv_timer_t *saver_timer_create(lv_timer_cb_t cb, uint32_t ms)
{
    return lv_timer_create(timed_tick, ms, (void *)cb);
}

/* (documented in ui_internal.h) */
void saver_perf_mark(void)
{
    if (pf_on && pf_tick_start) {
        pf_mark = esp_timer_get_time();
    }
}

/* (documented in ui_internal.h) */
void saver_perf_flush_add(uint32_t us)
{
    if (pf_on && pf_tick_start) {
        pf_tick_flush += us;
    }
}

/* (documented in ui_internal.h) */
bool is_saver_perf_on(void) { return pf_on; }

/** @brief Starts a measurement: the sums cleared, ticks and refreshes timed from now. */
static void perf_start(void)
{
    if (!pf_hooked) {
        lv_display_t *d = lv_display_get_default();
        lv_display_add_event_cb(d, refr_cb, LV_EVENT_REFR_START, NULL);
        lv_display_add_event_cb(d, refr_cb, LV_EVENT_REFR_READY, NULL);
        pf_hooked = true;
    }
    memset(&pf, 0, sizeof(pf));
    pf_tick_start = pf_refr_start = 0;
    pf_on = true;
}

/* (documented in ui_sudoku.h) */
void sudoku_ui_perf(uint32_t ms, sudoku_ui_perf_t *out)
{
    perf_start();
    int64_t t0 = esp_timer_get_time();
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    do {                                            /* as loop(): the timers, then sleep till the next is due */
        uint32_t wait = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(wait < 5 ? 1 : wait > 20 ? 20 : wait));
    } while ((int32_t)(end - xTaskGetTickCount()) > 0);
    pf_on = false;
    pf.ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    pf.saver = ui_saver_shown();
    *out = pf;
}
