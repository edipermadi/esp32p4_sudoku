/* sudoku_ui.c split: time attack, the rules card, Share (QR code), Send by sound, save to a collection. See sudoku_ui.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void atk_start(void);
static void atk_board_fill(lv_obj_t *c);
static void atk_go_cb(lv_event_t *e);
static void atk_home_cb(lv_event_t *e);
static void rules_pic_draw_cb(lv_event_t *e);
static void rules_show(int variant);
static bool share_text(char *buf, size_t n);
static void qr_draw_cb(lv_event_t *e);
static uint16_t pz_crc16(const char *p, size_t n);
static int pz_line(char *buf, size_t n);
static void pz_send_poll(lv_timer_t *t);
static void pz_send_cb(lv_event_t *e);
static void pz_check_fn(void *arg);
static void pz_play_cb(lv_event_t *e);
static void pz_check_poll(lv_timer_t *t);
static bool pz_take(char *line, char *why, size_t n);
static void sv_close_cb(lv_event_t *e);
static void sv_toggle_cb(lv_event_t *e);
static void sv_new_done(const char *text);
static void sv_new_cb(lv_event_t *e);
static void sv_open(void);

static uint32_t atk_used_ms;      /* the run's earlier puzzles */
static int      atk_solved, atk_mistakes;
static uint16_t atk_last;
static int s_nsaved;

/* ------------------------------------------------------------- time attack */

void atk_open_next(void)
{
    if (!game_attack()) {
        return;    /* stopped meanwhile */
    }
    const puzzle_collection_t *pc = puzzle_collection(0);   /* Easy */
    uint16_t id;
    int tries = 0;
    do {
        id = (uint16_t)(pc->first + esp_random() % pc->count);
    } while (++tries < 20 && (id == atk_last || id == store_saved_game()->id));   /* (not the Continue game) */
    atk_last = id;
    uint32_t used = atk_used_ms;
    start_game(id, false);
    game_set_attack(true);
    atk_used_ms = used;
    game_set_flags(0);                                    /* (no challenges in a run) */
    challenge_ui();
    update_header();
}

static void atk_start(void)
{
    atk_used_ms = 0;
    atk_solved = atk_mistakes = 0;
    atk_last = STORE_NO_GAME;
    game_set_attack(true);
    atk_open_next();
}

static void atk_board_fill(lv_obj_t *c)
{
    stats_attack_row_t rows[STATS_ATTACK_ROWS];
    int n = stats_db_attack_board(rows, STATS_ATTACK_ROWS);
    if (n <= 0) {
        make_wrap_text(c, &lv_font_montserrat_18, C_MUTED, n < 0 ? "The leaderboard needs sudoku.db on a microSD card."
                       : "No runs yet: be the first on the leaderboard.");
        return;
    }
    lv_obj_t *hd = make_label(c, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text(hd, "Leaderboard: best run");
    for (int i = 0; i < n; i++) {
        lv_obj_t *r = lv_obj_create(c);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_PCT(100), 36);
        lv_color_t col = rows[i].me ? C_ACCENT : C_TEXT;
        lv_obj_t *l = make_label(r, &lv_font_montserrat_18, col);
        lv_label_set_text_fmt(l, "%d.", i + 1);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_align(player_badge(r, player_find(0, rows[i].name), -1, 28, 10), LV_ALIGN_LEFT_MID, 28, 0);
        lv_obj_t *nm = make_label(r, &lv_font_montserrat_18, col);
        lv_label_set_text(nm, rows[i].name);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 64, 0);
        lv_obj_t *v = make_label(r, &lv_font_montserrat_18, col);
        lv_label_set_text_fmt(v, "%d solved   (%d run%s)", rows[i].best, rows[i].runs, rows[i].runs == 1 ? "" : "s");
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void atk_go_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    atk_start();
}

static void atk_home_cb(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
    show_home();
}

/* Home > Time attack: the rules, the leaderboard, Start */
void attack_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox(LV_SYMBOL_CHARGE "  Time attack", NULL);
    lv_obj_set_width(m, 600);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    lv_obj_t *x = make_wrap_text(c, &lv_font_montserrat_18, C_TEXT, "Solve as many Easy puzzles as you can in 10 "
                                 "minutes: the next one opens as soon as one is solved. Hint and Fill are off; pausing "
                                 "stops the clock and hides the board. Solves count as usual.");
    lv_obj_set_style_margin_bottom(x, 10, 0);
    atk_board_fill(c);
    msgbox_button(m, "Close", mbox_close_cb, false);
    msgbox_button(m, "Start", atk_go_cb, true);
}

/* the 10 minutes are up: the run's result, recorded, and the leaderboard */
void atk_finish(void)
{
    game_set_solved(true);                                /* (g_attack stays: the header and 0:00 stay too) */                                /* (the board can't be changed now) */
    hint_drop();
    stats_db_record_attack(atk_solved, atk_mistakes);
    sfx_play(SFX_SOLVED);
    char t[96];
    snprintf(t, sizeof(t), "Time's up! You solved %d Easy puzzle%s, with %d mistake%s.", atk_solved,
             atk_solved == 1 ? "" : "s", atk_mistakes, atk_mistakes == 1 ? "" : "s");
    lv_obj_t *m = make_msgbox(LV_SYMBOL_CHARGE "  Time attack", NULL);
    lv_obj_set_width(m, 600);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    lv_obj_t *x = make_wrap_text(c, &lv_font_montserrat_20, C_TEXT, t);
    lv_obj_set_style_margin_bottom(x, 10, 0);
    atk_board_fill(c);                              /* (the run above is queued first: it's in) */
    msgbox_button(m, "Home", atk_home_cb, false);
    msgbox_button(m, "Again", atk_go_cb, true);
    update_header();
}

/* cell size: a whole 9x9 board (Asterisk) smaller, so the dialog fits */
#define RULES_PIC_CS(p) ((p)->h > 6 ? 28 : 40)

static const uint8_t PIC_KROPKI[] = {0, MARK_WHITE, 1, MARK_BLACK, 0xFF};   /* 3 4 8 2 */
static const uint8_t PIC_XV[] = {0, MARK_V, 1, MARK_X, 0xFF};               /* 2 3 7 9 */
static const uint8_t PIC_GT[] = {0, MARK_LESS, 1, MARK_MORE, 2, MARK_LESS, 0xFF};   /* 2 < 5 > 4 < 9 */
static const uint8_t PIC_BARS[] = {0, 1, 0xFF};    /* 4|5: a bar; 5 3 and 3 7: none */
static const uint8_t PIC_THERMO[] = {0, 1, 2, 3, 0xFF};
static const uint8_t PIC_ARROW[] = {0, 1, 2, 0xFF};         /* 7: 4 + 3 */
static const uint8_t PIC_RENBAN[] = {0, 1, 2, 3, 0xFF};     /* 5 3 6 4 */
static const uint8_t PIC_WHISPER[] = {0, 1, 2, 3, 0xFF};    /* 2 8 1 6 */
static const uint8_t PIC_CAGES[] = {11, 0, 1, 0xFE, 8, 2, 3, 6, 0xFE, 0xFF};   /* 9 + 2 = 11, and an empty 8 */

static const rules_pic_t RULES_PIC[VAR_COUNT] = {
    /* w, h, box w, box h, digits, tint, regions, parity, cross, bars, thermo, cages, sandwich clue */
    [VAR_CLASSIC] = {3, 3, 3, 3, "591763842", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_X] = {
        5, 5, 0, 0, "1...." ".2..." "..3.." "...4." "....5", "1000101010001000101010001", NULL, NULL, NULL, NULL,
        NULL, NULL, -1
    },
    [VAR_MINI] = {6, 4, 3, 2, "123456" "456123" "......" "......", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_HYPER] = {
        5, 5, 0, 0, "....." ".123." ".456." ".789." ".....", "0000001110011100111000000", NULL, NULL, NULL, NULL,
        NULL, NULL, -1
    },
    [VAR_JIGSAW] = {
        5, 5, 0, 0, "....." "....." "....." "....." ".....", NULL, "aabbb" "aabbc" "adccc" "ddcee" "dddee",
        NULL, NULL, NULL, NULL, NULL, -1
    },
    [VAR_KILLER] = {4, 2, 0, 0, "92.." "....", NULL, NULL, NULL, NULL, NULL, NULL, PIC_CAGES, -1},
    [VAR_KNIGHT] = {
        5, 5, 0, 0, "....." "....." "..5.." "....." ".....", NULL, NULL, NULL,
        ".x.x." "x...x" "....." "x...x" ".x.x.", NULL, NULL, NULL, -1
    },
    [VAR_EVENODD] = {4, 1, 0, 0, "4.7.", NULL, NULL, "e.o.", NULL, NULL, NULL, NULL, -1},
    [VAR_CONSEC] = {4, 1, 0, 0, "4537", NULL, NULL, NULL, NULL, PIC_BARS, NULL, NULL, -1},
    [VAR_THERMO] = {4, 1, 0, 0, "2458", NULL, NULL, NULL, NULL, NULL, PIC_THERMO, NULL, -1},
    [VAR_SANDWICH] = {6, 1, 0, 0, "614329", NULL, NULL, NULL, NULL, NULL, NULL, NULL, 9},
    [VAR_KIDS] = {4, 4, 2, 2, "1234" "3412" "2143" "4321", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_KING] = {3, 3, 0, 0, "..." ".5." "...", NULL, NULL, NULL, "xxx" "x.x" "xxx", NULL, NULL, NULL, -1},
    [VAR_NONCONSEC] = {4, 1, 0, 0, "4627", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1},
    [VAR_DISJOINT] = {
        6, 3, 3, 3, "......" ".7..2." "......", "000000" "010010" "000000", NULL, NULL, NULL, NULL,
        NULL, NULL, -1
    },
    [VAR_ASTERISK] = {
        9, 9, 3, 3, "........." "....1...." "..2...3.." "........." ".4..5..6." "........."
        "..7...8.." "....9...." ".........",
        "000000000" "000010000" "001000100" "000000000" "010010010" "000000000" "001000100"
        "000010000" "000000000", NULL, NULL, NULL, NULL, NULL, NULL, -1
    },
    [VAR_KROPKI] = {4, 1, 0, 0, "3482", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, EDGE_KROPKI, PIC_KROPKI},
    [VAR_XV] = {4, 1, 0, 0, "2379", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, EDGE_XV, PIC_XV},
    [VAR_GT] = {4, 1, 0, 0, "2549", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, EDGE_GT, PIC_GT},
    [VAR_QUAD] = {2, 2, 0, 0, "16" "93", NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1, 0, NULL, "139"},
    [VAR_ARROW] = {3, 1, 0, 0, "743", NULL, NULL, NULL, NULL, NULL, PIC_ARROW, NULL, -1, 0, NULL, NULL, LINE_ARROW},
    [VAR_RENBAN] = {4, 1, 0, 0, "5364", NULL, NULL, NULL, NULL, NULL, PIC_RENBAN, NULL, -1, 0, NULL, NULL, LINE_RENBAN},
    [VAR_WHISPER] = {
        4, 1, 0, 0, "2816", NULL, NULL, NULL, NULL, NULL, PIC_WHISPER, NULL, -1, 0, NULL, NULL,
        LINE_WHISPER
    },
    [VAR_SKY] = {5, 1, 0, 0, "25198", NULL, NULL, NULL, NULL, NULL, NULL, NULL, 3, 0, NULL, NULL, 0, 0},
    [VAR_LK] = {
        4, 4, 0, 0, "3..." ".1.." "..4." "...2", "1000010000100001", NULL, NULL, NULL, NULL, NULL, NULL, -1, 0,
        NULL, NULL, 0, 10
    },
};

static const char *const RULES_TEXT[VAR_COUNT] = {
    [VAR_CLASSIC] = "Fill the grid so that every row, every column and every 3x3 box holds the digits 1 to 9, each "
    "once. The given digits can't be changed.",
    [VAR_X] = "Classic rules, and the two long diagonals (tinted) also hold 1 to 9, each once.",
    [VAR_MINI] = "A 6x6 grid: every row, column and 2x3 box holds 1 to 6, each once.",
    [VAR_HYPER] = "Classic rules, and the four tinted 3x3 windows also hold 1 to 9, each once.",
    [VAR_JIGSAW] = "Classic rules, but the boxes are irregular regions of nine cells, outlined with thick lines. "
    "Every region holds 1 to 9, each once.",
    [VAR_KILLER] = "Classic rules, plus cages (dashed outlines): the digits in a cage add up to the small number "
    "in its corner, and no digit repeats in a cage. Hold a cell to see which digits can make up "
    "its cage.",
    [VAR_KNIGHT] = "Classic rules, and two cells a chess knight's move apart never hold the same digit: the 5 "
    "here means none of the crossed cells can be 5.",
    [VAR_EVENODD] = "Classic rules, and some cells are marked: a shaded cell holds an even digit (2, 4, 6, 8), a "
    "cell with a ring an odd one (1, 3, 5, 7, 9).",
    [VAR_CONSEC] = "Classic rules, and a bar joins two side-by-side cells whose digits are 1 apart (4 and 5). "
    "Where there is no bar, they are not 1 apart (5 and 3, 3 and 7).",
    [VAR_THERMO] = "Classic rules, and along each thermometer the digits rise from the round bulb: every digit "
    "is bigger than the one before it.",
    [VAR_SANDWICH] = "Classic rules, and the number by each row (left) and column (top) is the sum of the digits "
    "between that line's 1 and 9, wherever they are: here 4 + 3 + 2 = 9.",
    [VAR_KIDS] = "A small 4x4 grid to start with: every row, every column and every 2x2 box holds 1, 2, 3 and 4, "
    "each once.",
    [VAR_KING] = "Classic rules, and two cells that touch, side by side or corner to corner (a chess king's move), "
    "never hold the same digit: the 5 here means none of the eight cells around it can be 5.",
    [VAR_NONCONSEC] = "Classic rules, and two side-by-side cells never hold digits 1 apart: a 4 can't sit next to "
    "a 3 or a 5. Corner to corner doesn't count.",
    [VAR_DISJOINT] = "Classic rules, and the cells in the same place in each 3x3 box (all the centres, all the "
    "top-left corners, ...) also hold 1 to 9, each once: the tinted 7 and 2 here.",
    [VAR_ASTERISK] = "Classic rules, and the nine tinted cells that make an asterisk around the centre also hold "
    "1 to 9, each once.",
    [VAR_KROPKI] = "Classic rules, and dots between side-by-side cells: a white dot means the digits are 1 apart "
    "(3 and 4), a black dot that one is double the other (4 and 8). No dot: neither (8 and 2). "
    "1 and 2 can have either dot.",
    [VAR_XV] = "Classic rules, and marks between side-by-side cells: V means they add up to 5 (2 and 3), X to 10 "
    "(3 and 7). No mark: they add up to neither (7 and 9).",
    [VAR_GT] = "Classic rules, and inside each 3x3 box a sign between every two side-by-side cells points to "
    "the smaller digit: 2 < 5 > 4 < 9.",
    [VAR_QUAD] = "Classic rules, and a circle where four cells meet lists digits that must be among those four "
    "cells, in any order: here 1, 3 and 9. The fourth cell can be anything else.",
    [VAR_ARROW] = "Classic rules, and the digits along each arrow's shaft add up to the digit in its circle: "
    "4 + 3 = 7. Digits on a shaft can repeat, where the rows, columns and boxes allow it.",
    [VAR_RENBAN] = "Classic rules, and the digits on each purple line are a run of consecutive digits, in any "
    "order: 5, 3, 6, 4 is 3 to 6. No digit repeats on a line.",
    [VAR_WHISPER] = "Classic rules, and along each green line, two neighbours differ by at least 5: 2, 8, 1, 6. "
    "So there's never a 5 on a line.",
    [VAR_SKY] = "Classic rules, and a clue left of a row or above a column says how many buildings are seen from "
    "there: each digit is a building that tall, and a taller one hides the shorter ones behind it. Here "
    "2, 5 and 9 are seen: 3.",
    [VAR_LK] = "Classic rules, and a clue outside the grid with an arrow gives the sum of the digits along that "
    "diagonal: 3 + 1 + 4 + 2 = 10. Digits can repeat on a diagonal where the boxes allow it.",
};

static void rules_pic_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    const rules_pic_t *p = lv_obj_get_user_data(obj);
    if (!p) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    const int cs = RULES_PIC_CS(p), w = p->w * cs, h = p->h * cs;
    const int x0 = (co.x1 + co.x2) / 2 - w / 2, y0 = (co.y1 + co.y2) / 2 - h / 2;
    lv_draw_rect_dsc_t r;
    lv_draw_line_dsc_t l;
    for (int i = 0; i < p->w * p->h; i++) {        /* cells */
        int x = x0 + (i % p->w) * cs, y = y0 + (i / p->w) * cs;
        lv_draw_rect_dsc_init(&r);
        lv_color_t bg = C_CELL;
        if (p->tint && p->tint[i] == '1') {
            bg = lv_color_mix(C_ORANGE, bg, 40);
        }
        if (p->parity && p->parity[i] == 'e') {
            bg = lv_color_mix(C_GRID_THIN, bg, 150);
        }
        r.bg_color = bg;
        r.border_color = C_GRID_THIN;
        r.border_width = 1;
        lv_area_t a = {.x1 = x, .y1 = y, .x2 = x + cs, .y2 = y + cs};
        lv_draw_rect(layer, &r, &a);
    }
    lv_draw_line_dsc_init(&l);                      /* thick lines: boxes or regions, and the frame */
    l.color = C_GRID_THICK;
    l.width = 3;
    for (int i = 0; i < p->w * p->h; i++) {
        int c = i % p->w, rr = i / p->w, x = x0 + c * cs, y = y0 + rr * cs;
        bool right = c + 1 < p->w && (p->regions ? p->regions[i] != p->regions[i + 1] : p->bw && (c + 1) % p->bw == 0);
        bool down = rr + 1 < p->h && (p->regions ? p->regions[i] != p->regions[i + p->w] : p->bh && (rr + 1) % p->bh == 0);
        if (right) {
            l.p1.x = l.p2.x = x + cs;
            l.p1.y = y;
            l.p2.y = y + cs;
            lv_draw_line(layer, &l);
        }
        if (down) {
            l.p1.y = l.p2.y = y + cs;
            l.p1.x = x;
            l.p2.x = x + cs;
            lv_draw_line(layer, &l);
        }
    }
    lv_draw_rect_dsc_init(&r);
    r.bg_opa = LV_OPA_TRANSP;
    r.border_color = C_GRID_THICK;
    r.border_width = 3;
    lv_area_t fr = {.x1 = x0 - 1, .y1 = y0 - 1, .x2 = x0 + w + 1, .y2 = y0 + h + 1};
    lv_draw_rect(layer, &r, &fr);
    if (p->thermo) {                                /* a thermometer (or an arrow, a Renban, a Whispers line) */
        lv_point_t pts[9];
        int n = 0;
        for (; n < 9 && p->thermo[n] != 0xFF; n++) {
            pts[n].x = x0 + (p->thermo[n] % p->w) * cs + cs / 2;
            pts[n].y = y0 + (p->thermo[n] / p->w) * cs + cs / 2;
        }
        draw_line_path(layer, p->line_kind, pts, n, cs);
    }
    for (int i = 0; p->parity && i < p->w * p->h; i++) {   /* odd rings */
        if (p->parity[i] != 'o') {
            continue;
        }
        lv_draw_rect_dsc_init(&r);
        r.bg_opa = LV_OPA_TRANSP;
        r.border_color = C_MUTED;
        r.border_width = 2;
        r.radius = LV_RADIUS_CIRCLE;
        int x = x0 + (i % p->w) * cs, y = y0 + (i / p->w) * cs;
        lv_area_t a = {.x1 = x + 4, .y1 = y + 4, .x2 = x + cs - 4, .y2 = y + cs - 4};
        lv_draw_rect(layer, &r, &a);
    }
    for (int k = 0; p->edges && p->edges[k] != 0xFF; k += 2) {   /* Kropki, XV, Greater-Than marks */
        int a = p->edges[k];
        draw_edge_mark(layer, p->edge_kind, p->edges[k + 1], false, x0 + (a % p->w + 1) * cs,
                       y0 + (a / p->w) * cs + cs / 2);
    }
    if (p->quad) {                                  /* Quadruple: the circle in the middle */
        uint8_t dig[4];
        int n = 0;
        for (const char *q = p->quad; *q && n < 4; q++) {
            dig[n++] = (uint8_t)(*q - '0');
        }
        draw_quad_circle(layer, x0 + w / 2, y0 + h / 2, dig, n);
    }
    for (int k = 0; p->bars && p->bars[k] != 0xFF; k += 2) {   /* consecutive bars */
        int a = p->bars[k], b = p->bars[k + 1];
        lv_draw_rect_dsc_init(&r);
        r.bg_color = C_ORANGE;
        r.radius = 3;
        int x = x0 + (a % p->w + 1) * cs, y = y0 + (a / p->w) * cs + cs / 2;
        (void)b;
        lv_area_t ar = {.x1 = x - 4, .y1 = y - 11, .x2 = x + 4, .y2 = y + 11};
        lv_draw_rect(layer, &r, &ar);
    }
    if (p->cages) {                                 /* dashed cages with their sums */
        const uint8_t *q = p->cages;
        while (*q != 0xFF) {
            int sum = *q++, first = *q;
            uint8_t in[16];
            int n = 0;
            while (*q != 0xFE) {
                in[n++] = *q++;
            }
            q++;
            lv_draw_line_dsc_init(&l);
            l.color = C_MUTED;
            l.width = 1;
            l.dash_width = 4;
            l.dash_gap = 3;
            for (int j = 0; j < n; j++) {
                int c = in[j], cx = c % p->w, cy = c / p->w, x = x0 + cx * cs, y = y0 + cy * cs;
                bool up = false, dn = false, lf = false, rt = false;
                for (int k = 0; k < n; k++) {
                    up |= in[k] == c - p->w;
                    dn |= in[k] == c + p->w;
                    lf |= cx > 0 && in[k] == c - 1;
                    rt |= cx < p->w - 1 && in[k] == c + 1;
                }
                if (!up) {
                    l.p1.x = x + 4;
                    l.p2.x = x + cs - 4;
                    l.p1.y = l.p2.y = y + 4;
                    lv_draw_line(layer, &l);
                }
                if (!dn) {
                    l.p1.x = x + 4;
                    l.p2.x = x + cs - 4;
                    l.p1.y = l.p2.y = y + cs - 4;
                    lv_draw_line(layer, &l);
                }
                if (!lf) {
                    l.p1.y = y + 4;
                    l.p2.y = y + cs - 4;
                    l.p1.x = l.p2.x = x + 4;
                    lv_draw_line(layer, &l);
                }
                if (!rt) {
                    l.p1.y = y + 4;
                    l.p2.y = y + cs - 4;
                    l.p1.x = l.p2.x = x + cs - 4;
                    lv_draw_line(layer, &l);
                }
            }
            char t[4];
            snprintf(t, sizeof(t), "%d", sum);
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = &lv_font_montserrat_12;
            d.color = C_TEXT;
            d.text = t;
            d.text_local = 1;
            lv_area_t la = {.x1 = x0 + (first % p->w) * cs + 3, .y1 = y0 + (first / p->w) * cs + 2,
                            .x2 = x0 + (first % p->w) * cs + 30, .y2 = y0 + (first / p->w) * cs + 16
                           };
            lv_draw_label(layer, &d, &la);
        }
    }
    if (p->lk_clue > 0) {                           /* Little Killer: a sum above the corner, its arrow down-right */
        char t[4];
        snprintf(t, sizeof(t), "%d", p->lk_clue);
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = &lv_font_montserrat_20;
        d.color = C_ACCENT;
        d.align = LV_TEXT_ALIGN_RIGHT;
        d.text = t;
        d.text_local = 1;
        lv_area_t a = {.x1 = x0 - 50, .y1 = y0 - 30, .x2 = x0 - 14, .y2 = y0 - 6};
        lv_draw_label(layer, &d, &a);
        draw_lk_arrow(layer, x0 - 12, y0 - 12, LK_DOWN_RIGHT, 9);
    }
    if (p->left_clue >= 0) {                        /* Sandwich, Skyscraper: the clue left of the row */
        char t[4];
        snprintf(t, sizeof(t), "%d", p->left_clue);
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = &lv_font_montserrat_22;
        d.color = C_ACCENT;
        d.align = LV_TEXT_ALIGN_RIGHT;
        d.text = t;
        d.text_local = 1;
        lv_area_t a = {.x1 = x0 - 50, .y1 = y0 + (cs - 24) / 2, .x2 = x0 - 10, .y2 = y0 + cs};
        lv_draw_label(layer, &d, &a);
    }
    for (int i = 0; i < p->w * p->h; i++) {         /* digits and crosses */
        char t[2] = {0, 0};
        lv_color_t col = C_TEXT;
        if (p->digits[i] >= '1' && p->digits[i] <= '9') {
            t[0] = p->digits[i];
        } else if (p->cross && p->cross[i] == 'x') {
            t[0] = 'x';
            col = C_RED;
        }
        if (!t[0]) {
            continue;
        }
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = &lv_font_montserrat_22;
        d.color = col;
        d.align = LV_TEXT_ALIGN_CENTER;
        d.text = t;
        d.text_local = 1;
        int x = x0 + (i % p->w) * cs, y = y0 + (i / p->w) * cs;
        lv_area_t a = {.x1 = x, .y1 = y + (cs - 24) / 2, .x2 = x + cs, .y2 = y + cs};
        lv_draw_label(layer, &d, &a);
    }
}

static void rules_show(int variant)
{
    if (variant < 0 || variant >= VAR_COUNT) {
        variant = VAR_CLASSIC;
    }
    char title[40];
    snprintf(title, sizeof(title), "%s rules", variant_name(variant));
    lv_obj_t *m = make_msgbox(title, NULL);
    lv_obj_set_width(m, 600);
    lv_obj_t *pic = lv_obj_create(lv_msgbox_get_content(m));
    lv_obj_remove_style_all(pic);
    const rules_pic_t *p = &RULES_PIC[variant];
    lv_obj_set_size(pic, LV_PCT(100), p->h * RULES_PIC_CS(p) + 16 + (p->lk_clue > 0 ? 32 : 0));
    lv_obj_set_user_data(pic, (void *)p);
    lv_obj_add_event_cb(pic, rules_pic_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_t *t = make_wrap_text(lv_msgbox_get_content(m), NULL, C_TEXT, RULES_TEXT[variant]);
    lv_obj_set_style_margin_top(t, 10, 0);
    msgbox_button(m, "OK", mbox_close_cb, true);
}

void rules_cb(lv_event_t *e)
{
    rules_show(game_var());
}

/* the first time a variant is played: its rules */
void rules_first_time(void)
{
    uint32_t seen = store_rules_seen();
    if (game_var() == VAR_CLASSIC || (seen >> game_var() & 1)) {
        return;
    }
    store_set_rules_seen(seen | (1u << game_var()));
    rules_show(game_var());
}

/* ------------------------------------------------------------- share (QR code) */
/*
 * The Share button: the puzzle as a QR code to scan with a phone. A Classic
 * (or imported) puzzle is the usual 81-digit string, givens row by row with 0
 * for empty, which most Sudoku apps can import; a variant is its name, the
 * givens and its layout, in the text forms sudoku.db keeps them in.
 */
static uint8_t *qr_code;                            /* qrcodegen_BUFFER_LEN_MAX each, PSRAM */
static uint8_t *qr_temp;

/* the text in the code; false if it doesn't fit buf */
static bool share_text(char *buf, size_t n)
{
    const sudoku_geom_t *G = game_geo();
    char giv[82];
    int k = 0;
    for (int i = 0; i < G->ncells; i++) {
        giv[k++] = (char)('0' + game_given()[G->cells[i]]);    /* (6x6: its 36 cells) */
    }
    giv[k] = 0;
    if (game_var() == VAR_CLASSIC) {
        return (size_t)snprintf(buf, n, "%s", giv) < n;
    }
    int len = snprintf(buf, n, "%s %s", variant_name(game_var()), giv);
    if (len < 0 || (size_t)len >= n) {
        return false;
    }
    static char extra[STATS_CAGES_LEN];
    extra[0] = 0;
    const char *key = NULL;
    switch (game_var()) {
    case VAR_JIGSAW:
        for (int i = 0; i < 81; i++) {
            extra[i] = (char)('0' + G->region[i]);
        }
        extra[81] = 0;
        key = "regions";
        break;
    case VAR_KILLER:
        key = sudoku_cages_text(G, extra, sizeof(extra)) ? "cages" : NULL;
        break;
    case VAR_QUAD:
        key = sudoku_quads_text(G, extra, sizeof(extra)) ? "circles" : NULL;
        break;
    case VAR_EVENODD:
    case VAR_CONSEC:
    case VAR_KROPKI:
    case VAR_XV:
    case VAR_GT:
        key = sudoku_marks_text(G, extra, sizeof(extra)) ? "marks" : NULL;
        break;
    case VAR_THERMO:
        key = sudoku_thermos_text(G, extra, sizeof(extra)) ? "thermos" : NULL;
        break;
    case VAR_ARROW:
    case VAR_RENBAN:
    case VAR_WHISPER:
        key = sudoku_thermos_text(G, extra, sizeof(extra)) ? "lines" : NULL;
        break;
    case VAR_SANDWICH:
        key = sudoku_sandwich_text(G, extra, sizeof(extra)) ? "sums" : NULL;
        break;
    case VAR_SKY:
        key = sudoku_sky_text(G, extra, sizeof(extra)) ? "clues" : NULL;
        break;
    case VAR_LK:
        key = sudoku_lk_text(G, extra, sizeof(extra)) ? "diagonals" : NULL;
        break;
    default:
        break;
    }
    if (key) {
        len += snprintf(buf + len, n - (size_t)len, " %s=%s", key, extra);
    }
    return (size_t)len < n;
}

/* the code: black modules on white (whatever the theme), with a quiet zone of 3 */
static void qr_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    int size = qrcodegen_getSize(qr_code);
    lv_area_t co;
    lv_obj_get_coords(obj, &co);
    int w = lv_area_get_width(&co), h = lv_area_get_height(&co), side = w < h ? w : h;
    int mod = side / (size + 6);                    /* whole pixels per module, the code and its margin fitting */
    if (mod < 1) {
        mod = 1;
    }
    int x0 = co.x1 + (w - mod * size) / 2, y0 = co.y1 + (h - mod * size) / 2;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_white();
    d.radius = 6;
    lv_area_t bg = {.x1 = x0 - 3 * mod, .y1 = y0 - 3 * mod, .x2 = x0 + (size + 3) * mod - 1, .y2 = y0 + (size + 3) * mod - 1};
    lv_draw_rect(layer, &d, &bg);                   /* white, with the 3-module quiet zone */
    d.radius = 0;
    d.bg_color = lv_color_black();
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size;) {                /* a row's runs of dark modules as one rectangle each */
            if (!qrcodegen_getModule(qr_code, x, y)) {
                x++;
                continue;
            }
            int x1 = x;
            while (x < size && qrcodegen_getModule(qr_code, x, y)) {
                x++;
            }
            lv_area_t a = {.x1 = x0 + x1 * mod, .y1 = y0 + y * mod, .x2 = x0 + x * mod - 1, .y2 = y0 + (y + 1) * mod - 1};
            lv_draw_rect(layer, &d, &a);
        }
    }
}

/* a QR code of text in a dialog, the note under it, then the text itself */
lv_obj_t *qr_dialog(const char *title, const char *text, const char *note)
{
    if (!qr_code) {
        qr_code = heap_caps_malloc(qrcodegen_BUFFER_LEN_MAX, MALLOC_CAP_SPIRAM);
    }
    if (!qr_temp) {
        qr_temp = heap_caps_malloc(qrcodegen_BUFFER_LEN_MAX, MALLOC_CAP_SPIRAM);
    }
    if (!qr_code || !qr_temp ||
            !qrcodegen_encodeText(text, qr_temp, qr_code, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                  qrcodegen_Mask_AUTO, true)) {
        return NULL;
    }
    lv_obj_t *m = make_msgbox(title, NULL);
    lv_obj_set_width(m, 560);
    lv_obj_set_style_text_font(lv_msgbox_get_header(m), &font_icons_20, 0);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_t *qr = lv_obj_create(c);
    lv_obj_remove_style_all(qr);
    lv_obj_set_size(qr, LV_PCT(100), 340);
    lv_obj_add_event_cb(qr, qr_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    make_wrap_text(c, &lv_font_montserrat_16, C_MUTED, note);
    lv_obj_t *t = make_wrap_text(c, &lv_font_montserrat_12, C_TEXT, text);
    lv_obj_set_style_margin_top(t, 6, 0);
    msgbox_button(m, "Close", mbox_close_cb, false);
    return m;
}

static uint16_t pz_crc16(const char *p, size_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)((uint8_t)*p++ << 8);
        for (int k = 0; k < 8; k++) {
            c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
        }
    }
    return c;
}

/* the puzzle on screen as that line (sdx_format, the givens packed); its length, 0 if it doesn't fit */
static int pz_line(char *buf, size_t n)
{
    const sudoku_geom_t *G = game_geo();
    int v = VAR_CLASSIC, lv = -1;
    if (puzzle_is_generated(game_id())) {
        gen_lookup(game_id(), NULL, &v, &lv);
    }
    static char extra[STATS_CAGES_LEN];
    char regions[82] = "";
    const char *cages = "", *marks = "", *thermos = "";
    extra[0] = 0;
    switch (game_var()) {
    case VAR_JIGSAW:
        for (int i = 0; i < 81; i++) {
            regions[i] = (char)('0' + G->region[i]);
        }
        regions[81] = 0;
        break;
    case VAR_KILLER:
        if (sudoku_cages_text(G, extra, sizeof(extra))) {
            cages = extra;
        }
        break;
    case VAR_QUAD:
        if (sudoku_quads_text(G, extra, sizeof(extra))) {
            cages = extra;
        }
        break;
    case VAR_EVENODD:
    case VAR_CONSEC:
    case VAR_KROPKI:
    case VAR_XV:
    case VAR_GT:
        if (sudoku_marks_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_SANDWICH:
        if (sudoku_sandwich_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_SKY:
        if (sudoku_sky_text(G, extra, sizeof(extra))) {
            marks = extra;
        }
        break;
    case VAR_LK:
        if (sudoku_lk_text(G, extra, sizeof(extra))) {
            cages = extra;
        }
        break;
    case VAR_THERMO:
    case VAR_ARROW:
    case VAR_RENBAN:
    case VAR_WHISPER:
        if (sudoku_thermos_text(G, extra, sizeof(extra))) {
            thermos = extra;
        }
        break;
    default:
        break;
    }
    int len = sdx_format(game_var(), lv >= 0 && lv < GEN_LEVELS ? lv : -1, game_given(), regions, cages, marks, thermos, true,
                         buf, n);
    return len < 0 ? 0 : len;
}

static lv_timer_t *pz_send_timer;

static void pz_send_poll(lv_timer_t *t)
{
    if (gibberlink_sending()) {
        return;
    }
    lv_timer_delete(pz_send_timer);
    pz_send_timer = NULL;
    sudoku_ui_notice("Sent by sound.", 2500);
}

static void pz_send_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (pz_send_timer || gibberlink_sending()) {
        sudoku_ui_notice("Still sending the last one.", 2500);
        return;
    }
    static char line[STATS_CAGES_LEN + 400];
    int len = pz_line(line, sizeof(line));
    int parts = (len + PZ_PART - 1) / PZ_PART;
    if (!len || parts > PZ_MAX_PARTS) {
        sudoku_ui_notice("This puzzle is too long to send by sound.", 3000);
        return;
    }
    unsigned id = esp_random() & 0xFF;
    for (int i = 0; i < parts; i++) {
        char body[160], msg[176];
        int k = len - i * PZ_PART < PZ_PART ? len - i * PZ_PART : PZ_PART;
        snprintf(body, sizeof(body), "pz %02X %d/%d %.*s", id, i + 1, parts, k, line + i * PZ_PART);
        snprintf(msg, sizeof(msg), PZ_ID "$%s*%04X", body, pz_crc16(body, strlen(body)));
        if (!gibberlink_send(msg)) {
            sudoku_ui_notice("Couldn't send it: no memory for the sound.", 3000);
            return;
        }
    }
    char name[48], msg[200];
    puzzle_name(game_id(), name, sizeof(name));
    snprintf(msg, sizeof(msg), "Sending %s by sound: %d part%s, about %d s. On the other board, Gibberlink must be "
             "on (Settings > Sound).", name, parts, parts == 1 ? "" : "s", (len + 20 * parts) / 16 + parts + 1);
    sudoku_ui_notice(msg, 5000);
    pz_send_timer = lv_timer_create(pz_send_poll, 500, NULL);
}

/* ---- receiving: the parts gathered (devctl "pz"), then checked off the LVGL task */
static struct {
    int id, n;                                    /* id -1: nothing coming */
    uint32_t got;                                 /* bit i-1: part i is here */
    uint32_t start;                               /* lv_tick of the first part */
    char part[PZ_MAX_PARTS][PZ_PART + 1];
} pz_rx = {.id = -1};
static pz_got_t *pz_got;                          /* PSRAM */
static lv_timer_t *pz_check_timer;
static TaskHandle_t pz_check_task;

static void pz_check_fn(void *arg)
{
    pz_got->graded = sudoku_grade(&pz_got->geom, pz_got->givens);
    vTaskSuspend(NULL);                           /* (deleted by pz_check_poll) */
}

static uint16_t pz_play_id;                       /* the one just received (its dialog's Play) */

static void pz_play_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    open_puzzle(pz_play_id);
}

static void pz_check_poll(lv_timer_t *t)
{
    if (pz_got->graded == -2 || eTaskGetState(pz_check_task) != eSuspended) {
        return;
    }
    vTaskDeleteWithCaps(pz_check_task);
    pz_check_task = NULL;
    lv_timer_delete(pz_check_timer);
    pz_check_timer = NULL;
    pz_got_t *p = pz_got;
    if (p->graded < 0) {
        sudoku_ui_notice("A puzzle came by sound, but it doesn't have exactly one solution: not kept.", 4000);
        return;
    }
    int level = p->level >= 0 ? p->level : p->graded;
    if (level < 0 || level >= GEN_LEVELS) {
        level = GEN_EXPERT;
    }
    const uint8_t *regions = p->variant == VAR_JIGSAW ? p->geom.region : NULL;
    const char *cages = p->cages[0] ? p->cages : NULL, *marks = p->marks[0] ? p->marks : NULL;
    const char *thermos = p->thermos[0] ? p->thermos : NULL;
    int number = 0;                               /* "Received #3": numbered like My puzzles */
    int gid = stats_db_ready() ? stats_db_gen_add_from(PZ_SOURCE, p->variant, level, p->givens, regions, cages, marks,
              thermos, &number)
              : -1;
    uint16_t id;
    if (gid > 0 && gid < PUZZLE_GEN_RAM_BASE - PUZZLE_GEN_BASE) {
        id = (uint16_t)(PUZZLE_GEN_BASE + gid);
    } else {
        id = gen_ram_take();                    /* no card: until power-off */
        static int ram_number;
        number = ++ram_number;
    }
    gen_cache_put(id, p->variant, level, p->givens, regions, cages, marks, thermos, NULL);
    gen_cache_source(id, PZ_SOURCE, number);
    char why[96] = "", text[200];
    bool kept = gid > 0 && stats_db_saved_add(PZ_SOURCE, id, why, sizeof(why));
    if (kept) {
        sv_changed();    /* (Puzzles' column: "Received" may be new) */
    }
    snprintf(text, sizeof(text), "%s #%d, %s %s, came by sound%s.", PZ_SOURCE, number, variant_name(p->variant),
             gen_level_name(level),
             kept ? ": it's in Puzzles > Saved > Received" : gid > 0 ? "" : " (kept until power-off: no card)");
    pz_play_id = id;
    lv_obj_t *m = make_msgbox(LV_SYMBOL_AUDIO "  Puzzle received", text);
    msgbox_button(m, "Later", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_PLAY "  Play", pz_play_cb, true);
}

/* the whole line: its fields read (sdx_parse), the board made, the check started; why on failure */
static bool pz_take(char *line, char *why, size_t n)
{
    if (pz_check_timer) {
        snprintf(why, n, "busy with the last one");
        return false;
    }
    if (!pz_got && !(pz_got = heap_caps_calloc(1, sizeof(*pz_got), MALLOC_CAP_SPIRAM))) {
        snprintf(why, n, "no memory");
        return false;
    }
    pz_got_t *p = pz_got;
    sdx_puzzle_t sp;
    if (!sdx_parse(line, &sp)) {
        snprintf(why, n, "it doesn't read as a puzzle");
        return false;
    }
    p->variant = sp.variant;
    p->level = sp.level;
    memcpy(p->givens, sp.givens, 81);
    snprintf(p->regions, sizeof(p->regions), "%s", sp.regions);
    snprintf(p->cages, sizeof(p->cages), "%s", sp.cages);
    snprintf(p->marks, sizeof(p->marks), "%s", sp.marks);
    snprintf(p->thermos, sizeof(p->thermos), "%s", sp.thermos);
    if (!sdx_geom(&sp, &p->geom)) {
        snprintf(why, n, "its layout doesn't read");
        return false;
    }
    p->graded = -2;
    if (xTaskCreatePinnedToCoreWithCaps(pz_check_fn, "pzcheck", 16384, NULL, 2, &pz_check_task, WORKER_CORE,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        snprintf(why, n, "no memory for the check");
        return false;
    }
    pz_check_timer = lv_timer_create(pz_check_poll, 50, NULL);
    sudoku_ui_notice("A puzzle came by sound: checking it...", 2000);
    return true;
}

bool sudoku_ui_puzzle_part(const char *args, char *out, size_t n)
{
    unsigned id;
    int i, cnt, off = 0;
    while (*args == ' ') {
        args++;
    }
    if (sscanf(args, "%x %d/%d %n", &id, &i, &cnt, &off) < 3 || !off || cnt < 1 || cnt > PZ_MAX_PARTS || i < 1 ||
            i > cnt) {
        snprintf(out, n, "ERR pz: pz <id> <i>/<n> <part>");
        return false;
    }
    const char *data = args + off;
    if (strlen(data) > PZ_PART) {
        snprintf(out, n, "ERR pz: a part is %d characters at most", PZ_PART);
        return false;
    }
    if (pz_rx.id != (int)id || pz_rx.n != cnt || lv_tick_elaps(pz_rx.start) > PZ_TIMEOUT_MS) {   /* a new one */
        pz_rx.id = (int)id;
        pz_rx.n = cnt;
        pz_rx.got = 0;
        pz_rx.start = lv_tick_get();
    }
    snprintf(pz_rx.part[i - 1], sizeof(pz_rx.part[0]), "%s", data);
    pz_rx.got |= 1u << (i - 1);
    int have = __builtin_popcount(pz_rx.got);
    if (have < cnt) {
        char msg[64];
        snprintf(msg, sizeof(msg), LV_SYMBOL_AUDIO "  Puzzle by sound: part %d of %d", have, cnt);
        sudoku_ui_notice(msg, 3000);
        snprintf(out, n, "OK pz %d/%d", have, cnt);
        return true;
    }
    static char line[PZ_MAX_PARTS * PZ_PART + 1];
    line[0] = 0;
    for (int k = 0; k < cnt; k++) {
        strlcat(line, pz_rx.part[k], sizeof(line));
    }
    pz_rx.id = -1;
    char why[64];
    if (!pz_take(line, why, sizeof(why))) {
        char msg[120];
        snprintf(msg, sizeof(msg), "A puzzle came by sound, but %s.", why);
        sudoku_ui_notice(msg, 4000);
        snprintf(out, n, "ERR pz: %s", why);
        return false;
    }
    snprintf(out, n, "OK pz done");
    return true;
}

void share_cb(lv_event_t *e)
{
    if (game_id() == STORE_NO_GAME) {
        return;
    }
    static char text[STATS_CAGES_LEN + 128];
    char name[48], title[64];
    puzzle_name(game_id(), name, sizeof(name));
    snprintf(title, sizeof(title), SYMBOL_QR "  %s", name);
    lv_obj_t *m = share_text(text, sizeof(text))
                  ? qr_dialog(title, text, game_var() == VAR_CLASSIC ? "Scan it with a phone: the givens row by row, 0 = empty. Most "
                              "Sudoku apps can import this. Or send it by sound to another "
                              "board (Gibberlink on there)."
                              : "Scan it with a phone: the variant, the givens row by row "
                              "(0 = empty) and its layout. Or send it by sound to another "
                              "board (Gibberlink on there).")
                  : NULL;
    if (!m) {
        sudoku_ui_notice("Couldn't make a QR code of this puzzle.", 3000);
        return;
    }
    msgbox_button(m, LV_SYMBOL_AUDIO "  Send by sound", pz_send_cb, true);   /* (another board, by Gibberlink) */
}

/* ------------------------------------------------------------- bookmarks */

/* ------------------------------------------------------------- save to a collection */
/*
 * The game screen's Save (generated and typed-in puzzles): a dialog lists the saved
 * collections, ticked where the puzzle is; a tap puts it in or takes it out, and
 * "New collection..." asks for a name (the keyboard) and puts it there. They show in
 * Puzzles > Saved. A collection goes when its last puzzle is taken out.
 */
static lv_obj_t *sv_box;

static void sv_close_cb(lv_event_t *e)
{
    if (sv_box) {
        lv_msgbox_close_async(sv_box);
    }
    sv_box = NULL;
}

/* after a change: the collections again, a new or gone one in Puzzles' row */
void sv_changed(void)
{
    int before = s_nsaved;
    imports_reload();
    if (s_nsaved != before) {
        list_fill_colls();
    }
    refresh_list();
    update_header();
}

static void sv_toggle_cb(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (k < 0 || k >= s_nsaved) {
        return;
    }
    char name[STATS_IMPORT_NAME], msg[96], why[96];
    snprintf(name, sizeof(name), "%s", ui_saved()[k].name);
    if (coll_find(PUZZLE_COLLECTION_COUNT + ui_nimports() + k, game_id()) >= 0) {
        bool last = ui_saved()[k].count == 1;
        stats_db_saved_remove(ui_saved()[k].id, game_id());
        snprintf(msg, sizeof(msg), last ? "Taken out of %s (it was the last: the collection is gone)."
                 : "Taken out of %s.", name);
    } else if (stats_db_saved_add(name, game_id(), why, sizeof(why))) {
        snprintf(msg, sizeof(msg), "Saved to %s (Puzzles > Saved).", name);
    } else {
        snprintf(msg, sizeof(msg), "%s", why);
    }
    sv_changed();
    sudoku_ui_notice(msg, 2500);
    sv_close_cb(NULL);
    sv_open();                                      /* (the ticks again) */
}

static void sv_new_done(const char *text)
{
    char name[STATS_IMPORT_NAME];
    while (*text == ' ') {
        text++;
    }
    snprintf(name, sizeof(name), "%s", text);
    for (size_t l = strlen(name); l && name[l - 1] == ' ';) {
        name[--l] = 0;
    }
    if (!name[0]) {
        sudoku_ui_notice("A collection needs a name.", 2500);
        return;
    }
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT + ui_nimports(); c++) {
        if (!strcasecmp(coll_name(c), name)) {
            sudoku_ui_notice("That name is taken by a built-in or imported collection.", 3000);
            return;
        }
    }
    char msg[96], why[96];
    if (stats_db_saved_add(name, game_id(), why, sizeof(why))) {
        snprintf(msg, sizeof(msg), "Saved to %s (Puzzles > Saved).", name);
    } else {
        snprintf(msg, sizeof(msg), "%s", why);
    }
    sv_changed();
    sudoku_ui_notice(msg, 2500);
}

static void sv_new_cb(lv_event_t *e)
{
    sv_close_cb(NULL);
    nd_open_ex("New collection", "", STATS_IMPORT_NAME - 1, "Save", sv_new_done);
}

static void sv_open(void)
{
    lv_obj_t *m = make_msgbox(LV_SYMBOL_SAVE "  Save to a collection", NULL);
    sv_box = m;
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 8, 0);
    lv_obj_set_style_max_height(c, 400, 0);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    if (!s_nsaved) {
        make_wrap_text(c, NULL, C_MUTED, "No collections yet: make one.");
    }
    for (int k = 0; k < s_nsaved; k++) {
        bool in = coll_find(PUZZLE_COLLECTION_COUNT + ui_nimports() + k, game_id()) >= 0;
        lv_obj_t *b = make_btn(c, 0, 0, 0, 56, NULL, NULL, sv_toggle_cb, (void *)(intptr_t)k);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_style_bg_color(b, in ? C_ACCENT : C_CARD2, 0);
        lv_obj_t *l = make_label(b, &lv_font_montserrat_20, in ? lv_color_white() : C_TEXT);
        lv_label_set_text_fmt(l, "%s%s", in ? LV_SYMBOL_OK "  " : "", ui_saved()[k].name);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *n = make_label(b, &lv_font_montserrat_16, in ? lv_color_white() : C_MUTED);
        lv_label_set_text_fmt(n, "%d", ui_saved()[k].count);
        lv_obj_align(n, LV_ALIGN_RIGHT_MID, -8, 0);
    }
    if (s_nsaved < STATS_MAX_SAVED) {
        lv_obj_t *nb = make_btn(c, 0, 0, 0, 56, LV_SYMBOL_PLUS "  New collection...", &lv_font_montserrat_20,
                                sv_new_cb, NULL);
        lv_obj_set_width(nb, LV_PCT(100));
        lv_obj_set_style_bg_color(nb, C_CARD2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(nb, 0), C_ACCENT, 0);
    }
    msgbox_button(m, "Close", sv_close_cb, false);
}

void save_cb(lv_event_t *e)
{
    if (game_practice() >= 0 || !puzzle_is_generated(game_id()) || game_id() >= PUZZLE_GEN_RAM_BASE) {
        return;
    }
    if (!stats_db_ready()) {
        sudoku_ui_notice("Collections are kept in sudoku.db on a microSD card: there's none.", 3000);
        return;
    }
    imports_reload();                               /* (current: another player may have changed them) */
    sv_open();
}

/* ---- accessors: the other UI files reach this file's state through these */
uint32_t attack_used_ms(void) { return atk_used_ms; }
void attack_set_used_ms(uint32_t v) { atk_used_ms = v; }
int attack_solved(void) { return atk_solved; }
void attack_set_solved(int v) { atk_solved = v; }
int attack_mistakes(void) { return atk_mistakes; }
void attack_set_mistakes(int v) { atk_mistakes = v; }
int ui_nsaved(void) { return s_nsaved; }
void ui_set_nsaved(int v) { s_nsaved = v; }
