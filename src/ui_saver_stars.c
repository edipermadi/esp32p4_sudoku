/**
 * @file ui_saver_stars.c
 * @ingroup ui_sudoku
 * @brief The starfield screen saver: flying through the stars (saver_stars_core.c) on black, 25 frames a
 *        second, each star a streak from where it was to where it is, mostly pale blue-white, some
 *        tinted with 3Blue1Brown's blue, yellow, gold or teal, dim and thin far away, bright and thick
 *        as it rushes past; the flight surges (long streaks) and calms in slow waves, rolling gently.
 *
 * Each frame the last frame's streaks are painted black and the new ones drawn with the savers' pen
 * (ui_saver_pic.c), which redraws only what changed.
 */
#include "ui_internal.h"
#include "saver_stars_core.h"

#define ST_FRAME_MS     40                      /**< a frame each, ms (25 a second) */
#define ST_LEVELS       16                      /**< brightness levels a tone */
#define ST_MIN_BRIGHT   6                       /**< dimmer stars aren't drawn (0..255) */

/** @brief A streak as drawn. */
typedef struct {
    int16_t x0, y0, x1, y1;               /**< from, to, px */
    uint8_t w;                            /**< its width, px (0: none) */
} st_streak_t;

static uint16_t *st_buf;                  /**< the picture (saver_pic_start(); drawn on with the pen) */
static st_streak_t *st_was;               /**< each star's streak as last drawn */
static uint16_t st_col[STARS_TONES][ST_LEVELS];   /**< each tone at each brightness, RGB565 */
static lv_timer_t *st_timer;              /**< a frame each ST_FRAME_MS */

/** @brief The colours: each tone from black up to its full colour. */
static void make_colors(void)
{
    static const uint32_t TONE[STARS_TONES] = {0xE8F4FF, SV_BLUE_A, SV_BLUE, SV_YELLOW_A, SV_GOLD, SV_TEAL};
    for (int t = 0; t < STARS_TONES; t++) {
        for (int l = 0; l < ST_LEVELS; l++) {
            st_col[t][l] = saver_pic_mix(0x000000, TONE[t], (l + 1) * 255 / ST_LEVELS);
        }
    }
}

/**
 * @brief A frame: the stars fly on, last frame's streaks erased and the new ones drawn, what changed
 *        redrawn.
 * @param[in] t  the timer (NULL: the first frame)
 */
static void stars_tick(lv_timer_t *t)
{
    stars_step();
    saver_perf_mark();
    saver_pen_begin();
    for (int i = 0; i < STARS_N; i++) {             /* last frame's streaks erased (st_was: as drawn) */
        const st_streak_t *s = &st_was[i];
        if (s->w) {
            saver_pen_line(s->x0, s->y0, s->x1, s->y1, s->w, 0);
        }
    }
    for (int i = 0; i < STARS_N; i++) {
        st_streak_t *s = &st_was[i];
        int x0, y0, x1, y1, bright, tone;
        s->w = 0;
        if (!stars_get(i, &x0, &y0, &x1, &y1, &bright, &tone) || bright < ST_MIN_BRIGHT) {
            continue;
        }
        s->x0 = (int16_t)x0;
        s->y0 = (int16_t)y0;
        s->x1 = (int16_t)x1;
        s->y1 = (int16_t)y1;
        s->w = (uint8_t)(bright < 90 ? 1 : bright < 190 ? 2 : 3);   /* bigger as it comes near */
        saver_pen_line(x0, y0, x1, y1, s->w, st_col[tone][bright * ST_LEVELS / 256]);
    }
    saver_pen_end();
}

/* (documented in ui_internal.h) */
bool saver_stars_start(lv_obj_t *parent)
{
    if (!st_was) {
        st_was = calloc(STARS_N, sizeof(*st_was));
    }
    if (!st_was || !stars_start(esp_random()) || !(st_buf = saver_pic_start(parent))) {
        saver_stars_stop();
        return false;
    }
    memset(st_was, 0, STARS_N * sizeof(*st_was));
    make_colors();
    stars_tick(NULL);
    st_timer = saver_timer_create(stars_tick, ST_FRAME_MS);
    return true;
}

/* (documented in ui_internal.h) */
void saver_stars_stop(void)
{
    if (st_timer) {
        lv_timer_delete(st_timer);
        st_timer = NULL;
    }
    if (st_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        st_buf = NULL;
    }
    free(st_was);
    st_was = NULL;
    stars_free();
}
