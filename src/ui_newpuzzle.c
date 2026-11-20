/* sudoku_ui.c split: New puzzle (the generator), the daily puzzle, Learn, Enter a puzzle. See sudoku_ui.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void gen_task(void *arg);
static void gen_poll_cb(lv_timer_t *t);
static void gen_start_ex(int variant, int level, uint32_t seed, const char *day);
static bool daily_today_v(char day[12], int *level, int *variant);
static uint32_t daily_seed(const char *day);
static void gen_levels_update(void);
static void gen_pick_close(void);
static void gen_level_cb(lv_event_t *e);
static void gen_var_cb(lv_event_t *e);
static void gen_cancel_cb(lv_event_t *e);
static void gen_backdrop_cb(lv_event_t *e);
static void new_puzzle_cb(lv_event_t *e);
static int learn_variant(int target);
static int learn_variant_tech(int target);
static int learn_tech(int target);
static bool learn_match(int target, const sudoku_step_t *st);
static bool learn_find_naked(const uint8_t vals[81], const uint8_t sol[81], sudoku_step_t *st);
static int learn_variant_find(int target);
static void learn_task(void *arg);
static void learn_poll_cb(lv_timer_t *t);
static void learn_pick_cb(lv_event_t *e);
static void learn_close_cb(lv_event_t *e);
static void learn_cb(lv_event_t *e);
static void ed_refresh(void);
static void ed_cell_cb(lv_event_t *e);
static void ed_digit_cb(lv_event_t *e);
static void ed_close(void);
static void ed_cancel_cb(lv_event_t *e);
static void ed_erase_cb(lv_event_t *e);
static void ed_clear_cb(lv_event_t *e);
static void ed_check_task(void *arg);
static void ed_check_done(lv_timer_t *t);
static void ed_play_cb(lv_event_t *e);
static void enter_cb(lv_event_t *e);

/* ------------------------------------------------------------- new puzzle (generator) */
/*
 * New puzzle: pick a level, a worker task runs the generator (generator.c)
 * while a dialog says so, then the puzzle is stored (sudoku.db, or RAM
 * without a card) and opened like any other.
 */
static lv_obj_t *gen_box;                           /* the "Generating..." dialog */
static lv_timer_t *gen_timer;
static volatile bool gen_done;
static int gen_var_req;                             /* variant being made (generator.h) */
static int gen_level_req, gen_level_got;
static uint8_t gen_givens[81];
static sudoku_geom_t *gen_board;                    /* the puzzle's board (Jigsaw regions, Killer cages), PSRAM */
static char *gen_cages;                             /* STATS_CAGES_LEN, PSRAM */
static char gen_marks[82];
static char *gen_thermos;                           /* STATS_THERMOS_LEN, PSRAM */
static uint32_t gen_seed;                           /* 0 = random */
static char gen_day[12];                            /* making a daily puzzle: its date */

static void gen_task(void *arg)
{
    gen_level_t got = (gen_level_t)gen_level_req;
    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
    sudoku_generate(gen_var_req, (gen_level_t)gen_level_req, gen_seed ? gen_seed : esp_random(), gen_givens,
                    gen_board, &got);
    gen_cages[0] = 0;
    if (gen_var_req == VAR_KILLER) sudoku_cages_text(gen_board, gen_cages, STATS_CAGES_LEN);
    gen_marks[0] = 0;
    if (gen_var_req == VAR_EVENODD || gen_var_req == VAR_CONSEC) sudoku_marks_text(gen_board, gen_marks, 82);
    if (gen_var_req == VAR_SANDWICH) sudoku_sandwich_text(gen_board, gen_marks, 82);
    gen_thermos[0] = 0;
    if (gen_var_req == VAR_THERMO) sudoku_thermos_text(gen_board, gen_thermos, STATS_THERMOS_LEN);
    int clues = 0;
    for (int i = 0; i < 81; i++) clues += gen_givens[i] != 0;
    printf("generator: %s %s asked, %s made, %d clues, %u ms\n", VARIANT_NAMES[gen_var_req],
           GEN_LEVEL_NAMES[gen_level_req], GEN_LEVEL_NAMES[got], clues,
           (unsigned)(esp_timer_get_time() / 1000 - t0));
    gen_level_got = got;
    gen_done = true;
    vTaskDelete(NULL);
}

static void gen_poll_cb(lv_timer_t *t)
{
    if (!gen_done) return;
    lv_timer_delete(gen_timer);
    gen_timer = NULL;
    if (gen_box) lv_msgbox_close(gen_box);
    gen_box = NULL;
    uint16_t id;
    const uint8_t *regions = gen_var_req == VAR_JIGSAW ? gen_board->region : NULL;
    const char *cages = gen_var_req == VAR_KILLER ? gen_cages : NULL;
    const char *marks = gen_marks[0] ? gen_marks : NULL;
    const char *thermos = gen_thermos[0] ? gen_thermos : NULL;
    int gid = stats_db_ready() ? stats_db_gen_add(gen_var_req, gen_level_got, gen_givens, regions, cages, marks,
                                                  thermos, gen_day[0] ? gen_day : NULL)
                               : -1;
    if (gid > 0 && gid < PUZZLE_GEN_RAM_BASE - PUZZLE_GEN_BASE) {
        id = (uint16_t)(PUZZLE_GEN_BASE + gid);
    } else {
        id = s_gen_ram_next++;                       /* no card: until power-off */
        if (s_gen_ram_next == 0xFFFF) s_gen_ram_next = PUZZLE_GEN_RAM_BASE;
    }
    gen_cache_put(id, gen_var_req, gen_level_got, gen_givens, regions, cages, marks, thermos, gen_day);
    if (gen_level_got != gen_level_req && !gen_day[0]) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Couldn't make a %s %s quickly; this one is %s.", GEN_LEVEL_NAMES[gen_level_req],
                 gen_var_req == VAR_CLASSIC ? "one" : VARIANT_NAMES[gen_var_req], GEN_LEVEL_NAMES[gen_level_got]);
        sudoku_ui_notice(msg, 4000);
    }
    open_puzzle(id);
}

/* seed 0 = a random puzzle; a daily one (always classic) has its date as the seed and day set */
static lv_timer_t *learn_timer;                     /* (Learn: its variant lessons use the generator too) */

static void gen_start_ex(int variant, int level, uint32_t seed, const char *day)
{
    if (gen_timer || learn_timer) return;            /* one at a time */
    if (!gen_board && !(gen_board = heap_caps_malloc(sizeof(*gen_board), MALLOC_CAP_SPIRAM))) return;
    if (!gen_cages && !(gen_cages = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) return;
    if (!gen_thermos && !(gen_thermos = heap_caps_malloc(STATS_THERMOS_LEN, MALLOC_CAP_SPIRAM))) return;
    gen_var_req = variant >= 0 && variant < VAR_COUNT ? variant : VAR_CLASSIC;
    gen_level_req = level;
    gen_seed = seed;
    snprintf(gen_day, sizeof(gen_day), "%s", day ? day : "");
    gen_done = false;
    char text[80];
    if (day) {
        char kind[32];
        daily_kind_text(gen_var_req, level, kind, sizeof(kind));
        snprintf(text, sizeof(text), "Making today's %s puzzle...", kind);
    }
    else if (gen_var_req == VAR_CLASSIC) snprintf(text, sizeof(text), "Making a new %s puzzle...", GEN_LEVEL_NAMES[level]);
    else snprintf(text, sizeof(text), "Making a new %s %s puzzle...", GEN_LEVEL_NAMES[level], VARIANT_NAMES[gen_var_req]);
    gen_box = make_msgbox(LV_SYMBOL_REFRESH "  New puzzle", text);
    if (xTaskCreatePinnedToCore(gen_task, "gen", 16384, NULL, 2, NULL, WORKER_CORE) != pdPASS) {
        lv_msgbox_close(gen_box);
        gen_box = NULL;
        sudoku_ui_notice("Couldn't start the generator.", 3000);
        return;
    }
    gen_timer = lv_timer_create(gen_poll_cb, 50, NULL);
}

void gen_start(int variant, int level)
{
    gen_start_ex(variant, level, 0, NULL);
}

/* ------------------------------------------------------------- daily puzzle */
/*
 * One puzzle a day, the same for every player: made from the date (its seed),
 * with a variant and level by weekday (DAILY_*): Mon Classic Easy, Tue Mini
 * Medium, Wed X-Sudoku Medium, Thu Hyper Hard, Fri Killer Medium, Sat Jigsaw
 * Hard, Sun Classic Expert. Stored like any generated puzzle, with its date
 * (generated.day): a day's puzzle, once made, stays what it is. The streak
 * counts days in a row with the daily solved (stats_db).
 */
static const uint8_t DAILY_LEVEL[7] = {GEN_EXPERT, GEN_EASY, GEN_MEDIUM, GEN_MEDIUM, GEN_HARD, GEN_MEDIUM, GEN_HARD};
static const uint8_t DAILY_VARIANT[7] = {VAR_CLASSIC, VAR_CLASSIC, VAR_MINI, VAR_X, VAR_HYPER, VAR_KILLER, VAR_JIGSAW};

/* today's date, level and variant; false while the clock isn't set */
static bool daily_today_v(char day[12], int *level, int *variant)
{
    time_t now = time(NULL);
    if (!CLOCK_VALID(now)) return false;
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
    *level = DAILY_LEVEL[tm.tm_wday];
    if (variant) *variant = DAILY_VARIANT[tm.tm_wday];
    return true;
}

bool daily_today(char day[12], int *level, int *wday)
{
    (void)wday;
    return daily_today_v(day, level, NULL);
}

/* "Hard", "X-Sudoku Medium": a daily puzzle's level, with its variant unless Classic */
void daily_kind_text(int variant, int level, char *buf, size_t n)
{
    const char *lv = level >= 0 && level < GEN_LEVELS ? GEN_LEVEL_NAMES[level] : "";
    if (variant > VAR_CLASSIC && variant < VAR_COUNT) snprintf(buf, n, "%s %s", VARIANT_NAMES[variant], lv);
    else snprintf(buf, n, "%s", lv);
}

static uint32_t daily_seed(const char *day)          /* FNV-1a of the date */
{
    uint32_t h = 2166136261u;
    for (const char *p = day; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return h ? h : 1;
}

/* the Daily puzzle line on the home screen */
void daily_sub_text(char *buf, size_t n)
{
    char day[12], kind[32];
    int level, variant, id, streak;
    bool solved;
    if (!daily_today_v(day, &level, &variant)) {
        snprintf(buf, n, "Needs the date: connect Wi-Fi in Settings");
        return;
    }
    if (!stats_db_daily_status(day, &id, &solved, &streak)) {
        daily_kind_text(variant, level, kind, sizeof(kind));
        snprintf(buf, n, "Today: %s", kind);
        return;
    }
    if (id) gen_lookup((uint16_t)(PUZZLE_GEN_BASE + id), NULL, &variant, &level);   /* as it was made */
    daily_kind_text(variant, level, kind, sizeof(kind));
    if (solved) snprintf(buf, n, LV_SYMBOL_OK "  Solved today  -  streak %d", streak);
    else if (streak) snprintf(buf, n, "Today: %s  -  streak %d", kind, streak);
    else snprintf(buf, n, "Today: %s", kind);
}

void daily_cb(lv_event_t *e)
{
    char day[12];
    int level, variant, id = 0, streak = 0;
    bool solved = false;
    if (!daily_today_v(day, &level, &variant)) {
        sudoku_ui_notice("The daily puzzle needs today's date: connect Wi-Fi (Settings) so the clock is set.", 4000);
        return;
    }
    stats_db_daily_status(day, &id, &solved, &streak);
    if (!id) {                                        /* RAM-only (no card): look in the cache */
        for (int i = 0; i < GEN_CACHE; i++) {
            if (s_gen_cache[i].id && !strcmp(s_gen_cache[i].day, day)) id = s_gen_cache[i].id - PUZZLE_GEN_BASE;
        }
    }
    if (id) {
        if (solved) sudoku_ui_notice("You've solved today's puzzle; playing it again doesn't change the streak.", 3500);
        open_puzzle((uint16_t)(PUZZLE_GEN_BASE + id));
        return;
    }
    gen_start_ex(variant, level, daily_seed(day), day);
}

static lv_obj_t *gen_pick;                          /* the level dialog */
static lv_obj_t *gen_pick_desc;                     /* its line about the variant chosen */
static int gen_pick_var = VAR_CLASSIC;              /* variant chosen there (kept until power-off) */

/* the variants in the New puzzle dialog, in button order */
static const uint8_t GEN_VAR_ORDER[VAR_COUNT] = {VAR_CLASSIC, VAR_X, VAR_HYPER, VAR_JIGSAW, VAR_KILLER, VAR_MINI,
                                                 VAR_KNIGHT, VAR_EVENODD, VAR_CONSEC, VAR_THERMO, VAR_SANDWICH,
                                                 VAR_KIDS};
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
};
static lv_obj_t *gen_lvl_btn[GEN_LEVELS];          /* the level buttons (Kids 4x4: Hard and Expert off) */

/* Hard and Expert need techniques a 4x4 board never calls for: off for Kids 4x4 */
static void gen_levels_update(void)
{
    for (int lv = 0; lv < GEN_LEVELS; lv++) {
        if (gen_lvl_btn[lv]) lv_obj_set_state(gen_lvl_btn[lv], LV_STATE_DISABLED, gen_pick_var == VAR_KIDS && lv >= GEN_HARD);
    }
}

static void gen_pick_close(void)
{
    if (gen_pick) lv_msgbox_close_async(gen_pick);
    gen_pick = NULL;
}

static void gen_level_cb(lv_event_t *e)
{
    int level = (int)(intptr_t)lv_event_get_user_data(e);
    gen_pick_close();
    gen_start(gen_pick_var, level);
}

static void gen_var_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target_obj(e);
    uint32_t sel = lv_buttonmatrix_get_selected_button(bm);
    if (sel == LV_BUTTONMATRIX_BUTTON_NONE || sel >= VAR_COUNT) return;
    gen_pick_var = GEN_VAR_ORDER[sel];
    lv_label_set_text(gen_pick_desc, GEN_VAR_DESC[gen_pick_var]);
    gen_levels_update();
}

static void gen_cancel_cb(lv_event_t *e)
{
    gen_pick_close();
}

/* a tap on the dimmed background (not on the dialog) closes it too */
static void gen_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) gen_pick_close();
}

static void new_puzzle_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("New puzzle", NULL);
    gen_pick = m;
    lv_obj_set_width(m, 600);
    make_wrap_text(lv_msgbox_get_content(m), NULL, C_TEXT,
                   "A new puzzle made on the board, with exactly one solution. Easy and Medium need only "
                   "singles; Hard needs pairs or locked candidates; Expert needs more than that.");

    /* the variant: Classic, X-Sudoku or Mini 6x6 (one of them checked) */
    static const char *const VAR_MAP[] = {"Classic", "X-Sudoku", "Hyper", "\n", "Jigsaw", "Killer", "Mini 6x6", "\n",
                                          "Anti-Knight", "Even-Odd", "Consecutive", "\n", "Thermo", "Sandwich", "Kids 4x4", ""};
    lv_obj_t *vm = lv_buttonmatrix_create(lv_msgbox_get_content(m));
    lv_buttonmatrix_set_map(vm, VAR_MAP);
    lv_buttonmatrix_set_button_ctrl_all(vm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(vm, true);
    for (uint32_t i = 0; i < VAR_COUNT; i++) {
        if (GEN_VAR_ORDER[i] == gen_pick_var) lv_buttonmatrix_set_button_ctrl(vm, i, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    lv_obj_set_size(vm, LV_PCT(100), 226);          /* rows of three */
    lv_obj_set_style_pad_row(vm, 8, 0);
    lv_obj_set_style_pad_all(vm, 0, 0);
    lv_obj_set_style_pad_column(vm, 8, 0);
    lv_obj_set_style_margin_top(vm, 14, 0);
    lv_obj_set_style_border_width(vm, 0, 0);
    lv_obj_set_style_bg_opa(vm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(vm, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_color(vm, C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_text_color(vm, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(vm, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(vm, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(vm, gen_var_cb, LV_EVENT_VALUE_CHANGED, NULL);
    gen_pick_desc = make_wrap_text(lv_msgbox_get_content(m), &lv_font_montserrat_16, C_MUTED,
                                   GEN_VAR_DESC[gen_pick_var]);
    lv_obj_set_style_margin_top(gen_pick_desc, 8, 0);

    /* the four levels: big buttons in two rows */
    lv_obj_t *grid = lv_obj_create(lv_msgbox_get_content(m));
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_top(grid, 16, 0);
    for (int lv = 0; lv < GEN_LEVELS; lv++) {
        /* a star per step: Easy one, Expert four */
        char label[48] = "";
        for (int k = 0; k <= lv; k++) strcat(label, SYMBOL_STAR);
        strcat(label, "  ");
        strcat(label, GEN_LEVEL_NAMES[lv]);
        lv_obj_t *b = make_btn(grid, 0, 0, 0, 60, label, &font_icons_24, gen_level_cb, (void *)(intptr_t)lv);
        lv_obj_set_width(b, LV_PCT(48));
        lv_obj_set_style_bg_color(b, C_ACCENT, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), lv_color_white(), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_30, LV_STATE_DISABLED);
        gen_lvl_btn[lv] = b;
    }
    gen_levels_update();

    msgbox_button(m, "Cancel", gen_cancel_cb, false);
    lv_obj_t *backdrop = lv_obj_get_parent(m);         /* the modal's dimmed layer */
    if (backdrop && backdrop != lv_layer_top()) lv_obj_add_event_cb(backdrop, gen_backdrop_cb, LV_EVENT_CLICKED, NULL);
}

/* ------------------------------------------------------------- learn (practice a technique) */
/*
 * Learn (Puzzles screen): pick a technique; a worker task walks built-in
 * puzzles forward with the hint engine (sudoku_next_step) until the next
 * step needs that technique, and the game opens at that point as a practice
 * game, with the hint card on the technique. Practice games aren't saved,
 * recorded or counted.
 */
static const char *const LEARN_DESC[LEARN_COUNT] = {
    "A digit has only one place left in a row, column or box.",
    "A cell has only one digit left that fits.",
    "In a box, a digit's places all lie on one line: the rest of that line can't have it.",
    "On a line, a digit's places all lie in one box: the rest of that box can't have it.",
    "Two cells of a unit can only be the same two digits: no other cell there can be either.",
    "Two digits fit only the same two cells of a unit: those cells can't be anything else.",
    "Three cells of a unit can only be the same three digits between them: no other cell there can.",
    "In two rows a digit fits only the same two columns: it can't be elsewhere in those columns.",
    "The same with three rows and three columns (or three columns and three rows).",
    "A cell that is x or y sees an x-or-z and a y-or-z cell: one of those is z, so what sees both isn't.",
    "A cage's sum allows only some sets of digits: with what's around it, its cells narrow down.",
    "Digits rise from the bulb: each cell lies between what the cells before and after it can be.",
    "The sum between a row's 1 and 9 leaves the 1 and the 9 only a few places in that row or column.",
    "A bar: the two cells differ by 1. No bar: they don't. Both narrow down a cell's digits.",
};

/* the variant lessons: a new puzzle of that variant (made on the spot, kept in RAM only) */
static int learn_variant(int target)
{
    switch (target) {
    case LEARN_CAGE:     return VAR_KILLER;
    case LEARN_THERMO:   return VAR_THERMO;
    case LEARN_SANDWICH: return VAR_SANDWICH;
    case LEARN_CONSEC:   return VAR_CONSEC;
    }
    return -1;
}

static int learn_variant_tech(int target)
{
    switch (target) {
    case LEARN_CAGE:     return TECH_CAGE;
    case LEARN_THERMO:   return TECH_THERMO;
    case LEARN_SANDWICH: return TECH_SANDWICH;
    case LEARN_CONSEC:   return TECH_CONSEC;
    }
    return TECH_NONE;
}

/* the techniques that sudoku_step_with() finds a lesson position for (the newer, rarer ones:
 * the hint engine seldom reaches them first, so any position where they open a single will do) */
static int learn_tech(int target)
{
    switch (target) {
    case LEARN_NAKED_TRIPLE: return TECH_NAKED_TRIPLE;
    case LEARN_X_WING:       return TECH_X_WING;
    case LEARN_SWORDFISH:    return TECH_SWORDFISH;
    case LEARN_XY_WING:      return TECH_XY_WING;
    }
    return TECH_NONE;
}

static lv_obj_t *learn_box, *learn_wait;
static volatile int learn_state;                    /* 0 searching, 1 found, 2 nothing found */
static int learn_target;
static uint16_t learn_id;
static uint8_t learn_vals[81];
static sudoku_step_t learn_step;

/* does this step need the technique (for a naked single: a cell with one candidate is enough) */
static bool learn_match(int target, const sudoku_step_t *st)
{
    switch (target) {
    case LEARN_HIDDEN: return st->kind == STEP_HIDDEN_SINGLE && !st->tech.kind;
    case LEARN_NAKED: return st->kind == STEP_NAKED_SINGLE && !st->tech.kind;
    case LEARN_POINTING: return st->tech.kind == TECH_POINTING;
    case LEARN_CLAIMING: return st->tech.kind == TECH_CLAIMING;
    case LEARN_NAKED_PAIR: return st->tech.kind == TECH_NAKED_PAIR;
    case LEARN_HIDDEN_PAIR: return st->tech.kind == TECH_HIDDEN_PAIR;
    }
    return false;
}

/* a naked single on the board: a cell only one digit fits (singles are found
 * as hidden ones first, so the hint engine seldom names this one itself) */
static bool learn_find_naked(const uint8_t vals[81], const uint8_t sol[81], sudoku_step_t *st)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    for (int c = 0; c < 81; c++) {
        if (vals[c]) continue;
        uint16_t m = 0x3FE;
        for (int p = 0; p < G->npeers[c]; p++) m &= (uint16_t)~(1u << vals[G->peers[c][p]]);
        if (__builtin_popcount(m) == 1 && __builtin_ctz(m) == sol[c]) {
            memset(st, 0, sizeof(*st));
            st->kind = STEP_NAKED_SINGLE;
            st->cell = c;
            st->digit = sol[c];
            st->unit = -1;
            return true;
        }
    }
    return false;
}

/* a variant lesson's puzzle, for learn_poll_cb to put in the RAM cache */
static uint8_t learn_givens[81];
static sudoku_geom_t *learn_board;                  /* PSRAM */
static char *learn_cages, *learn_thermos;           /* PSRAM */
static char learn_marks[82];

static int learn_variant_find(int target)
{
    static uint8_t vals[81], sol[81];
    const int var = learn_variant(target), tech = learn_variant_tech(target);
    for (int tries = 0; tries < 6; tries++) {      /* most puzzles have one early; a few seconds each at most */
        gen_level_t got;
        if (!sudoku_generate(var, GEN_MEDIUM, esp_random(), learn_givens, learn_board, &got)) continue;
        if (!sudoku_solve(learn_board, learn_givens, sol)) continue;
        memcpy(vals, learn_givens, 81);
        for (int step = 0; step < 81; step++) {
            sudoku_step_t st;
            if (!sudoku_next_step(learn_board, vals, sol, &st) || st.cell < 0) break;
            if (st.tech.kind == tech) {
                memcpy(learn_vals, vals, 81);
                learn_step = st;
                learn_cages[0] = learn_marks[0] = learn_thermos[0] = 0;
                if (var == VAR_KILLER) sudoku_cages_text(learn_board, learn_cages, STATS_CAGES_LEN);
                if (var == VAR_CONSEC) sudoku_marks_text(learn_board, learn_marks, sizeof(learn_marks));
                if (var == VAR_SANDWICH) sudoku_sandwich_text(learn_board, learn_marks, sizeof(learn_marks));
                if (var == VAR_THERMO) sudoku_thermos_text(learn_board, learn_thermos, STATS_THERMOS_LEN);
                return 1;
            }
            vals[st.cell] = (uint8_t)st.digit;      /* on to the next step */
        }
    }
    return 2;
}

static void learn_task(void *arg)
{
    static uint8_t vals[81], sol[81];
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    const int target = learn_target;
    if (learn_variant(target) >= 0) {
        learn_state = learn_variant_find(target);
        vTaskDelete(NULL);
        return;
    }
    /* the harder collections first for the harder techniques */
    uint32_t r = esp_random();
    int found = 0;
    for (int tries = 0; tries < 2000 && !found; tries++) {     /* (pairs are the rarest: ~200 puzzles) */
        int c = (int)((r + (uint32_t)tries * 7) % PUZZLE_COLLECTION_COUNT);
        if (target >= LEARN_POINTING && strstr(puzzle_collections[c].name, "Easy")) continue;
        uint16_t id = (uint16_t)(puzzle_collections[c].first + (esp_random() % puzzle_collections[c].count));
        puzzle_load(id, vals);
        if (!sudoku_solve(G, vals, sol)) continue;
        sudoku_step_t st, w;
        const int tech = learn_tech(target);
        for (int step = 0; step < 81 && !found; step++) {
            if (target == LEARN_NAKED && step >= 8 && learn_find_naked(vals, sol, &st)) found = 1;
            else if (!sudoku_next_step(G, vals, sol, &st) || st.cell < 0) break;
            else if (tech && (st.tech.kind || st.kind == STEP_NONE) && sudoku_step_with(G, vals, sol, tech, &w)) {
                st = w;                             /* stuck on singles, and this technique opens one */
                found = 1;
            } else if (!tech && learn_match(target, &st)) found = 1;
            if (found) {
                learn_id = id;
                memcpy(learn_vals, vals, 81);
                learn_step = st;
                break;
            }
            vals[st.cell] = (uint8_t)st.digit;      /* on to the next step */
        }
    }
    learn_state = found ? 1 : 2;
    vTaskDelete(NULL);
}

static void learn_poll_cb(lv_timer_t *t)
{
    if (learn_state == 0) return;
    lv_timer_delete(learn_timer);
    learn_timer = NULL;
    if (learn_wait) lv_msgbox_close(learn_wait);
    learn_wait = NULL;
    if (learn_state != 1) {
        sudoku_ui_notice("Couldn't find an example just now: try again.", 3000);
        return;
    }
    /* the puzzle, at that point, as a practice game */
    int var = learn_variant(learn_target);
    if (var >= 0) {                                 /* made just now: kept in RAM only, as without a card */
        learn_id = s_gen_ram_next++;
        if (s_gen_ram_next == 0xFFFF) s_gen_ram_next = PUZZLE_GEN_RAM_BASE;
        gen_cache_put(learn_id, var, GEN_MEDIUM, learn_givens, NULL, var == VAR_KILLER ? learn_cages : NULL,
                      learn_marks[0] ? learn_marks : NULL, learn_thermos[0] ? learn_thermos : NULL, NULL);
    }
    start_game(learn_id, false);
    g_practice = learn_target;
    g_flags = 0;                                    /* (practice: never the no-notes challenge) */
    challenge_ui();
    uint8_t given[81];
    puzzle_get(learn_id, given);
    for (int i = 0; i < 81; i++) {
        if (!given[i]) g_b.val[i] = learn_vals[i];
    }
    g_undo_len = g_undo_head = 0;
    g_nmoves = 0;
    compute_conflicts();
    update_header();
    h_info = learn_step;                            /* the hint card on the technique */
    h_step = 1;
    hint_render();
}

static void learn_pick_cb(lv_event_t *e)
{
    int target = (int)(intptr_t)lv_event_get_user_data(e);
    if (learn_box) lv_msgbox_close_async(learn_box);
    learn_box = NULL;
    if (learn_timer) return;
    char text[120];
    if (learn_variant(target) >= 0) {
        if (gen_timer) return;                      /* New puzzle is using the generator (one at a time) */
        if (!learn_board && !(learn_board = heap_caps_malloc(sizeof(*learn_board), MALLOC_CAP_SPIRAM))) return;
        if (!learn_cages && !(learn_cages = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) return;
        if (!learn_thermos && !(learn_thermos = heap_caps_malloc(STATS_THERMOS_LEN, MALLOC_CAP_SPIRAM))) return;
        snprintf(text, sizeof(text), "Making a %s puzzle where the next step uses its rule...",
                 VARIANT_NAMES[learn_variant(target)]);
    } else {
        snprintf(text, sizeof(text), "Finding a puzzle where the next step is a %s...", LEARN_NAMES[target]);
    }
    learn_target = target;
    learn_state = 0;
    learn_wait = make_msgbox(LV_SYMBOL_EYE_OPEN "  Learn", text);
    /* 16 KB: the variant lessons run the generator (as gen_task); 8 KB overflowed */
    if (xTaskCreatePinnedToCore(learn_task, "learn", 16384, NULL, 2, NULL, WORKER_CORE) != pdPASS) {
        lv_msgbox_close(learn_wait);
        learn_wait = NULL;
        return;
    }
    learn_timer = lv_timer_create(learn_poll_cb, 50, NULL);
}

static void learn_close_cb(lv_event_t *e)
{
    if (learn_box) lv_msgbox_close_async(learn_box);
    learn_box = NULL;
}

static void learn_cb(lv_event_t *e)
{
    lv_obj_t *m = make_msgbox("Learn a technique", NULL);
    learn_box = m;
    lv_obj_set_width(m, 640);
    lv_obj_t *c = lv_msgbox_get_content(m);
    lv_obj_set_height(c, 500);                      /* ten techniques: the list scrolls */
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    make_wrap_text(c, &lv_font_montserrat_16, C_MUTED,
                   "Pick one: a puzzle opens where it's the next step, with the hint showing where to look. "
                   "Practice games aren't saved or counted.");
    for (int i = 0; i < LEARN_COUNT; i++) {
        if (i == LEARN_CAGE) {                      /* the variants' own rules: a heading over them */
            lv_obj_t *hd = make_wrap_text(c, &lv_font_montserrat_16, C_MUTED,
                                          "Variant rules: a new puzzle of that kind is made (a few seconds).");
            lv_obj_set_style_margin_top(hd, 14, 0);
        }
        lv_obj_t *row = make_btn(c, 0, 0, 0, 86, NULL, NULL, learn_pick_cb, (void *)(intptr_t)i);   /* 2 lines */
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_style_bg_color(row, C_CARD2, 0);
        lv_obj_set_style_margin_top(row, 8, 0);
        lv_obj_t *n = make_label(row, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text(n, LEARN_NAMES[i]);
        lv_obj_align(n, LV_ALIGN_TOP_LEFT, 14, 8);
        lv_obj_t *d = make_label(row, &lv_font_montserrat_14, C_MUTED);
        lv_obj_set_width(d, 580);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);   /* (the descriptions take up to two lines) */
        lv_label_set_text(d, LEARN_DESC[i]);
        lv_obj_align(d, LV_ALIGN_TOP_LEFT, 14, 36);
    }
    msgbox_button(m, "Close", learn_close_cb, false);
}

/* ------------------------------------------------------------- enter a puzzle by hand */
/*
 * Enter (Puzzles screen): a blank 9x9 board and a 1-9 pad to type in a
 * Classic puzzle from a book or a newspaper. Check & play: it needs exactly
 * one solution (the solver counts them); then it's graded, saved as "My
 * puzzles #n" (sudoku.db, like an imported one; RAM only without a card)
 * and opened like any other.
 */
lv_obj_t *scr_edit, *ed_cells[81], *ed_lbl[81], *ed_status;
static uint8_t ed_val[81];
static int ed_sel = -1;
static uint16_t s_own_ram_next = 1;                 /* RAM-only numbering without a card */

static void ed_refresh(void)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    int filled = 0, bad = 0;
    for (int i = 0; i < 81; i++) {
        bool clash = false;
        for (int p = 0; ed_val[i] && p < G->npeers[i]; p++) clash |= ed_val[G->peers[i][p]] == ed_val[i];
        filled += ed_val[i] != 0;
        bad += clash;
        lv_color_t bg = i == ed_sel ? C_CELL_SEL : (ed_sel >= 0 && sudoku_geom_peer(G, i, ed_sel) ? C_CELL_PEER : C_CELL);
        if (clash) bg = C_CELL_BAD;
        lv_obj_set_style_bg_color(ed_cells[i], bg, 0);
        lv_obj_set_style_text_color(ed_lbl[i], clash ? C_RED : C_TEXT, 0);
        lv_label_set_text_static(ed_lbl[i], DIGITS[ed_val[i]]);
    }
    if (bad) {
        lv_label_set_text_fmt(ed_status, LV_SYMBOL_WARNING "  %d digits break a rule", bad);
        lv_obj_set_style_text_color(ed_status, C_RED, 0);
    } else {
        lv_label_set_text_fmt(ed_status, "%d given%s  -  tap a cell, then a digit", filled, filled == 1 ? "" : "s");
        lv_obj_set_style_text_color(ed_status, C_MUTED, 0);
    }
}

static void ed_cell_cb(lv_event_t *e)
{
    ed_sel = (int)(intptr_t)lv_event_get_user_data(e);
    ed_refresh();
}

static void ed_digit_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (ed_sel < 0) return;
    ed_val[ed_sel] = ed_val[ed_sel] == d ? 0 : (uint8_t)d;   /* the same digit again clears it */
    if (ed_val[ed_sel] && ed_sel < 80) ed_sel++;     /* on to the next cell, as when typing */
    ed_refresh();
}

static void ed_close(void)
{
    lv_screen_load(scr_list);
    if (scr_edit) lv_obj_delete_async(scr_edit);
    scr_edit = NULL;
}

static void ed_cancel_cb(lv_event_t *e)
{
    ed_close();
}

static void ed_erase_cb(lv_event_t *e)
{
    if (ed_sel >= 0) ed_val[ed_sel] = 0;
    ed_refresh();
}

static void ed_clear_cb(lv_event_t *e)
{
    memset(ed_val, 0, sizeof(ed_val));
    ed_sel = 0;
    ed_refresh();
}

/* the check runs in its own task: the solver recurses a level per empty cell (the UI task's stack is small) */
static volatile int ed_check_state;                 /* 0 idle, 1 checking, 2 done */
static volatile int ed_check_n, ed_check_level;
static uint8_t ed_check_cells[81];
static lv_timer_t *ed_check_timer;
static TaskHandle_t ed_check_handle;                /* its stack in PSRAM (internal RAM is short) */

static void ed_check_task(void *arg)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    int n = sudoku_count_solutions(G, ed_check_cells, 2);
    ed_check_level = n == 1 ? sudoku_grade(G, ed_check_cells) : -1;
    ed_check_n = n;
    ed_check_state = 2;
    vTaskSuspend(NULL);                             /* deleted by ed_check_done (a PSRAM-stack task can't delete itself) */
}

static void ed_check_done(lv_timer_t *t)
{
    if (ed_check_state != 2 || eTaskGetState(ed_check_handle) != eSuspended) return;
    vTaskDeleteWithCaps(ed_check_handle);
    ed_check_handle = NULL;
    lv_timer_delete(ed_check_timer);
    ed_check_timer = NULL;
    ed_check_state = 0;
    if (!scr_edit) return;                          /* (cancelled meanwhile) */
    ed_refresh();
    if (ed_check_n == 0) {
        sudoku_ui_notice("This puzzle has no solution: check the digits (red ones break a rule).", 4000);
        return;
    }
    if (ed_check_n > 1) {
        sudoku_ui_notice("This puzzle has more than one solution: some digits are missing.", 4000);
        return;
    }
    int level = ed_check_level;
    if (level < 0 || level >= GEN_LEVELS) level = GEN_EXPERT;
    int number = 0;
    int gid = stats_db_ready() ? stats_db_gen_add_own(ed_check_cells, level, &number) : -1;
    uint16_t id;
    if (gid > 0 && gid < PUZZLE_GEN_RAM_BASE - PUZZLE_GEN_BASE) {
        id = (uint16_t)(PUZZLE_GEN_BASE + gid);
    } else {
        id = s_gen_ram_next++;                       /* no card: until power-off */
        if (s_gen_ram_next == 0xFFFF) s_gen_ram_next = PUZZLE_GEN_RAM_BASE;
        number = s_own_ram_next++;
    }
    gen_cache_put(id, VAR_CLASSIC, level, ed_check_cells, NULL, NULL, NULL, NULL, NULL);
    gen_cache_source(id, STATS_OWN_SOURCE, number);
    char msg[96];
    snprintf(msg, sizeof(msg), "Saved as %s #%d: one solution, rated %s.", STATS_OWN_SOURCE, number,
             GEN_LEVEL_NAMES[level]);
    sudoku_ui_notice(msg, 3500);
    if (scr_edit) lv_obj_delete_async(scr_edit);
    scr_edit = NULL;
    open_puzzle(id);
}

static void ed_play_cb(lv_event_t *e)
{
    if (ed_check_state) return;
    int filled = 0;
    for (int i = 0; i < 81; i++) filled += ed_val[i] != 0;
    if (filled < 17) {                              /* no Sudoku with fewer has just one solution */
        sudoku_ui_notice("A puzzle needs at least 17 digits to have just one solution.", 4000);
        return;
    }
    memcpy(ed_check_cells, ed_val, 81);
    ed_check_state = 1;
    if (xTaskCreatePinnedToCoreWithCaps(ed_check_task, "edcheck", 16384, NULL, 2, &ed_check_handle, WORKER_CORE,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        ed_check_state = 0;
        sudoku_ui_notice("Not enough memory to check the puzzle right now.", 3000);
        return;
    }
    lv_label_set_text(ed_status, "Checking...");
    lv_obj_set_style_text_color(ed_status, C_MUTED, 0);
    ed_check_timer = lv_timer_create(ed_check_done, 50, NULL);
}

static void enter_cb(lv_event_t *e)
{
    memset(ed_val, 0, sizeof(ed_val));
    ed_sel = 0;
    scr_edit = make_screen();
    make_btn(scr_edit, 8, 6, 120, 48, LV_SYMBOL_CLOSE "  Cancel", &lv_font_montserrat_18, ed_cancel_cb, NULL);
    lv_obj_t *t = make_label(scr_edit, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text(t, "Enter a puzzle");
    lv_obj_set_pos(t, 142, 6);
    ed_status = make_label(scr_edit, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(ed_status, 142, 36);
    lv_obj_t *go = make_btn(scr_edit, SCR_W - 8 - 220, TOOL_Y, 220, 48, LV_SYMBOL_PLAY "  Check & play",
                            &lv_font_montserrat_18, ed_play_cb, NULL);
    lv_obj_set_style_bg_color(go, C_ACCENT, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(go, 0), lv_color_white(), 0);
    lv_obj_t *er = make_btn(scr_edit, 8, TOOL_Y, 130, 48, LV_SYMBOL_BACKSPACE "  Erase", &lv_font_montserrat_16,
                            ed_erase_cb, NULL);
    lv_obj_set_style_bg_color(er, C_CARD2, 0);
    lv_obj_t *cl = make_btn(scr_edit, 8 + 130 + 8, TOOL_Y, 130, 48, LV_SYMBOL_TRASH "  Clear", &lv_font_montserrat_16,
                            ed_clear_cb, NULL);
    lv_obj_set_style_bg_color(cl, C_CARD2, 0);

    lv_obj_t *grid = lv_obj_create(scr_edit);
    lv_obj_remove_style_all(grid);
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(grid, false);
    lv_obj_t *boxes[9];
    for (int b = 0; b < 9; b++) {
        boxes[b] = lv_obj_create(grid);
        lv_obj_remove_style_all(boxes[b]);
        lv_obj_set_style_bg_color(boxes[b], C_GRID_THIN, 0);
        lv_obj_set_style_bg_opa(boxes[b], LV_OPA_COVER, 0);
        lv_obj_set_scrollable(boxes[b], false);
        lv_obj_set_clickable(boxes[b], false);
    }
    for (int i = 0; i < 81; i++) {
        ed_cells[i] = lv_obj_create(grid);
        lv_obj_remove_style_all(ed_cells[i]);
        lv_obj_set_style_bg_opa(ed_cells[i], LV_OPA_COVER, 0);
        lv_obj_set_scrollable(ed_cells[i], false);
        lv_obj_set_clickable(ed_cells[i], true);
        lv_obj_add_event_cb(ed_cells[i], ed_cell_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        ed_lbl[i] = make_label(ed_cells[i], &font_digits_44, C_TEXT);
        lv_obj_center(ed_lbl[i]);
    }
    board_layout(sudoku_geom(VAR_CLASSIC), grid, boxes, ed_cells, NULL, GRID_X);
    const int span = GRID_SIZE - 6, h = (span - 8 * PAD_GAP) / 9;
    for (int d = 1; d <= 9; d++) {
        make_btn(scr_edit, PAD_X, GRID_Y + 1 + (d - 1) * (h + PAD_GAP), PAD_W, h, DIGITS[d], &lv_font_montserrat_28,
                 ed_digit_cb, (void *)(intptr_t)d);
    }
    lv_obj_t *hint = make_label(scr_edit, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(hint, SCR_W - 16);
    lv_label_set_text(hint, "Type in a Classic puzzle's given digits, then Check & play: it needs exactly one solution.");
    lv_obj_set_pos(hint, 8, BOT_Y + (BOT_H - 18) / 2);
    ed_refresh();
    lv_screen_load(scr_edit);
}

/* ---- the title row's context menu (the vertical dots): New puzzle, Enter, Learn */
void new_menu_cb(lv_event_t *e)
{
    static const ctx_item_t ITEMS[] = {
        {LV_SYMBOL_PLUS, "New puzzle", new_puzzle_cb},
        {LV_SYMBOL_KEYBOARD, "Enter puzzle", enter_cb},      /* type one in from a book */
        {LV_SYMBOL_EYE_OPEN, "Learn", learn_cb},             /* practise a technique */
    };
    ctx_menu_toggle(l_new_btn, ITEMS, (int)(sizeof(ITEMS) / sizeof(ITEMS[0])));
}
