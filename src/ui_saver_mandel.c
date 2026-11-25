/**
 * @file ui_saver_mandel.c
 * @ingroup ui_sudoku
 * @brief The Mandelbrot screen saver: a never-ending zoom into the Mandelbrot set (saver_mandel_core.c), its
 *        outside in 3Blue1Brown's colours (deep blue, blue, teal, green, yellow, gold, red, pink and round
 *        again, flowing slowly), its inside black.
 *
 * A worker task on the worker core makes the next level's key while the shown one is zoomed into, a frame at a
 * time, by scaling it up (mandel_frame(), into the savers' picture, all of it redrawn). Writing the whole picture
 * and LVGL redrawing the whole screen take some 40 ms each, so it runs at about 12 frames a second, which a slow
 * zoom hides well. The zoom keeps pace
 * with the worker: it goes as fast as the next key is coming along (never more than MB_MAX_RATE), so it is
 * quick near the top and slows down deeper in, where each point takes more steps; it holds still for a
 * moment if the next key is late. A dive's last level fades out to black, and the next dive, from the whole
 * set again, fades in.
 */
#include "ui_internal.h"
#include "saver_mandel_core.h"

#define MB_FRAME_MS     40                      /**< a frame each, ms (asked: a frame takes some 80) */
#define MB_ROWS         4                       /**< rows the worker makes between its pauses */
#define MB_MAX_RATE     0.35f                   /**< the zoom: at most a level (twice as deep) in about 3 s */
#define MB_MIN_RATE     0.01f                   /**< ... and at least one in 100 s while the key comes */
#define MB_FADE_S       1.0f                    /**< a new dive fades in over this, s */
#define MB_FADE_OUT     0.3f                    /**< a dive's last level fades out over its last this much */
#define MB_FLOW         1                       /**< the colours move this far a frame (of 1024) */
#define MB_STOPS        8                       /**< the palette's colours, round the cycle */

static uint16_t *mb_buf;                  /**< the picture (saver_pic_start()) */
static uint16_t mb_base[1024];            /**< the palette at full brightness */
static uint16_t mb_pal[1024];             /**< the palette as drawn (faded) */
static lv_timer_t *mb_timer;              /**< a frame each MB_FRAME_MS */
static TaskHandle_t mb_task;              /**< the worker (NULL: none) */
static volatile bool mb_quit;             /**< the worker should stop */
static volatile bool mb_gone;             /**< the worker has stopped */
static float mb_p;                        /**< the zoom into the shown key, 0 .. 1 (1: the next key's view) */
static float mb_rate;                     /**< the zoom's speed, levels a second (smoothed) */
static float mb_last_prog;                /**< the next key's progress last frame */
static float mb_fade;                     /**< the brightness, 0 .. 1 */
static float mb_fade_in;                  /**< the new dive's fade in, 0 .. 1 */
static int mb_shown_fade = -1;            /**< the brightness mb_pal is at, 0..256 (-1: none yet) */
static uint32_t mb_shift;                 /**< the colours' flow */
static uint32_t mb_t;                     /**< the last frame's time (lv_tick_get(); 0: none yet) */

/** @brief The palette: 3Blue1Brown's colours round a cycle, each blended into the next. */
static void make_palette(void)
{
    static const uint32_t STOPS[MB_STOPS] = {SV_BLUE_E, SV_BLUE, SV_TEAL, SV_GREEN, SV_YELLOW, SV_GOLD, SV_RED,
                                             SV_PINK
                                            };
    for (int i = 0; i < 1024; i++) {
        const int k = i * MB_STOPS / 1024, u = (i * MB_STOPS % 1024) * 255 / 1024;
        mb_base[i] = saver_pic_mix(STOPS[k], STOPS[(k + 1) % MB_STOPS], u);
    }
    mb_shown_fade = -1;
}

/**
 * @brief The palette as drawn, dimmed to a brightness (only when it changed).
 * @param[in] f  the brightness, 0 .. 1
 */
static void fade_palette(float f)
{
    const int k = (int)(f * 256.0f + 0.5f);
    if (k == mb_shown_fade) {
        return;
    }
    mb_shown_fade = k;
    for (int i = 0; i < 1024; i++) {
        const uint16_t c = mb_base[i];
        const int r = (c >> 11) * k >> 8, g = ((c >> 5) & 63) * k >> 8, b = (c & 31) * k >> 8;
        mb_pal[i] = (uint16_t)(r << 11 | g << 5 | b);
    }
}

/**
 * @brief The worker: makes keys, a few rows at a time, pausing a tick between (the idle task, the watchdog);
 *        waits while the made key isn't shown yet.
 * @param[in] arg  unused
 */
static void mandel_task(void *arg)
{
    (void)arg;
    while (!mb_quit) {
        const bool done = mandel_work(MB_ROWS);
        vTaskDelay(done ? pdMS_TO_TICKS(10) : 1);
    }
    mb_gone = true;
    vTaskDelete(NULL);
}

/**
 * @brief A frame: the zoom moved on at the worker's pace, the next key taken over (or the next dive) when it's
 *        reached and ready, the frame drawn.
 * @param[in] t  the timer (NULL: the first frame)
 */
static void mandel_tick(lv_timer_t *t)
{
    const uint32_t now = lv_tick_get();             /* (the time since the last frame: frames can come late) */
    float dt = mb_t ? (float)(now - mb_t) / 1000.0f : MB_FRAME_MS / 1000.0f;
    dt = dt > 0.25f ? 0.25f : dt;
    mb_t = now;
    if (!is_mandel_shown()) {                       /* the first key: black until it's made */
        if (!is_mandel_ready()) {
            return;
        }
        mandel_advance();
        mb_p = mb_last_prog = 0;
        mb_fade_in = 0;
    }
    /* the zoom's pace: the worker's, smoothed, a little behind it while it works */
    const float prog = mandel_progress();
    if (!is_mandel_ready() && prog >= mb_last_prog) {
        const float r = (prog - mb_last_prog) / dt;
        mb_rate += (r - mb_rate) * 0.05f;
    }
    mb_last_prog = prog;
    float step = mb_rate * dt * (1.0f + 2.0f * (prog - mb_p));   /* (a little faster when behind, slower ahead) */
    step = step < MB_MIN_RATE * dt ? MB_MIN_RATE * dt : step > MB_MAX_RATE * dt ? MB_MAX_RATE * dt : step;
    if (!is_mandel_ready() && mb_p + step > prog) {
        step = prog > mb_p ? prog - mb_p : 0;       /* (never ahead of it: it would have to wait at the end) */
    }
    mb_p += step;
    if (mb_p >= 1.0f) {
        if (is_mandel_ready()) {                    /* the next key's view reached: it takes over */
            const bool new_dive = is_mandel_dive_end();
            mandel_advance();
            mb_p = mb_last_prog = 0;
            if (new_dive) {
                mb_fade_in = 0;
            }
        } else {
            mb_p = 1.0f;                            /* (late: hold) */
        }
    }
    mb_fade_in = mb_fade_in + dt / MB_FADE_S > 1.0f ? 1.0f : mb_fade_in + dt / MB_FADE_S;
    float out = (1.0f - mb_p) / MB_FADE_OUT;        /* (a dive's last level: out, near its end) */
    out = !is_mandel_dive_end() || out > 1.0f ? 1.0f : out;
    mb_fade = out * mb_fade_in;
    fade_palette(mb_fade);
    mb_shift += MB_FLOW;
    saver_perf_mark();
    mandel_frame(mb_buf, mb_p, mb_pal, mb_shift, 0);
    saver_pic_flush(0, SCR_W - 1);
    saver_pic_redraw(0, 0, SCR_W - 1, SCR_W - 1);
    (void)t;
}

/* (documented in ui_internal.h) */
bool saver_mandel_start(lv_obj_t *parent)
{
    if (!mandel_start(esp_random()) || !(mb_buf = saver_pic_start(parent))) {
        saver_mandel_stop();
        return false;
    }
    make_palette();
    mb_p = mb_last_prog = 0;
    mb_rate = MB_MAX_RATE;
    mb_fade = mb_fade_in = 0;
    mb_shift = 0;
    mb_t = 0;
    mb_quit = mb_gone = false;
    if (xTaskCreatePinnedToCore(mandel_task, "mandel", 4096, NULL, 1, &mb_task, WORKER_CORE) != pdPASS) {
        mb_task = NULL;
        saver_mandel_stop();
        return false;
    }
    mb_timer = saver_timer_create(mandel_tick, MB_FRAME_MS);
    return true;
}

/* (documented in ui_internal.h) */
void saver_mandel_stop(void)
{
    if (mb_timer) {
        lv_timer_delete(mb_timer);
        mb_timer = NULL;
    }
    if (mb_task) {                                  /* the worker stops first: it uses the keys */
        mb_quit = true;
        while (!mb_gone) {
            vTaskDelay(1);
        }
        mb_task = NULL;
    }
    if (mb_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        mb_buf = NULL;
    }
    mandel_free();
}
