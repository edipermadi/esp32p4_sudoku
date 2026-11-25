/**
 * @file ui_saver_galaxy.c
 * @ingroup ui_sudoku
 * @brief The galaxy collision screen saver: two galaxies meeting (saver_galaxy_core.c), one in 3Blue1Brown's blues,
 *        the other in its yellows, golds and reds (pale near the core, deeper outwards), each star a 2 x 2 dot on
 *        black, the cores white; the view turns slowly. Each meeting (the pass that flings out the tidal tails, the
 *        second pass, the merger) takes about a minute, then fades out and the next pair comes in.
 *
 * Each frame the last frame's dots are painted black and the new ones drawn with the savers' pen (ui_saver_pic.c),
 * which redraws only what changed.
 */
#include "ui_internal.h"
#include "saver_galaxy_core.h"

#define GX_FRAME_MS     40                      /**< a frame each, ms (25 a second) */
#define GX_LEVELS       8                       /**< brightness levels a tone (the fades) */
#define GX_FADE         40                      /**< frames a fade takes, in or out */
#define GX_DOT          2                       /**< a star's size, px */
#define GX_CORE         4                       /**< a core's size, px */

/** @brief A dot as drawn. */
typedef struct {
    int16_t x, y;                         /**< its top left, px (x < 0: none) */
} gx_dot_t;

static uint16_t *gx_buf;                  /**< the picture (saver_pic_start(); drawn on with the pen) */
static gx_dot_t *gx_was;                  /**< each star's dot as last drawn, then the two cores' */
static uint16_t gx_col[GALAXY_TONES + 1][GX_LEVELS];   /**< each tone (and the cores' white) at each brightness */
static lv_timer_t *gx_timer;              /**< a frame each GX_FRAME_MS */
static int gx_frame;                      /**< frames since this meeting began */
static int gx_out;                        /**< frames into the fade out (0: not fading out) */

/** @brief The colours: each tone, and the cores' white, from black up to full. */
static void make_colors(void)
{
    static const uint32_t TONE[GALAXY_TONES + 1] = {SV_BLUE_A, SV_BLUE, SV_BLUE_D, SV_YELLOW_A, SV_GOLD, SV_RED,
                                                    0xFFFFFF
                                                   };
    for (int t = 0; t <= GALAXY_TONES; t++) {
        for (int l = 0; l < GX_LEVELS; l++) {
            gx_col[t][l] = saver_pic_mix(0x000000, TONE[t], (l + 1) * 255 / GX_LEVELS);
        }
    }
}

/**
 * @brief A frame: the galaxies moved on, last frame's dots erased and the new ones drawn (faded at a meeting's start
 *        and end), what changed redrawn; the next meeting once this one has faded out.
 * @param[in] t  the timer (NULL: the first frame)
 */
static void galaxy_tick(lv_timer_t *t)
{
    (void)t;
    if (gx_out >= GX_FADE) {                        /* faded out: the next pair */
        galaxy_start(esp_random());
        gx_frame = gx_out = 0;
    }
    galaxy_step();
    gx_frame++;
    if (is_galaxy_done() || gx_out) {
        gx_out++;
    }
    const int in = gx_frame < GX_FADE ? gx_frame : GX_FADE, out = gx_out ? GX_FADE - gx_out : GX_FADE;
    const int level = (in < out ? in : out) * (GX_LEVELS - 1) / GX_FADE;
    saver_perf_mark();
    saver_pen_begin();
    for (int i = 0; i < GALAXY_STARS + 2; i++) {    /* last frame's dots erased */
        if (gx_was[i].x >= 0) {
            saver_pen_square(gx_was[i].x, gx_was[i].y, i < GALAXY_STARS ? GX_DOT : GX_CORE, 0);
        }
    }
    for (int i = 0; i < GALAXY_STARS; i++) {
        int x, y, tone;
        gx_was[i].x = -1;
        if (galaxy_get(i, &x, &y, &tone)) {
            gx_was[i].x = (int16_t)x;
            gx_was[i].y = (int16_t)y;
            saver_pen_square(x, y, GX_DOT, gx_col[tone][level]);
        }
    }
    for (int k = 0; k < 2; k++) {                   /* the cores last, on top */
        gx_dot_t *d = &gx_was[GALAXY_STARS + k];
        int x, y;
        d->x = -1;
        if (galaxy_core(k, &x, &y)) {
            d->x = (int16_t)(x - GX_CORE / 2);
            d->y = (int16_t)(y - GX_CORE / 2);
            saver_pen_square(d->x, d->y, GX_CORE, gx_col[GALAXY_TONES][level]);
        }
    }
    saver_pen_end();
}

/* (documented in ui_internal.h) */
bool saver_galaxy_start(lv_obj_t *parent)
{
    if (!gx_was) {
        gx_was = malloc((GALAXY_STARS + 2) * sizeof(*gx_was));
    }
    if (!gx_was || !galaxy_start(esp_random()) || !(gx_buf = saver_pic_start(parent))) {
        saver_galaxy_stop();
        return false;
    }
    for (int i = 0; i < GALAXY_STARS + 2; i++) {
        gx_was[i].x = -1;
    }
    make_colors();
    gx_frame = gx_out = 0;
    galaxy_tick(NULL);
    gx_timer = saver_timer_create(galaxy_tick, GX_FRAME_MS);
    return true;
}

/* (documented in ui_internal.h) */
void saver_galaxy_stop(void)
{
    if (gx_timer) {
        lv_timer_delete(gx_timer);
        gx_timer = NULL;
    }
    if (gx_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        gx_buf = NULL;
    }
    free(gx_was);
    gx_was = NULL;
    galaxy_free();
}
