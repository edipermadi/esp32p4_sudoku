/**
 * @file ui_saver_automata.c
 * @ingroup ui_sudoku
 * @brief The cellular automata screen savers (Game of Life, Brian's Brain, Langton's ant and the rest):
 *        one rule (saver_automata_core.c) on a grid of round dots, in 3Blue1Brown's colours on black, the unlit
 *        dots faintly there. Life-like rules are blue with yellow newborns, dead cells fading through deep
 *        blue (Brian's Brain: firing yellow, dying blue); cyclic rules go round a wheel of colours; the
 *        excitable medium's waves are yellow with a refractory tail from gold to deep blue; the forest is
 *        green, its fires yellow, the embers red; Langton's ant is yellow on blue cells.
 *
 * Each generation the dots whose key changed are drawn with the savers' pen (ui_saver_pic.c), which
 * redraws only what changed.
 */
#include "ui_internal.h"
#include "saver_automata_core.h"

#define AU_PX           (SCR_W / AUTO_N)        /**< a cell's size, px (6) */
#define AU_PURPLE       0x9A72AC                /**< 3Blue1Brown's purple (only here) */
#define AU_UNLIT        0x0C1417                /**< an unlit dot: barely there */

static uint16_t *au_buf;                  /**< the picture (saver_pic_start(); drawn on with the pen) */
static uint8_t *au_shown;                 /**< each cell's key as drawn (0xFF: not drawn yet) */
static uint16_t au_col[AUTO_KEYS];        /**< each key's colour for the rule running, RGB565 */
static lv_timer_t *au_timer;              /**< a generation each auto_step_ms() */

/**
 * @brief A colour along a list of colours.
 * @param[in] stops  the colours, 0xRRGGBB
 * @param[in] n  how many
 * @param[in] u  where, 0 (the first) .. 255 (the last)
 * @return the colour, RGB565
 */
static uint16_t along(const uint32_t *stops, int n, int u)
{
    int v = u * (n - 1), seg = v / 255;
    if (seg >= n - 1) {
        return lv_color_to_u16(lv_color_hex(stops[n - 1]));
    }
    return saver_pic_mix(stops[seg], stops[seg + 1], v - seg * 255);
}

/**
 * @brief A state's colour for the rule running.
 * @param[in] s  the state (for a Generations rule, 0: just born)
 * @return the colour, RGB565
 */
static uint16_t state_colour(int s)
{
    static const uint32_t WHEEL[] = {SV_BLUE, SV_TEAL, SV_GREEN, SV_YELLOW, SV_GOLD, SV_RED, SV_PINK, AU_PURPLE, SV_BLUE};
    static const uint32_t REFRACT[] = {SV_GOLD, SV_RED, SV_PINK, AU_PURPLE, SV_BLUE_E, 0x124A5A};
    static const uint32_t DYING[] = {SV_TEAL, SV_BLUE_D, SV_BLUE_E};
    static const uint32_t ANT[] = {SV_BLUE_E, SV_BLUE, SV_TEAL, SV_GREEN, SV_YELLOW};
    int n = auto_states();
    switch (auto_kind()) {
    case AUTO_CYCLIC:                               /* round the wheel, the last state next to the first */
        return along(WHEEL, sizeof(WHEEL) / sizeof(WHEEL[0]), s * 255 / n);
    case AUTO_EXCITABLE:
        return s == 1 ? lv_color_to_u16(lv_color_hex(SV_YELLOW))
               : along(REFRACT, sizeof(REFRACT) / sizeof(REFRACT[0]), n > 3 ? (s - 2) * 255 / (n - 3) : 0);
    case AUTO_FOREST:
        return lv_color_to_u16(lv_color_hex(s == 1 ? SV_GREEN_E : SV_YELLOW));
    case AUTO_TURMITE:
        if (s == 0) {
            return lv_color_to_u16(lv_color_hex(AU_UNLIT));
        }
        return n == 2 ? lv_color_to_u16(lv_color_hex(SV_BLUE))
               : along(ANT, sizeof(ANT) / sizeof(ANT[0]), (s - 1) * 255 / (n - 2));
    default:                                        /* Generations */
        if (s <= 1) {
            return lv_color_to_u16(lv_color_hex(s == 0 ? SV_YELLOW : SV_BLUE));   /* just born, alive */
        }
        if (auto_rule() == AUTO_R_BRAIN) {
            return lv_color_to_u16(lv_color_hex(SV_BLUE));                       /* Brian's Brain: dying blue */
        }
        return along(DYING, sizeof(DYING) / sizeof(DYING[0]), n > 3 ? (s - 2) * 255 / (n - 3) : 0);
    }
}

/** @brief The colours for the rule running: fading, each state, the ant. */
static void make_colors(void)
{
    static const uint32_t FADE[AUTO_FADE] = {AU_UNLIT, 0x0B2A33, 0x124A5A, SV_BLUE_E};
    static const uint32_t EMBER[AUTO_FADE] = {AU_UNLIT, 0x2A1210, 0x5A2018, SV_RED_E};
    for (int k = 0; k < AUTO_FADE; k++) {
        au_col[k] = lv_color_to_u16(lv_color_hex(auto_kind() == AUTO_FOREST ? EMBER[k] : FADE[k]));
    }
    for (int s = 0; s < AUTO_STATES_MAX; s++) {
        au_col[AUTO_FADE + s] = state_colour(s);
    }
    au_col[AUTO_KEY_ANT] = lv_color_to_u16(lv_color_hex(SV_YELLOW));
}

/** @brief Draws the dots whose key changed. */
static void automata_paint(void)
{
    saver_pen_begin();
    for (int y = 0; y < AUTO_N; y++) {
        for (int x = 0; x < AUTO_N; x++) {
            uint8_t k = (uint8_t)auto_key(x, y);
            uint8_t *s = &au_shown[y * AUTO_N + x];
            if (*s != k) {
                *s = k;
                saver_pen_dot(x, y, AU_PX, au_col[k]);
            }
        }
    }
    saver_pen_end();
}

/**
 * @brief A generation (or a new start), and the dots that changed drawn.
 * @param[in] t  the timer
 */
static void automata_tick(lv_timer_t *t)
{
    auto_step();
    saver_perf_mark();
    automata_paint();
}

/* (documented in ui_internal.h) */
bool saver_automata_start(lv_obj_t *parent, int rule)
{
    if (!au_shown) {
        au_shown = malloc(AUTO_CELLS);
    }
    if (!au_shown || !auto_start(esp_random(), rule) || !(au_buf = saver_pic_start(parent))) {
        saver_automata_stop();
        return false;
    }
    make_colors();
    memset(au_shown, 0xFF, AUTO_CELLS);             /* every dot to draw (the gaps stay black) */
    automata_paint();
    au_timer = saver_timer_create(automata_tick, (uint32_t)auto_step_ms());
    return true;
}

/* (documented in ui_internal.h) */
void saver_automata_stop(void)
{
    if (au_timer) {
        lv_timer_delete(au_timer);
        au_timer = NULL;
    }
    if (au_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        au_buf = NULL;
    }
    free(au_shown);
    au_shown = NULL;
    auto_free();
}
