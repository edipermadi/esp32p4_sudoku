/**
 * @file ui_saver_ripple.c
 * @ingroup ui_sudoku
 * @brief The water ripples screen saver: raindrops (and now and then a stone) falling on a pool
 *        (saver_ripple_core.c), the rings spreading, crossing and bouncing off the sides, the tiled floor in
 *        3Blue1Brown's blues seen through the water, bent by the waves, with white glints.
 *
 * A worker task on the worker core steps the waves (RP_STEPS a frame) and shades the picture (ripple_publish());
 * each frame doubles the newest picture's pixels into the savers' picture (ripple_frame()), all of it redrawn.
 */
#include "ui_internal.h"
#include "saver_ripple_core.h"

#define RP_FRAME_MS     40                      /**< a frame each, ms (asked: a full-screen frame takes some 80) */
#define RP_PACE_MS      40                      /**< the worker's round (RP_STEPS steps and a picture), ms at least */
#define RP_STEPS        3                       /**< wave steps a picture */

static uint16_t *rp_buf;                  /**< the picture (saver_pic_start()) */
static lv_timer_t *rp_timer;              /**< a frame each RP_FRAME_MS */
static TaskHandle_t rp_task;              /**< the worker (NULL: none) */
static volatile bool rp_quit;             /**< the worker should stop */
static volatile bool rp_gone;             /**< the worker has stopped */

/** @brief The pool's colours (3Blue1Brown's): the tiles, the joints, the glints. */
static const uint32_t RP_COLOURS[RIPPLE_TONES] = {SV_BLUE_D, SV_BLUE_A, 0xFFFFFF};

/**
 * @brief The worker: the waves stepped and a picture shaded, a round each RP_PACE_MS at most.
 * @param[in] arg  unused
 */
static void ripple_task(void *arg)
{
    (void)arg;
    while (!rp_quit) {
        const TickType_t t0 = xTaskGetTickCount();
        for (int k = 0; k < RP_STEPS; k++) {
            ripple_step();
        }
        ripple_publish();                           /* (skipped if the last one isn't drawn yet) */
        const TickType_t spent = xTaskGetTickCount() - t0;   /* (a tick at least: the idle task, the watchdog) */
        vTaskDelay(spent + 1 < pdMS_TO_TICKS(RP_PACE_MS) ? pdMS_TO_TICKS(RP_PACE_MS) - spent : 1);
    }
    rp_gone = true;
    vTaskDelete(NULL);
}

/**
 * @brief A frame: the newest picture drawn, if there's one.
 * @param[in] t  the timer
 */
static void ripple_tick(lv_timer_t *t)
{
    (void)t;
    if (!is_ripple_fresh()) {
        return;
    }
    saver_perf_mark();
    if (ripple_frame(rp_buf)) {
        saver_pic_flush(0, SCR_W - 1);
        saver_pic_redraw(0, 0, SCR_W - 1, SCR_W - 1);
    }
}

/* (documented in ui_internal.h) */
bool saver_ripple_start(lv_obj_t *parent)
{
    if (!ripple_start(esp_random(), RP_COLOURS) || !(rp_buf = saver_pic_start(parent))) {
        saver_ripple_stop();
        return false;
    }
    rp_quit = rp_gone = false;
    if (xTaskCreatePinnedToCore(ripple_task, "ripple", 4096, NULL, 1, &rp_task, WORKER_CORE) != pdPASS) {
        rp_task = NULL;
        saver_ripple_stop();
        return false;
    }
    rp_timer = saver_timer_create(ripple_tick, RP_FRAME_MS);
    return true;
}

/* (documented in ui_internal.h) */
void saver_ripple_stop(void)
{
    if (rp_timer) {
        lv_timer_delete(rp_timer);
        rp_timer = NULL;
    }
    if (rp_task) {                                  /* the worker stops first: it uses the pool */
        rp_quit = true;
        while (!rp_gone) {
            vTaskDelay(1);
        }
        rp_task = NULL;
    }
    if (rp_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        rp_buf = NULL;
    }
    ripple_free();
}
