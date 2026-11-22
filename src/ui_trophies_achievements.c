/**
 * @file ui_trophies_achievements.c
 * @ingroup ui_trophies
 * @brief Trophies, the Achievements tab: unlocked ones first (a trophy and the date), then the others (a lock and their progress).
 */
#include "ui_internal.h"
#include "ui_trophies_tab.h"
#include "trophies_core.h"

static lv_obj_t *tr_ach_count;             /**< "5 of 35 unlocked" */
static lv_obj_t *tr_ach_list;              /**< the cards */

/**
 * @brief An achievement's card: a trophy and the date it was unlocked, or a lock and its progress
 *        (a bar where there is a count to show).
 * @param[in] a  the achievement
 */
static void ach_card(const stats_ach_t *a)
{
    lv_obj_t *card = tr_card(tr_ach_list, 80);
    lv_obj_t *icon = make_label(card, &font_icons_24, a->unlocked ? C_ORANGE : C_MUTED);
    lv_label_set_text(icon, a->unlocked ? SYMBOL_TROPHY : SYMBOL_LOCK);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *t = make_label(card, &lv_font_montserrat_20, a->unlocked ? C_TEXT : C_MUTED);
    lv_label_set_text(t, a->title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 44, -13);
    lv_obj_t *d = make_label(card, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(d, SCR_W - 24 - 32 - 44 - 190);
    lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(d, a->desc);
    lv_obj_align(d, LV_ALIGN_LEFT_MID, 44, 14);

    lv_obj_t *r = make_label(card, &lv_font_montserrat_16, a->unlocked ? C_GREEN : C_MUTED);
    lv_obj_set_width(r, 176);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_RIGHT, 0);
    if (a->unlocked) {
        char when[24] = "Unlocked";
        if (a->unlocked_at) {
            time_t tt = (time_t)a->unlocked_at;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "%d %b %Y", &tm);
        }
        lv_label_set_text(r, when);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
    } else if (a->goal > 1) {
        char prog[32];
        trophies_ach_progress_text(a, prog, sizeof(prog));
        lv_label_set_text(r, prog);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, -12);
        lv_obj_t *bar = lv_bar_create(card);
        lv_obj_set_size(bar, 150, 10);
        lv_bar_set_range(bar, 0, a->goal);
        lv_bar_set_value(bar, a->value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, C_CARD2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, C_ACCENT, LV_PART_INDICATOR);
        lv_obj_align(bar, LV_ALIGN_RIGHT_MID, 0, 14);
    } else {
        lv_label_set_text(r, "Not yet");
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

/* (documented in ui_trophies_tab.h) */
void tr_ach_render(const stats_achievements_t *a)
{
    lv_label_set_text_fmt(tr_ach_count, "%d of %d unlocked", a->unlocked, a->n);
    lv_obj_clean(tr_ach_list);
    for (int pass = 0; pass < 2; pass++) {             /* unlocked first, then the rest */
        for (int i = 0; i < a->n; i++) {
            if (a->a[i].unlocked == (pass == 0)) {
                ach_card(&a->a[i]);
            }
        }
    }
    lv_obj_scroll_to_y(tr_ach_list, 0, LV_ANIM_OFF);
}

/* (documented in ui_trophies_tab.h) */
lv_obj_t *tr_ach_build(lv_obj_t *scr)
{
    lv_obj_t *av = lv_obj_create(scr);
    lv_obj_remove_style_all(av);
    lv_obj_set_pos(av, 12, TR_VIEW_Y);
    lv_obj_set_size(av, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_ach_count = make_label(av, &lv_font_montserrat_18, C_MUTED);
    lv_obj_set_pos(tr_ach_count, 4, 4);
    tr_ach_list = lv_obj_create(av);
    lv_obj_remove_style_all(tr_ach_list);
    lv_obj_set_pos(tr_ach_list, 0, 36);
    lv_obj_set_size(tr_ach_list, SCR_W - 24, SCR_W - TR_VIEW_Y - 12 - 36);
    lv_obj_set_flex_flow(tr_ach_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr_ach_list, 8, 0);
    lv_obj_set_scroll_dir(tr_ach_list, LV_DIR_VER);
    lv_label_set_text(tr_ach_count, "");
    tr_hint(tr_ach_list, "Loading...");
    return av;
}

/* (documented in ui_trophies_tab.h) */
void tr_ach_ask(void)
{
    stats_db_query_achievements();
}

/* (documented in ui_trophies_tab.h) */
void tr_ach_no_db(void)
{
    lv_label_set_text(tr_ach_count, "");
    tr_hint(tr_ach_list, "Achievements are worked out from the solves in sudoku.db: insert a FAT32 microSD "
            "card before switching on.");
}
