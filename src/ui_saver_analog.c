/**
 * @file ui_saver_analog.c
 * @ingroup ui_sudoku
 * @brief The analog clock screen saver: a round face in dim greys on black (ticks, 1 to 12, the hour,
 *        minute and second hands, the date), at a new place every minute so nothing burns in.
 *
 * The face is an LVGL scale over a range of 3600 (the seconds in an hour), so every hand moves
 * smoothly: the second hand a 60th of the way round each second, the minute hand each second too, the
 * hour hand each minute.
 */
#include "ui_internal.h"

#define AN_SIZE         440                     /**< the face's diameter, px */
#define AN_INSET        17                      /**< the ticks' gap to the rim, px */
#define AN_RANGE        3600                    /**< the scale's range: seconds in an hour */
#define AN_C_RING       0x2A2E35                /**< the face's rim */
#define AN_C_TICK       0x5A606B                /**< the ticks */
#define AN_C_TEXT       0x7A808B                /**< the numbers */
#define AN_C_HAND       0x9AA0AA                /**< the hour and minute hands */
#define AN_C_SEC        0x8A4A3A                /**< the second hand: a dim red */
#define AN_C_DATE       0x6B7280                /**< the date */

static lv_obj_t *an_box;                  /**< the clock: face, hands, date (moves each minute) */
static lv_obj_t *an_scale;                /**< the face */
static lv_obj_t *an_hour;                 /**< the hour hand */
static lv_obj_t *an_min;                  /**< the minute hand */
static lv_obj_t *an_sec;                  /**< the second hand */
static lv_obj_t *an_date;                 /**< the date */
static lv_timer_t *an_timer;              /**< each second */
static uint32_t an_moved;                 /**< when it last moved */

/**
 * @brief A hand: a line with round ends, a child of the face.
 * @param[in] width  its width, px
 * @param[in] color  its colour
 * @return the line
 */
static lv_obj_t *an_hand(int32_t width, uint32_t color)
{
    lv_obj_t *l = lv_line_create(an_scale);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

/**
 * @brief Each second: the hands and the date, and somewhere else on the screen each minute.
 * @param[in] t  the timer (NULL: the first call)
 */
static void an_tick(lv_timer_t *t)
{
    if (!an_box) {
        return;
    }
    time_t now = time(NULL);
    struct tm tm = {0};
    char date[32] = "";
    if (CLOCK_VALID(now)) {
        localtime_r(&now, &tm);
        strftime(date, sizeof(date), "%a %d %b", &tm);
    }
    int32_t s = tm.tm_sec, m = tm.tm_min * 60 + s, h = ((tm.tm_hour % 12) * 60 + tm.tm_min) * 5;
    lv_scale_set_line_needle_value(an_scale, an_hour, AN_SIZE / 2 - 130, h);
    lv_scale_set_line_needle_value(an_scale, an_min, AN_SIZE / 2 - 70, m);
    lv_scale_set_line_needle_value(an_scale, an_sec, AN_SIZE / 2 - 50, s * 60);
    lv_label_set_text(an_date, date);
    if (!t || lv_tick_elaps(an_moved) >= 60000) {  /* somewhere else each minute */
        an_moved = lv_tick_get();
        uint32_t room = (uint32_t)LV_MAX(1, SCR_W - 40 - AN_SIZE);
        lv_obj_set_pos(an_box, 20 + (int32_t)(esp_random() % room), 20 + (int32_t)(esp_random() % room));
    }
}

/* (documented in ui_internal.h) */
void saver_analog_start(lv_obj_t *parent)
{
    static const char *const NUMS[] = {"12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", NULL};
    an_box = lv_obj_create(parent);
    lv_obj_remove_style_all(an_box);
    lv_obj_set_size(an_box, AN_SIZE, AN_SIZE);
    lv_obj_set_clickable(an_box, false);
    lv_obj_set_style_radius(an_box, LV_RADIUS_CIRCLE, 0);          /* the rim */
    lv_obj_set_style_border_width(an_box, 3, 0);
    lv_obj_set_style_border_color(an_box, lv_color_hex(AN_C_RING), 0);

    /* (no padding or border on the scale itself: its needles are placed from its content's corner) */
    an_scale = lv_scale_create(an_box);
    lv_obj_set_size(an_scale, AN_SIZE - 2 * AN_INSET, AN_SIZE - 2 * AN_INSET);
    lv_obj_center(an_scale);
    lv_obj_set_clickable(an_scale, false);
    lv_scale_set_mode(an_scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(an_scale, 0, AN_RANGE);
    lv_scale_set_angle_range(an_scale, 360);
    lv_scale_set_rotation(an_scale, 270);           /* 0 at the top */
    lv_scale_set_total_tick_count(an_scale, 61);    /* a tick a minute */
    lv_scale_set_major_tick_every(an_scale, 5);     /* a bigger one each hour, with its number */
    lv_scale_set_label_show(an_scale, true);
    lv_scale_set_text_src(an_scale, (const char **)NUMS);
    lv_obj_set_style_border_width(an_scale, 0, 0);
    lv_obj_set_style_bg_opa(an_scale, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(an_scale, 0, 0);
    lv_obj_set_style_arc_width(an_scale, 0, LV_PART_MAIN);         /* no arc along the ticks */
    lv_obj_set_style_length(an_scale, 8, LV_PART_ITEMS);           /* minor ticks */
    lv_obj_set_style_line_width(an_scale, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(an_scale, lv_color_hex(AN_C_TICK), LV_PART_ITEMS);
    lv_obj_set_style_length(an_scale, 18, LV_PART_INDICATOR);      /* hour ticks */
    lv_obj_set_style_line_width(an_scale, 4, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(an_scale, lv_color_hex(AN_C_TICK), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(an_scale, lv_color_hex(AN_C_TEXT), LV_PART_INDICATOR);
    lv_obj_set_style_text_font(an_scale, &lv_font_montserrat_28, LV_PART_INDICATOR);
    lv_obj_set_style_pad_radial(an_scale, 12, LV_PART_INDICATOR);  /* the numbers inside the ticks */

    an_date = make_label(an_box, &lv_font_montserrat_20, lv_color_hex(AN_C_DATE));
    lv_obj_align(an_date, LV_ALIGN_CENTER, 0, 90);

    an_hour = an_hand(10, AN_C_HAND);
    an_min = an_hand(6, AN_C_HAND);
    an_sec = an_hand(2, AN_C_SEC);
    lv_obj_t *cap = lv_obj_create(an_box);         /* the pin the hands turn on */
    lv_obj_remove_style_all(cap);
    lv_obj_set_size(cap, 16, 16);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cap, lv_color_hex(AN_C_HAND), 0);
    lv_obj_center(cap);

    an_tick(NULL);
    an_timer = lv_timer_create(an_tick, 1000, NULL);
}

/* (documented in ui_internal.h) */
void saver_analog_stop(void)
{
    if (an_timer) {
        lv_timer_delete(an_timer);
        an_timer = NULL;
    }
    an_box = NULL;                                  /* (a child of the cover: goes with it) */
}
