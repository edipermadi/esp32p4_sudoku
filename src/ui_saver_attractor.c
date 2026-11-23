/**
 * @file ui_saver_attractor.c
 * @ingroup ui_sudoku
 * @brief The strange attractor screen saver: a Clifford or de Jong attractor (attractor_core.c) drawn
 *        point by point in 3Blue1Brown's colours, each new point a white spark for a frame, the shape
 *        going from deep blue through blue, green and yellow to white where it is denser; once drawn the screen clears and
 *        the next one is drawn from nothing.
 *
 * The points come slowly at first (ATTR_START_POINTS a frame), then more each frame up to
 * ATTR_MAX_POINTS, so the shape gathers out of scattered sparks and then fills in. Each frame repaints
 * only the pixels last frame's and this frame's points landed on, in the savers' picture
 * (ui_saver_pic.c): a pixel's colour comes from how many points it has had, on a log scale.
 */
#include "ui_internal.h"
#include "attractor_core.h"
#include <math.h>

#define ATTR_FRAME_MS       100                 /**< a frame each, ms */
#define ATTR_START_POINTS   150                 /**< points plotted in the first frame */
#define ATTR_GROW           1.02f               /**< each frame plots this many times more ... */
#define ATTR_MAX_POINTS     10000               /**< ... up to this many */
#define ATTR_BUILD_FRAMES   1200                /**< frames an attractor is drawn for (2 minutes) */
#define ATTR_SPARK          0xFFFFFF            /**< a new point, for its first frame */

static uint16_t *at_buf;                  /**< the picture (saver_pic_start()) */
static uint32_t *at_new;                  /**< the cells this frame's points landed on */
static uint32_t *at_old;                  /**< last frame's (sparks to settle) */
static int at_n_old;                      /**< how many in at_old */
static uint16_t at_pal[ATTR_MAX + 1];     /**< the colour of each count, RGB565 */
static uint16_t at_spark;                 /**< ATTR_SPARK, RGB565 */
static lv_timer_t *at_timer;              /**< a frame each ATTR_FRAME_MS */
static int at_frame;                      /**< frames since this attractor began */
static float at_points;                   /**< points to plot this frame (growing) */

/** @brief The colours (3Blue1Brown's): black, then deep blue, blue, green, yellow and white, on a log scale. */
static void make_palette(void)
{
    static const uint32_t STOPS[] = {0x000000, SV_BLUE_E, SV_BLUE, SV_GREEN, SV_YELLOW, 0xFFFFFF};
    const int n = sizeof(STOPS) / sizeof(STOPS[0]) - 1;
    at_pal[0] = 0;
    for (int c = 1; c <= ATTR_MAX; c++) {
        float t = logf(1.0f + c) / logf(1.0f + ATTR_MAX);       /* 0..1 */
        float f = 0.25f + 0.75f * t;                            /* (a single point already shows) */
        int k = (int)(f * n);
        k = k >= n ? n - 1 : k;
        float u = f * n - k;
        lv_color_t a = lv_color_hex(STOPS[k]), b = lv_color_hex(STOPS[k + 1]);
        at_pal[c] = lv_color_to_u16(lv_color_make((uint8_t)(a.red + (b.red - a.red) * u),
                                    (uint8_t)(a.green + (b.green - a.green) * u),
                                    (uint8_t)(a.blue + (b.blue - a.blue) * u)));
    }
    at_spark = lv_color_to_u16(lv_color_hex(ATTR_SPARK));
}

/**
 * @brief Repaints a box of the picture from the counts, and redraws it.
 * @param[in] x0  left
 * @param[in] y0  top
 * @param[in] x1  right (included)
 * @param[in] y1  bottom (included)
 */
static void paint_box(int x0, int y0, int x1, int y1)
{
    const uint8_t *cnt = attr_counts();
    for (int y = y0; y <= y1; y++) {
        uint16_t *p = at_buf + y * SCR_W;
        const uint8_t *c = cnt + y * ATTR_W;
        for (int x = x0; x <= x1; x++) {
            p[x] = at_pal[c[x]];
        }
    }
    saver_pic_flush(y0, y1);
    saver_pic_redraw(x0, y0, x1, y1);
}

/**
 * @brief Paints the cells in a list, each in a colour, and widens a box around them.
 * @param[in] cells  the cells (y * ATTR_W + x)
 * @param[in] n  how many
 * @param[in] spark  white-hot (a new point), else its count's colour
 * @param[in,out] box  x0, y0, x1, y1, widened
 */
static void paint_cells(const uint32_t *cells, int n, bool spark, int *box)
{
    const uint8_t *cnt = attr_counts();
    for (int i = 0; i < n; i++) {
        uint32_t k = cells[i];
        int x = (int)(k % ATTR_W), y = (int)(k / ATTR_W);
        at_buf[y * SCR_W + x] = spark ? at_spark : at_pal[cnt[k]];
        box[0] = LV_MIN(box[0], x);
        box[1] = LV_MIN(box[1], y);
        box[2] = LV_MAX(box[2], x);
        box[3] = LV_MAX(box[3], y);
    }
}

/**
 * @brief A frame: last frame's sparks settle to their colour and new points are plotted as sparks;
 *        or, once drawn, the screen cleared and the next attractor started.
 * @param[in] t  the timer
 */
static void attr_tick(lv_timer_t *t)
{
    int x0, y0, x1, y1;
    attr_box(&x0, &y0, &x1, &y1);
    at_frame++;
    if (at_frame > ATTR_BUILD_FRAMES) {             /* drawn: clear, and the next one from nothing */
        attr_next();
        paint_box(x0, y0, x1, y1);                  /* (the old box: black) */
        at_frame = 0;
        at_points = ATTR_START_POINTS;
        at_n_old = 0;
        return;
    }
    int box[4] = {ATTR_W, ATTR_W, -1, -1};
    paint_cells(at_old, at_n_old, false, box);      /* last frame's sparks settle */
    at_n_old = 0;
    int n = attr_plot((int)at_points, at_new, ATTR_MAX_POINTS);
    paint_cells(at_new, n, true, box);
    uint32_t *sw = at_old;                          /* (this frame's sparks settle next frame) */
    at_old = at_new;
    at_new = sw;
    at_n_old = n;
    at_points = LV_MIN(at_points * ATTR_GROW, (float)ATTR_MAX_POINTS);
    if (box[2] >= 0) {
        saver_pic_flush(box[1], box[3]);
        saver_pic_redraw(box[0], box[1], box[2], box[3]);
    }
}

/* (documented in ui_internal.h) */
bool saver_attractor_start(lv_obj_t *parent)
{
    if (!at_new) {
        at_new = malloc(ATTR_MAX_POINTS * sizeof(*at_new));
    }
    if (!at_old) {
        at_old = malloc(ATTR_MAX_POINTS * sizeof(*at_old));
    }
    if (!at_new || !at_old || !attr_start(esp_random()) || !(at_buf = saver_pic_start(parent))) {
        saver_attractor_stop();
        return false;
    }
    make_palette();
    at_frame = 0;
    at_points = ATTR_START_POINTS;
    at_n_old = 0;
    at_timer = lv_timer_create(attr_tick, ATTR_FRAME_MS, NULL);
    return true;
}

/* (documented in ui_internal.h) */
void saver_attractor_stop(void)
{
    if (at_timer) {
        lv_timer_delete(at_timer);
        at_timer = NULL;
    }
    if (at_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        at_buf = NULL;
    }
    free(at_new);
    free(at_old);
    at_new = at_old = NULL;
    attr_free();
}
