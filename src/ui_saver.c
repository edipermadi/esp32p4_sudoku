/**
 * @file ui_saver.c
 * @ingroup ui_sudoku
 * @brief The screen saver: after the screen timeout without a touch the game pauses, and then what was
 *        chosen shows at full speed: a dim clock (digital, or analog: ui_saver_analog.c) moving about,
 *        digital rain (ui_saver_rain.c), a strange attractor (ui_saver_attractor.c), a flock of boids
 *        (ui_saver_boids.c), a Lissajous figure (ui_saver_lissajous.c), double pendulums
 *        (ui_saver_pendulum.c), a starfield flown through (ui_saver_stars.c), a zoom into the Mandelbrot set
 *        (ui_saver_mandel.c), liquids stirred and mixing (ui_saver_fluid.c), raindrops
 *        rippling a pool (ui_saver_ripple.c), two galaxies colliding (ui_saver_galaxy.c), a cellular automaton
 *        (ui_saver_automata.c: Game of Life, Brian's Brain, Langton's ant and eight more), or a 3D
 *        cellular automaton in a turning cube (ui_saver_automata3d.c: Crystal growth, Diamond growth,
 *        Pyroclastic, Builder 1). After "Screen saver for" minutes (or at once, for Dark) the screen goes
 *        black and the board saves power. The next tap only wakes it. The lock screen, if on, waits
 *        under it.
 */
#include "ui_internal.h"
#include "saver_automata_core.h"
#include "saver_automata3d_core.h"

static void saver_sleep(bool lock_now, int mode);
static void saver_show_stop(void);

static lv_obj_t *s_saver;                 /**< the black cover on the system layer while the screen is off */
static lv_obj_t *sv_clock;                /**< the dim clock (NULL: none, or dark already) */
static lv_obj_t *sv_time;                 /**< its time */
static lv_obj_t *sv_date;                 /**< its date */
static lv_timer_t *sv_timer;              /**< the clock, each second */
static uint32_t sv_moved;                 /**< when it last moved */
static lv_timer_t *sv_dark_timer;         /**< each second while a saver shows: is it time for dark? */
static uint32_t sv_since;                 /**< when the saver came up */
static int sv_shown = -1;                 /**< the saver showing (store_saver_t), -1: none or dark */

/**
 * @brief Each second: the clock's time and date, somewhere else each minute (no burn-in).
 * @param[in] t  the timer (NULL: the first call)
 */
static void saver_clock_tick(lv_timer_t *t)
{
    if (!s_saver || !sv_clock) {
        return;
    }
    time_t now = time(NULL);
    char hm[8] = "--:--", date[48] = "";
    if (CLOCK_VALID(now)) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(hm, sizeof(hm), "%H:%M", &tm);
        strftime(date, sizeof(date), "%a %d %b", &tm);
    }
    lv_label_set_text(sv_time, hm);
    lv_label_set_text(sv_date, date);
    if (!t || lv_tick_elaps(sv_moved) >= 60000) {  /* somewhere else each minute */
        sv_moved = lv_tick_get();
        lv_obj_update_layout(sv_clock);
        int32_t w = lv_obj_get_width(sv_clock), h = lv_obj_get_height(sv_clock);
        lv_obj_set_pos(sv_clock, 20 + (int32_t)(esp_random() % (uint32_t)LV_MAX(1, SCR_W - 40 - w)),
                       20 + (int32_t)(esp_random() % (uint32_t)LV_MAX(1, SCR_W - 40 - h)));
    }
}

/** @brief The dim digital clock starts on the cover. */
static void saver_clock_start(void)
{
    sv_clock = lv_obj_create(s_saver);
    lv_obj_remove_style_all(sv_clock);
    lv_obj_set_size(sv_clock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sv_clock, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sv_clock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_clickable(sv_clock, false);
    sv_time = make_label(sv_clock, &font_timer_36, lv_color_hex(0x9AA0AA));   /* (dim by its grey text) */
    sv_date = make_label(sv_clock, &lv_font_montserrat_20, lv_color_hex(0x6B7280));
    saver_clock_tick(NULL);
    sv_timer = lv_timer_create(saver_clock_tick, 1000, NULL);
}

/**
 * @brief The chosen saver starts on the cover.
 * @param[in] mode  store_saver_t
 * @return false for Dark, or if it couldn't start (no memory): dark then
 */
static bool saver_show_start(uint8_t mode)
{
    switch (mode) {
    case SAVER_MODE_CLOCK:
        saver_clock_start();
        return true;
    case SAVER_MODE_ANALOG:
        saver_analog_start(s_saver);
        return true;
    case SAVER_MODE_LIFE:
        return saver_automata_start(s_saver, AUTO_R_LIFE);
    case SAVER_MODE_RAIN:
        return saver_rain_start(s_saver);
    case SAVER_MODE_ATTRACTOR:
        return saver_attractor_start(s_saver);
    case SAVER_MODE_BOIDS:
        return saver_boids_start(s_saver);
    case SAVER_MODE_LISSAJOUS:
        return saver_lissajous_start(s_saver);
    case SAVER_MODE_PENDULUM:
        return saver_pendulum_start(s_saver);
    case SAVER_MODE_STARS:
        return saver_stars_start(s_saver);
    case SAVER_MODE_MANDEL:
        return saver_mandel_start(s_saver);
    case SAVER_MODE_FLUID:
        return saver_fluid_start(s_saver);
    case SAVER_MODE_RIPPLE:
        return saver_ripple_start(s_saver);
    case SAVER_MODE_GALAXY:
        return saver_galaxy_start(s_saver);
    case SAVER_MODE_ANT:
        return saver_automata_start(s_saver, AUTO_R_ANT);
    case SAVER_MODE_BRAIN:
        return saver_automata_start(s_saver, AUTO_R_BRAIN);
    case SAVER_MODE_CYCLIC:
        return saver_automata_start(s_saver, AUTO_R_CYCLIC);
    case SAVER_MODE_RPS:
        return saver_automata_start(s_saver, AUTO_R_RPS);
    case SAVER_MODE_EXCITABLE:
        return saver_automata_start(s_saver, AUTO_R_EXCITABLE);
    case SAVER_MODE_DAY_NIGHT:
        return saver_automata_start(s_saver, AUTO_R_DAY_NIGHT);
    case SAVER_MODE_SEEDS:
        return saver_automata_start(s_saver, AUTO_R_SEEDS);
    case SAVER_MODE_MAZE:
        return saver_automata_start(s_saver, AUTO_R_MAZE);
    case SAVER_MODE_STAR_WARS:
        return saver_automata_start(s_saver, AUTO_R_STAR_WARS);
    case SAVER_MODE_FOREST:
        return saver_automata_start(s_saver, AUTO_R_FOREST);
    case SAVER_MODE_3D_CRYSTAL:
        return saver_automata3d_start(s_saver, A3_R_CRYSTAL);
    case SAVER_MODE_3D_DIAMOND:
        return saver_automata3d_start(s_saver, A3_R_DIAMOND);
    case SAVER_MODE_3D_PYRO:
        return saver_automata3d_start(s_saver, A3_R_PYRO);
    case SAVER_MODE_3D_BUILDER:
        return saver_automata3d_start(s_saver, A3_R_BUILDER);
    default:
        return false;
    }
}

/** @brief Stops whichever saver shows (its pictures go now, before their memory; its objects too). */
static void saver_show_stop(void)
{
    if (sv_timer) {
        lv_timer_delete(sv_timer);
    }
    sv_timer = NULL;
    if (sv_dark_timer) {
        lv_timer_delete(sv_dark_timer);
    }
    sv_dark_timer = NULL;
    sv_clock = NULL;
    sv_shown = -1;
    saver_analog_stop();
    saver_rain_stop();
    saver_attractor_stop();
    saver_boids_stop();
    saver_lissajous_stop();
    saver_pendulum_stop();
    saver_stars_stop();
    saver_mandel_stop();
    saver_fluid_stop();
    saver_ripple_stop();
    saver_galaxy_stop();
    saver_automata_stop();
    saver_automata3d_stop();
    if (s_saver) {
        lv_obj_clean(s_saver);                      /* (the clocks' objects: the cover stays, black) */
    }
}

/**
 * @brief Each second while a saver shows: after "Screen saver for" minutes, the saver goes and the
 *        screen goes dark, saving power.
 * @param[in] t  the timer
 */
static void saver_dark_tick(lv_timer_t *t)
{
    uint8_t min = store_saver_minutes();
    if (!s_saver || !min || lv_tick_elaps(sv_since) < min * 60u * 1000u) {
        return;
    }
    saver_show_stop();                              /* (this timer too) */
    lv_refr_now(NULL);                              /* panel shows black before the light goes off */
    bsp_power_save(true);                           /* light off, panel asleep, CPU slower */
}

/**
 * @brief The waking tap: the light back on the press; the cover goes once the finger is up, so the
 *        tap reaches nothing below.
 * @param[in] e  the press, release or press-lost event
 */
static void saver_wake_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        saver_show_stop();
        bsp_power_save(false);                      /* CPU and panel back (if it was dark) */
        bsp_display_brightness_set(effective_brightness());
    } else if (s_saver) {
        /* removed only once the finger is up, so the waking tap reaches nothing below */
        lv_obj_delete_async(s_saver);
        s_saver = NULL;
    }
}

/**
 * @brief A saver on the cover at full speed (for "Screen saver for" minutes); or, for Dark (or if it
 *        couldn't start), dark at once, saving power.
 * @param[in] mode  store_saver_t
 */
static void saver_show(uint8_t mode)
{
    if (saver_show_start(mode)) {
        sv_shown = mode;
        lv_refr_now(NULL);
        bsp_display_brightness_set(effective_brightness());   /* (low PWM levels show nothing on this panel) */
        sv_since = lv_tick_get();
        sv_dark_timer = lv_timer_create(saver_dark_tick, 1000, NULL);
        return;
    }
    lv_refr_now(NULL);                  /* panel shows black before the light goes off */
    bsp_power_save(true);               /* light off, panel asleep, CPU slower */
}

/**
 * @brief The screen off: the game paused, the lock screen (if on) under a black cover, then a saver at
 *        full speed (for "Screen saver for" minutes); or dark at once, saving power.
 * @param[in] lock_now  the lock button on Home: locked even with the lock Off, and dark at once
 * @param[in] mode  store_saver_t to show, or -1 for the one chosen in Settings
 */
static void saver_sleep(bool lock_now, int mode)
{
    if (lv_screen_active() == screen_game() && game_id() != STORE_NO_GAME && !is_game_solved() && !is_game_paused()) {
        set_paused(true);   /* stops the clock, hides the board, saves the game */
    }
    ui_lock_show(lock_now);                /* (if on) under the black cover: what the waking tap shows */
    s_saver = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_saver);
    lv_obj_set_size(s_saver, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_saver, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_saver, LV_OPA_COVER, 0);
    lv_obj_set_clickable(s_saver, true);
    lv_obj_add_event_cb(s_saver, saver_wake_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_saver, saver_wake_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_saver, saver_wake_cb, LV_EVENT_PRESS_LOST, NULL);
    saver_show(lock_now ? SAVER_MODE_DARK : mode >= 0 ? (uint8_t)mode : store_saver_mode());
}

/* (documented in ui_sudoku.h) */
void sudoku_ui_screen_off(void)
{
    if (!s_saver) {
        saver_sleep(false, -1);
    }
}

/* (documented in ui_sudoku.h) */
bool sudoku_ui_screen_saver(int mode)
{
    if (mode < 0 || mode >= SAVER_MODE_COUNT) {
        return false;
    }
    if (!s_saver) {
        saver_sleep(false, mode);
        return true;
    }
    saver_show_stop();                  /* one showing (or dark already): this one instead */
    bsp_power_save(false);
    lv_obj_invalidate(s_saver);
    saver_show((uint8_t)mode);
    return true;
}

/**
 * @brief The lock button on Home: the screen off now, and locked when it wakes.
 * @param[in] e  the event
 */
void lock_now_cb(lv_event_t *e)
{
    if (!s_saver) {
        saver_sleep(true, -1);
    }
}

/* (documented in ui_internal.h: the screen off once it's been untouched for the screen timeout) */
void saver_cb(lv_timer_t *t)
{
    if (!s_saver && ui_sleep_min() &&
            lv_display_get_inactive_time(NULL) >= (uint32_t)ui_sleep_min() * 60 * 1000) {
        saver_sleep(false, -1);
    }
}

/* ---- accessors: the other UI files reach this file's state through these */
/** @brief The screen saver's cover. @return it (NULL: the screen is on) */
lv_obj_t *ui_saver(void) { return s_saver; }
/* (documented in ui_internal.h) */
int ui_saver_shown(void) { return sv_shown; }
