/**
 * @file ui_trophies_records.c
 * @ingroup ui_trophies
 * @brief Trophies, the Records tab: your personal bests, each with the puzzle and date it was set on.
 */
#include "ui_internal.h"
#include "ui_trophies_tab.h"
#include "trophies_core.h"

static lv_obj_t *tr_rec_list;              /**< the records' cards */

/* (documented in ui_trophies_tab.h) */
void tr_records_render(const stats_records_t *R)
{
    lv_obj_clean(tr_rec_list);
    for (int i = 0; i < R->n; i++) {
        const stats_record_t *r = &R->r[i];
        lv_obj_t *card = tr_card(tr_rec_list, 72);
        lv_obj_t *t = make_label(card, &lv_font_montserrat_18, r->has ? C_TEXT : C_MUTED);
        lv_label_set_text(t, r->title);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, -12);
        /* what it was set on, and when */
        char detail[96] = "", when[24] = "";
        if (r->when) {
            time_t tt = (time_t)r->when;
            struct tm tm;
            localtime_r(&tt, &tm);
            strftime(when, sizeof(when), "%d %b %Y", &tm);
        }
        if (!r->has) {
            snprintf(detail, sizeof(detail), "Not yet");
        } else if (r->puzzle != STORE_NO_GAME) {
            char name[40];
            puzzle_name(r->puzzle, name, sizeof(name));
            snprintf(detail, sizeof(detail), "%s%s%s", name, when[0] ? "   -   " : "", when);
        } else if (when[0]) {
            snprintf(detail, sizeof(detail), "%s", when);
        }
        lv_obj_t *d = make_label(card, &lv_font_montserrat_14, C_MUTED);
        lv_obj_set_width(d, SCR_W - 24 - 32 - 190);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(d, detail);
        lv_obj_align(d, LV_ALIGN_LEFT_MID, 0, 13);
        char v[24] = "-";
        if (r->has) {
            switch (r->unit) {
            case REC_TIME:
                fmt_time(v, sizeof(v), (uint32_t)r->value);
                break;
            case REC_DAYS:
                snprintf(v, sizeof(v), "%ld day%s", (long)r->value, r->value == 1 ? "" : "s");
                break;
            case REC_MINUTES:
                if (r->value >= 60) {
                    snprintf(v, sizeof(v), "%ld h %02ld min", (long)(r->value / 60), (long)(r->value % 60));
                } else {
                    snprintf(v, sizeof(v), "%ld min", (long)r->value);
                }
                break;
            default:
                snprintf(v, sizeof(v), "%ld", (long)r->value);
                break;
            }
        }
        lv_obj_t *vl = make_label(card, r->unit == REC_TIME && r->has ? &font_timer_36 : &lv_font_montserrat_24,
                                  r->has ? (i == 0 ? C_ORANGE : C_TEXT) : C_MUTED);
        lv_label_set_text(vl, v);
        lv_obj_align(vl, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    lv_obj_scroll_to_y(tr_rec_list, 0, LV_ANIM_OFF);
}

/* (documented in ui_trophies_tab.h) */
lv_obj_t *tr_records_build(lv_obj_t *scr)
{
    lv_obj_t *rv = lv_obj_create(scr);
    lv_obj_remove_style_all(rv);
    lv_obj_set_pos(rv, 12, TR_VIEW_Y);
    lv_obj_set_size(rv, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    tr_rec_list = lv_obj_create(rv);
    lv_obj_remove_style_all(tr_rec_list);
    lv_obj_set_size(tr_rec_list, SCR_W - 24, SCR_W - TR_VIEW_Y - 12);
    lv_obj_set_flex_flow(tr_rec_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr_rec_list, 8, 0);
    lv_obj_set_scroll_dir(tr_rec_list, LV_DIR_VER);
    tr_hint(tr_rec_list, "Loading...");
    return rv;
}

/* (documented in ui_trophies_tab.h) */
void tr_records_ask(void)
{
    stats_db_query_records();
}

/* (documented in ui_trophies_tab.h) */
void tr_records_no_db(void)
{
    tr_hint(tr_rec_list, "Records are worked out from the solves in sudoku.db: insert a FAT32 microSD card "
            "before switching on.");
}
