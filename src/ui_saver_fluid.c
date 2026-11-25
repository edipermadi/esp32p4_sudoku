/**
 * @file ui_saver_fluid.c
 * @ingroup ui_sudoku
 * @brief The liquid mixing screen saver: a tank of liquids in 3Blue1Brown's blue, yellow and red, stirred by
 *        invisible spoons that pour more of their colour as they go (saver_fluid_core.c), the colours folding
 *        into each other in swirls and marbling; every FL_RESTART_MS the tank fades out and a fresh one, in
 *        new bands and stirred along new paths, fades in.
 *
 * A worker task on the worker core steps the liquid and hands over each step's colours (fluid_publish()); each
 * frame draws the newest ones, scaled up smoothly, into the savers' picture (fluid_frame()), all of it redrawn.
 */
#include "ui_internal.h"
#include "saver_fluid_core.h"

#define FL_FRAME_MS     40                      /**< a frame each, ms (asked: a full-screen frame takes some 80) */
#define FL_STEP_MS      40                      /**< a step of the liquid each, ms, at most (25 a second) */
#define FL_RESTART_MS   (100u * 1000u)          /**< a fresh tank every 100 s (before it all mixes into one colour) */
#define FL_FADE_MS      1500u                   /**< fading out or in, ms */

static uint16_t *fl_buf;                  /**< the picture (saver_pic_start()) */
static lv_timer_t *fl_timer;              /**< a frame each FL_FRAME_MS */
static TaskHandle_t fl_task;              /**< the worker (NULL: none) */
static volatile bool fl_quit;             /**< the worker should stop */
static volatile bool fl_gone;             /**< the worker has stopped */
static volatile bool fl_refill;           /**< the worker should start a fresh tank */
static uint32_t fl_since;                 /**< when this tank began (lv_tick_get()) */

/** @brief The dyes' colours (3Blue1Brown's). */
static const uint32_t FL_COLOURS[FLUID_SPOONS] = {SV_BLUE, SV_YELLOW, SV_RED};

/**
 * @brief The worker: steps the liquid, at most a step each FL_STEP_MS, handing over each step's colours; starts a
 *        fresh tank when asked (while the screen is dark).
 * @param[in] arg  unused
 */
static void fluid_task(void *arg)
{
    (void)arg;
    while (!fl_quit) {
        const TickType_t t0 = xTaskGetTickCount();
        if (fl_refill) {
            fluid_start(esp_random(), FL_COLOURS);  /* (its memory kept: it can't fail now) */
            fl_refill = false;
        }
        fluid_step();
        fluid_publish();                            /* (skipped if the last ones aren't drawn yet) */
        const TickType_t spent = xTaskGetTickCount() - t0;   /* (a tick at least: the idle task, the watchdog) */
        vTaskDelay(spent + 1 < pdMS_TO_TICKS(FL_STEP_MS) ? pdMS_TO_TICKS(FL_STEP_MS) - spent : 1);
    }
    fl_gone = true;
    vTaskDelete(NULL);
}

/**
 * @brief A frame: the newest colours drawn (faded at a tank's start and end), the next tank asked for when this
 *        one has faded out.
 * @param[in] t  the timer
 */
static void fluid_tick(lv_timer_t *t)
{
    (void)t;
    const uint32_t age = lv_tick_elaps(fl_since);
    if (age >= FL_RESTART_MS && !fl_refill) {       /* faded out: a fresh tank */
        fl_refill = true;
        fl_since = lv_tick_get();
        return;
    }
    if (fl_refill || !is_fluid_fresh()) {
        return;                                     /* (nothing new to draw) */
    }
    const uint32_t left = FL_RESTART_MS - age;
    const uint32_t f = age < FL_FADE_MS ? age : left < FL_FADE_MS ? left : FL_FADE_MS;
    saver_perf_mark();
    if (fluid_frame(fl_buf, (int)(f * 256u / FL_FADE_MS))) {
        saver_pic_flush(0, SCR_W - 1);
        saver_pic_redraw(0, 0, SCR_W - 1, SCR_W - 1);
    }
}

/* (documented in ui_internal.h) */
bool saver_fluid_start(lv_obj_t *parent)
{
    if (!fluid_start(esp_random(), FL_COLOURS) || !(fl_buf = saver_pic_start(parent))) {
        saver_fluid_stop();
        return false;
    }
    fl_quit = fl_gone = fl_refill = false;
    fl_since = lv_tick_get();
    if (xTaskCreatePinnedToCore(fluid_task, "fluid", 4096, NULL, 1, &fl_task, WORKER_CORE) != pdPASS) {
        fl_task = NULL;
        saver_fluid_stop();
        return false;
    }
    fl_timer = saver_timer_create(fluid_tick, FL_FRAME_MS);
    return true;
}

/* (documented in ui_internal.h) */
void saver_fluid_stop(void)
{
    if (fl_timer) {
        lv_timer_delete(fl_timer);
        fl_timer = NULL;
    }
    if (fl_task) {                                  /* the worker stops first: it uses the tank */
        fl_quit = true;
        while (!fl_gone) {
            vTaskDelay(1);
        }
        fl_task = NULL;
    }
    if (fl_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        fl_buf = NULL;
    }
    fluid_free();
}
