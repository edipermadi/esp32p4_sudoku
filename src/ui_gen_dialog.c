/**
 * @file ui_gen_dialog.c
 * @ingroup ui_game
 * @brief The New puzzle dialog: the variants in four tabs (Grids, Marks, Lines, Clues), what each is, the levels.
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void gen_levels_update(void);
static void gen_pick_close(void);
static void gen_level_cb(lv_event_t *e);
static void gen_var_cb(lv_event_t *e);
static void gen_cat_cb(lv_event_t *e);
static void gen_cancel_cb(lv_event_t *e);
static void gen_backdrop_cb(lv_event_t *e);

static lv_obj_t *gen_pick;                          /**< the level dialog */
static lv_obj_t *gen_pick_desc;                     /**< its line about the variant chosen */
static int gen_pick_var = VAR_CLASSIC;              /**< variant chosen there (kept until power-off) */

#define GEN_CATS     4                  /**< the dialog's tabs */
#define GEN_CAT_SLOTS 20                /**< variant buttons per tab: 5 rows of 4 (the unused ones hidden) */
#define GEN_CAT_END  0xFF               /**< after a tab's last variant */

/** @brief the New puzzle dialog's tabs: their names */
static const char *const GEN_CAT_NAMES[GEN_CATS] = {"Grids", "Marks", "Lines", "Clues"};
/** @brief each tab's variants, in button order: the grids and their extra regions; marks and rules
 * between cells; lines; clues (cages, and sums or counts outside the grid). Each tab runs from the easiest,
 * top left, to the hardest, bottom right: the familiar rules and those marked on the board first, those where
 * a missing mark says something, and the less familiar kinds, later. */
static const uint8_t GEN_CAT_VARS[GEN_CATS][GEN_CAT_SLOTS + 1] = {
    {
        VAR_KIDS, VAR_MINI, VAR_CLASSIC, VAR_CENTERDOT, VAR_GIRANDOLA, VAR_ASTERISK, VAR_X, VAR_HYPER, VAR_REGIONS,
        VAR_DISJOINT, VAR_JIGSAW, VAR_MAGIC, VAR_ANTIDIAG, VAR_159, VAR_ARGYLE, GEN_CAT_END
    },
    {
        VAR_EVENODD, VAR_CONSEC, VAR_GT, VAR_FORTRESS, VAR_QUAD, VAR_MATHRAX, VAR_XV, VAR_KROPKI, VAR_KING, VAR_KNIGHT,
        VAR_CHESS, VAR_FLATMATES, VAR_DIAGCONSEC, VAR_NONCONSEC, VAR_DIAGNC, GEN_CAT_END
    },
    {
        VAR_THERMO, VAR_SLOW, VAR_ARROW, VAR_AVG, VAR_PILL, VAR_PALINDROME, VAR_RENBAN, VAR_PARITY, VAR_WHISPER, VAR_DUTCH, VAR_DARROW,
        VAR_REGIONSUM, VAR_ZIPPER, VAR_NABNER, VAR_BETWEEN, VAR_LOCKOUT, VAR_TEN, VAR_ENTROPIC, VAR_MODULAR, GEN_CAT_END
    },
    {
        VAR_KILLER, VAR_FRAME, VAR_OUTSIDE, VAR_SKY, VAR_LK, VAR_PRODUCT, VAR_SANDWICH, VAR_ROSSINI, VAR_XSUM,
        VAR_PSKY, VAR_SKYSUM, VAR_NROOMS, VAR_SUMLESS, GEN_CAT_END
    },
};
/** @brief each variant's button text (shorter than variant_name() where four across need it) */
static const char *const GEN_VAR_LABEL[VAR_COUNT] = {
    [VAR_CLASSIC] = "Classic", [VAR_X] = "X-Sudoku", [VAR_MINI] = "Mini 6x6", [VAR_HYPER] = "Hyper",
    [VAR_JIGSAW] = "Jigsaw", [VAR_KILLER] = "Killer", [VAR_KNIGHT] = "Anti-Knight", [VAR_EVENODD] = "Even-Odd",
    [VAR_CONSEC] = "Consecutive", [VAR_THERMO] = "Thermo", [VAR_SANDWICH] = "Sandwich", [VAR_KIDS] = "Kids 4x4",
    [VAR_KING] = "Anti-King", [VAR_NONCONSEC] = "Non-Consec.", [VAR_DISJOINT] = "Disjoint", [VAR_ASTERISK] = "Asterisk",
    [VAR_KROPKI] = "Kropki", [VAR_XV] = "XV", [VAR_GT] = "Greater-Than", [VAR_QUAD] = "Quadruple", [VAR_ARROW] = "Arrow",
    [VAR_RENBAN] = "Renban", [VAR_WHISPER] = "Whispers", [VAR_SKY] = "Skyscraper", [VAR_LK] = "Little Killer",
    [VAR_PALINDROME] = "Palindrome", [VAR_REGIONSUM] = "Region Sum", [VAR_XSUM] = "X-Sums", [VAR_ZIPPER] = "Zipper",
    [VAR_FRAME] = "Frame", [VAR_BETWEEN] = "Between", [VAR_ENTROPIC] = "Entropic", [VAR_MODULAR] = "Modular",
    [VAR_CENTERDOT] = "Center Dot", [VAR_OUTSIDE] = "Outside", [VAR_ROSSINI] = "Rossini", [VAR_DIAGNC] = "Diagonal NC",
    [VAR_DUTCH] = "Dutch", [VAR_PARITY] = "Parity", [VAR_SLOW] = "Slow Thermo", [VAR_FORTRESS] = "Fortress",
    [VAR_ARGYLE] = "Argyle", [VAR_NABNER] = "Nabner", [VAR_NROOMS] = "Numbered", [VAR_SKYSUM] = "Sum Sky",
    [VAR_LOCKOUT] = "Lockout", [VAR_SUMLESS] = "Sumless", [VAR_DARROW] = "Double Arrow",
    [VAR_DIAGCONSEC] = "Diagonal C.", [VAR_TEN] = "10 Lines", [VAR_GIRANDOLA] = "Girandola",
    [VAR_159] = "159 Sudoku", [VAR_PRODUCT] = "Product", [VAR_REGIONS] = "Extra Regions",
    [VAR_ANTIDIAG] = "Anti-Diagonal", [VAR_PILL] = "Pill Arrow", [VAR_PSKY] = "Odd/Even Sky",
    [VAR_CHESS] = "Chess", [VAR_MAGIC] = "Magic Square", [VAR_AVG] = "Average", [VAR_FLATMATES] = "Flatmates",
    [VAR_MATHRAX] = "Mathrax",
};
/** @brief each tab's variant picked last (the one a tab opens on; kept until power-off) */
static uint8_t gen_cat_pick[GEN_CATS] = {VAR_CLASSIC, VAR_KNIGHT, VAR_THERMO, VAR_KILLER};
static int gen_pick_cat;                            /**< the tab shown (kept until power-off) */
static lv_obj_t *gen_var_bm;                        /**< the variant buttons of the tab shown */
/** each variant's rules in a sentence (New puzzle's dialog) */
static const char *const GEN_VAR_DESC[VAR_COUNT] = {
    [VAR_CLASSIC] = "9x9: rows, columns and 3x3 boxes hold 1-9 once each.",
    [VAR_X] = "9x9, and the two tinted diagonals also hold 1-9 once each.",
    [VAR_MINI] = "6x6 with 2x3 boxes: rows, columns and boxes hold 1-6 once each.",
    [VAR_HYPER] = "9x9, and the four tinted 3x3 windows also hold 1-9 once each.",
    [VAR_JIGSAW] = "9x9 with nine irregular regions (thick borders) instead of boxes; each holds 1-9 once.",
    [VAR_KILLER] = "9x9 with cages (dashed): a cage's digits add up to its sum, none repeated. Few or no givens.",
    [VAR_KNIGHT] = "9x9, and two cells a chess knight's move apart never hold the same digit.",
    [VAR_EVENODD] = "9x9: shaded cells hold an even digit, cells with a ring an odd one.",
    [VAR_CONSEC] = "9x9: a bar joins every two side-by-side cells whose digits are 1 apart; no bar, they aren't.",
    [VAR_THERMO] = "9x9 with thermometers: along each one, the digits rise from the round bulb.",
    [VAR_SANDWICH] = "9x9: the number by each row and column is the sum of the digits between its 1 and its 9.",
    [VAR_KIDS] = "4x4 for kids, with big digits: rows, columns and 2x2 boxes hold 1-4 once each. Easy or Medium.",
    [VAR_KING] = "9x9, and two cells that touch, also corner to corner, never hold the same digit.",
    [VAR_NONCONSEC] = "9x9: two side-by-side cells never hold digits 1 apart (no 4 next to a 5).",
    [VAR_DISJOINT] = "9x9, and the cells in the same place in each 3x3 box also hold 1-9 once each.",
    [VAR_ASTERISK] = "9x9, and the nine tinted cells of the asterisk also hold 1-9 once each.",
    [VAR_KROPKI] = "9x9: a white dot joins digits 1 apart, a black dot a digit and its double; no dot, neither.",
    [VAR_XV] = "9x9: X joins two cells that add up to 10, V two that add up to 5; no mark, neither.",
    [VAR_GT] = "9x9: in each box, the signs between side-by-side cells say which digit is bigger.",
    [VAR_QUAD] = "9x9: a circle where four cells meet lists digits that are among those four cells.",
    [VAR_ARROW] = "9x9 with arrows: the digits along each shaft add up to the digit in its circle.",
    [VAR_RENBAN] = "9x9 with purple lines: each line's digits are a run of consecutive digits, in any order.",
    [VAR_WHISPER] = "9x9 with green lines (German Whispers): neighbours along a line differ by 5 or more.",
    [VAR_SKY] = "9x9: a clue by a row or column is how many buildings are seen from there (taller ones hide shorter).",
    [VAR_LK] = "9x9: a clue with an arrow outside the grid is the sum of the digits along that diagonal.",
    [VAR_PALINDROME] = "9x9 with grey lines: each line reads the same from both ends.",
    [VAR_REGIONSUM] = "9x9 with blue lines: a line's digits add up to the same in each box it passes through.",
    [VAR_XSUM] = "9x9: a clue by a row or column is the sum of its first X digits, X being the first digit.",
    [VAR_ZIPPER] = "9x9 with pink lines: cells the same distance from a line's middle add up to the same total.",
    [VAR_FRAME] = "9x9: a clue by a row or column is the sum of the first three digits from there.",
    [VAR_BETWEEN] = "9x9: the digits on a line between two circles lie between the circles' digits.",
    [VAR_ENTROPIC] = "9x9 with orange lines: any three cells in a row on one hold a low, a middle and a high digit.",
    [VAR_MODULAR] = "9x9 with teal lines: any three cells in a row on one hold one each of 147, 258 and 369.",
    [VAR_CENTERDOT] = "9x9, and the nine tinted box centres also hold 1-9 once each.",
    [VAR_OUTSIDE] = "9x9: a clue by a row or column lists digits that are among its first three cells.",
    [VAR_ROSSINI] = "9x9: an arrow by a row or column: its first three digits rise that way; no arrow: they don't.",
    [VAR_DIAGNC] = "9x9: two cells that touch at a corner never hold digits 1 apart.",
    [VAR_DUTCH] = "9x9 with amber lines (Dutch Whispers): neighbours along a line differ by 4 or more.",
    [VAR_PARITY] = "9x9 with red lines: neighbours along a line are one odd and one even digit.",
    [VAR_NABNER] = "9x9 with brown lines: no two digits on a line are the same or 1 apart.",
    [VAR_NROOMS] = "9x9 with clues outside: a clue is the digit in the X-th cell, X being the first digit.",
    [VAR_SKYSUM] = "9x9 with clues outside: the sum of the buildings seen from there (taller ones hide the rest).",
    [VAR_LOCKOUT] = "9x9 with lines: the end diamonds are 4+ apart, the digits between outside their range.",
    [VAR_SUMLESS] = "9x9 with cages (dashed) without sums: no digit repeats in a cage.",
    [VAR_DARROW] = "9x9 with blue lines: the two end circles add up to the digits between them.",
    [VAR_DIAGCONSEC] = "9x9 with corner marks: marked corner neighbours are 1 apart, unmarked ones aren't.",
    [VAR_TEN] = "9x9 with green lines: each splits into runs of cells that add up to 10.",
    [VAR_GIRANDOLA] = "9x9, and the nine tinted pinwheel cells also hold 1-9 once each.",
    [VAR_CHESS] = "9x9: no repeat a knight's move apart, nor in cells that touch (Anti-Knight and Anti-King).",
    [VAR_MAGIC] = "9x9, and the tinted centre box is a magic square: its lines add up to 15.",
    [VAR_AVG] = "9x9 with teal lines: the circled digit is the average of the line's other digits.",
    [VAR_ANTIDIAG] = "9x9, and each tinted long diagonal holds at most three different digits.",
    [VAR_FLATMATES] = "9x9: every 5 has a 1 right above it or a 9 right below it (or both).",
    [VAR_MATHRAX] = "9x9 with circles: both diagonal pairs of the four cells make the clue (9+, 2-, 12x, 3/, E, O).",
    [VAR_PILL] = "9x9 with arrows: the two cells in a pill, read as a number, are the sum of the shaft.",
    [VAR_PSKY] = "9x9 with clues outside: a ringed clue counts the odd buildings seen, a shaded one the even.",
    [VAR_159] = "9x9: in each row, the digits in columns 1, 5 and 9 say where its 1, 5 and 9 are.",
    [VAR_PRODUCT] = "9x9 with cages (dashed): a cage's digits multiply to its number, none repeated. Up to Hard.",
    [VAR_REGIONS] = "9x9 with a few shaded regions: no digit repeats in a region.",
    [VAR_SLOW] = "9x9 with thermometers: the digits rise from the bulb, or stay the same where they may.",
    [VAR_FORTRESS] = "9x9: each grey cell holds a bigger digit than every white cell side by side with it.",
    [VAR_ARGYLE] = "9x9, and no digit repeats along any of the eight diagonal lines of the argyle pattern.",
};
static lv_obj_t *gen_lvl_btn[GEN_LEVELS];          /**< the level buttons (Kids 4x4: Hard and Expert off) */

/** @brief Hard and Expert need techniques a 4x4 board never calls for: off for Kids 4x4 */
static void gen_levels_update(void)
{
    for (int lv = 0; lv < GEN_LEVELS; lv++) {
        if (gen_lvl_btn[lv]) {
            lv_obj_set_state(gen_lvl_btn[lv], LV_STATE_DISABLED, lv > (int)sudoku_top_level(gen_pick_var));
        }
    }
}

/**
 * @brief Closes New puzzle's dialog.
 */
static void gen_pick_close(void)
{
    if (gen_pick) {
        lv_msgbox_close_async(gen_pick);
    }
    gen_pick = NULL;
}

/**
 * @brief A level picked: makes the puzzle.
 * @param[in] e  the event (user data: the level)
 */
static void gen_level_cb(lv_event_t *e)
{
    int level = (int)(intptr_t)lv_event_get_user_data(e);
    gen_pick_close();
    gen_start(gen_pick_var, level);
}

/**
 * @brief A variant picked: the levels it has.
 * @param[in] e  the event
 */
static void gen_var_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target_obj(e);
    uint32_t sel = lv_buttonmatrix_get_selected_button(bm);
    if (sel == LV_BUTTONMATRIX_BUTTON_NONE || sel >= GEN_CAT_SLOTS || GEN_CAT_VARS[gen_pick_cat][sel] == GEN_CAT_END) {
        return;
    }
    gen_pick_var = GEN_CAT_VARS[gen_pick_cat][sel];
    gen_cat_pick[gen_pick_cat] = (uint8_t)gen_pick_var;
    lv_label_set_text(gen_pick_desc, GEN_VAR_DESC[gen_pick_var]);
    gen_levels_update();
}

/**
 * @brief Shows a tab's variants in the variant buttons, with its last pick checked (and chosen).
 * @param[in] cat  the tab
 */
static void gen_cat_show(int cat)
{
    static const char *map[GEN_CAT_SLOTS + GEN_CAT_SLOTS / 4];   /* the labels, a "\n" after each row of four */
    int k = 0, n = 0;
    while (n < GEN_CAT_SLOTS && GEN_CAT_VARS[cat][n] != GEN_CAT_END) {
        n++;
    }
    for (int i = 0; i < GEN_CAT_SLOTS; i++) {
        map[k++] = i < n ? GEN_VAR_LABEL[GEN_CAT_VARS[cat][i]] : " ";   /* (" ": a slot kept, hidden below) */
        if (i % 4 == 3 && i + 1 < GEN_CAT_SLOTS) {
            map[k++] = "\n";
        }
    }
    map[k] = "";
    gen_pick_cat = cat;
    gen_pick_var = gen_cat_pick[cat];
    lv_buttonmatrix_set_map(gen_var_bm, map);       /* (the same count: LVGL keeps the old flags) */
    lv_buttonmatrix_clear_button_ctrl_all(gen_var_bm, LV_BUTTONMATRIX_CTRL_HIDDEN | LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_buttonmatrix_set_button_ctrl_all(gen_var_bm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    for (int i = 0; i < GEN_CAT_SLOTS; i++) {
        if (i >= n) {
            lv_buttonmatrix_set_button_ctrl(gen_var_bm, (uint32_t)i, LV_BUTTONMATRIX_CTRL_HIDDEN);
        } else if (GEN_CAT_VARS[cat][i] == gen_pick_var) {
            lv_buttonmatrix_set_button_ctrl(gen_var_bm, (uint32_t)i, LV_BUTTONMATRIX_CTRL_CHECKED);
        }
    }
    lv_label_set_text(gen_pick_desc, GEN_VAR_DESC[gen_pick_var]);
    gen_levels_update();
}

/**
 * @brief A tab tapped: its variants shown.
 * @param[in] e  the event (the tabs' button matrix)
 */
static void gen_cat_cb(lv_event_t *e)
{
    uint32_t sel = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (sel < GEN_CATS && (int)sel != gen_pick_cat) {
        gen_cat_show((int)sel);
    }
}

/**
 * @brief Cancel.
 * @param[in] e  the event
 */
static void gen_cancel_cb(lv_event_t *e)
{
    gen_pick_close();
}

/**
 * @brief A tap on the dimmed background (not on the dialog) closes it too.
 * @param[in] e  the event
 */
static void gen_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) {
        gen_pick_close();
    }
}

/* (documented in ui_internal.h) */
void new_puzzle_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("New puzzle", NULL);
    gen_pick = m;
    lv_obj_set_width(m, 600);
    make_wrap_text(lv_msgbox_get_content(m), &lv_font_montserrat_16, C_TEXT,
                   "A new puzzle made on the board, with exactly one solution. Easy and Medium need only "
                   "singles; Hard needs pairs or locked candidates; Expert needs more than that.");

    /* the tabs (Grids, Marks, Lines, Clues), then the tab's variants (one checked): rows of four */
    static const char *const CAT_MAP[] = {"Grids", "Marks", "Lines", "Clues", ""};
    for (int c = 0; c < GEN_CATS; c++) {            /* the tab of the variant picked last */
        for (int i = 0; GEN_CAT_VARS[c][i] != GEN_CAT_END; i++) {
            if (GEN_CAT_VARS[c][i] == gen_pick_var) {
                gen_pick_cat = c;
                gen_cat_pick[c] = (uint8_t)gen_pick_var;
            }
        }
    }
    lv_obj_t *tabs = make_tab_strip(lv_msgbox_get_content(m), CAT_MAP, gen_pick_cat);
    lv_obj_add_event_cb(tabs, gen_cat_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *vm = lv_buttonmatrix_create(lv_msgbox_get_content(m));
    gen_var_bm = vm;
    lv_buttonmatrix_set_one_checked(vm, true);
    lv_obj_set_size(vm, LV_PCT(100), 252);          /* five rows (44 px each) */
    lv_obj_set_style_pad_row(vm, 8, 0);
    lv_obj_set_style_pad_all(vm, 0, 0);
    lv_obj_set_style_pad_column(vm, 8, 0);
    lv_obj_set_style_margin_top(vm, 14, 0);
    lv_obj_set_style_border_width(vm, 0, 0);
    lv_obj_set_style_bg_opa(vm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(vm, &lv_font_montserrat_16, 0);   /* (four across: "Consecutive" fits) */
    lv_obj_set_style_bg_color(vm, C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_text_color(vm, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(vm, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(vm, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(vm, gen_var_cb, LV_EVENT_VALUE_CHANGED, NULL);
    gen_pick_desc = make_wrap_text(lv_msgbox_get_content(m), &lv_font_montserrat_16, C_MUTED,
                                   GEN_VAR_DESC[gen_pick_var]);
    lv_obj_set_style_margin_top(gen_pick_desc, 8, 0);

    /* the four levels: one row of buttons, a figure growing up from Easy to Expert (the
     * players' avatar emoji, avatars.h) over the level's name */
    static const uint16_t LEVEL_AVATAR[GEN_LEVELS] = {
        225,                                        /* baby: Easy (avatars_order.txt line 226) */
        226,                                        /* child: Medium */
        231,                                        /* man: Hard */
        345,                                        /* prince (the king): Expert */
    };
    lv_obj_t *grid = lv_obj_create(lv_msgbox_get_content(m));
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_style_pad_top(grid, 12, 0);
    for (int lv = 0; lv < GEN_LEVELS; lv++) {
        lv_obj_t *b = make_btn(grid, 0, 0, 0, 72, NULL, NULL, gen_level_cb, (void *)(intptr_t)lv);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_bg_color(b, C_CARD2, 0);
        lv_obj_t *img = lv_image_create(b);
        lv_image_set_src(img, avatar_image(LEVEL_AVATAR[lv]));
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 7);
        lv_obj_t *name = lv_label_create(b);
        lv_label_set_text(name, gen_level_name(lv));
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name, C_TEXT, 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);   /* (Kids 4x4: no Hard, Expert) */
        gen_lvl_btn[lv] = b;
    }
    gen_cat_show(gen_pick_cat);                     /* (the tab's variants, its description, the levels) */

    /* closing: an X in the title bar (or a tap beside the dialog), no Cancel row */
    lv_obj_t *x = lv_msgbox_add_header_button(m, LV_SYMBOL_CLOSE);
    lv_obj_set_size(x, 52, 40);
    lv_obj_set_style_bg_color(x, C_CARD2, 0);
    lv_obj_set_style_text_color(x, C_TEXT, 0);
    lv_obj_set_style_radius(x, 8, 0);
    lv_obj_set_style_shadow_width(x, 0, 0);
    lv_obj_add_event_cb(x, gen_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *head = lv_msgbox_get_header(m);
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(head, 6, 0);
    lv_obj_t *backdrop = lv_obj_get_parent(m);         /* the modal's dimmed layer */
    if (backdrop && backdrop != lv_layer_top()) {
        lv_obj_add_event_cb(backdrop, gen_backdrop_cb, LV_EVENT_CLICKED, NULL);
    }
}
