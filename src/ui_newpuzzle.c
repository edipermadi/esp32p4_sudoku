/**
 * @file ui_newpuzzle.c
 * @ingroup ui_game
 * @brief New puzzle (the generator and its dialog), the daily puzzle's button, the Puzzles screen's ⋮ menu (Enter a puzzle: ui_enter_puzzle.c).
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void gen_task(void *arg);
static void gen_poll_cb(lv_timer_t *t);
static void gen_start_ex(int variant, int level, uint32_t seed, const char *day);
static void gen_levels_update(void);
static void gen_pick_close(void);
static void gen_level_cb(lv_event_t *e);
static void gen_var_cb(lv_event_t *e);
static void gen_cat_cb(lv_event_t *e);
static void gen_cancel_cb(lv_event_t *e);
static void gen_backdrop_cb(lv_event_t *e);
static void new_puzzle_cb(lv_event_t *e);


/* ------------------------------------------------------------- new puzzle (generator) */
/**
 * @brief New puzzle: pick a level, a worker task runs the generator (gen_make.c)
 * while a dialog says so, then the puzzle is stored (sudoku.db, or RAM
 * without a card) and opened like any other.
 */
static lv_obj_t *gen_box;                           /**< the "Generating..." dialog */
static lv_obj_t *gen_bar;                           /**< ... its progress bar */
static int64_t gen_t0;                              /**< when it started (esp_timer, us) */

/** @brief About how long a puzzle takes on the board, ms: the averages of devctl "genbench all all 10"
 * (docs/development.md, 2026-11-21), by variant (generator.h's order) and level. The progress
 * bar is a guess from these: the generator can't tell how many attempts it will need. Update
 * them with the table when the generator gets faster or slower. */
static const uint16_t GEN_EXPECT_MS[VAR_COUNT][GEN_LEVELS] = {
    {5, 69, 441, 90},       /* Classic */
    {5, 171, 283, 235},     /* X-Sudoku */
    {1, 2, 1052, 41},       /* Mini 6x6 */
    {62, 233, 343, 373},    /* Hyper */
    {297, 346, 527, 1113},  /* Jigsaw */
    {46, 498, 1033, 1630},  /* Killer */
    {12, 281, 442, 405},    /* Anti-Knight */
    {6, 57, 183, 192},      /* Even-Odd */
    {19, 300, 639, 366},    /* Consecutive */
    {8, 86, 416, 331},      /* Thermo */
    {297, 199, 2348, 1987}, /* Sandwich */
    {1, 1, 1, 1},           /* Kids 4x4 (Easy and Medium only) */
    {7, 94, 326, 191},      /* Anti-King */
    {862, 909, 2908, 1540}, /* Non-Consecutive */
    {7, 43, 168, 119},      /* Disjoint Groups */
    {6, 93, 337, 186},      /* Asterisk */
    {28, 116, 352, 189},    /* Kropki */
    {15, 136, 307, 1743},   /* XV */
    {7, 24, 197, 489},      /* Greater-Than */
    {9, 72, 204, 600},      /* Quadruple */
    {9, 320, 518, 497},   /* Arrow */
    {9, 174, 525, 477},   /* Renban */
    {12, 450, 1784, 1111}, /* German Whispers */
    {38, 159, 677, 304},    /* Skyscraper */
    {15, 471, 1102, 1225},  /* Little Killer */
    {11, 284, 905, 756},    /* Palindrome (2026-11-22) */
    {21, 403, 1070, 1206},  /* Region Sum Lines */
    {72, 835, 3012, 2360},  /* X-Sums */
    {12, 86, 186, 946},     /* Zipper Lines (2026-11-22; lines that don't cross, logic-first digging) */
    {15, 112, 641, 262},    /* Frame (re-measured 2026-11-22) */
    {19, 88, 293, 1672},    /* Between Lines (the same) */
    {15, 85, 223, 829},     /* Entropic Lines (the same) */
    {13, 81, 192, 905},     /* Modular Lines */
    {9, 79, 333, 242},      /* Center Dot */
    {37, 117, 1406, 1360},  /* Outside Sudoku (2026-11-22) */
    {41, 292, 1780, 643},   /* Rossini */
    {274, 512, 798, 839},   /* Diagonal Non-Consecutive */
    {16, 460, 1277, 1349},  /* Dutch Whispers (2026-11-22) */
    {9, 187, 784, 754},     /* Parity Lines */
    {11, 188, 696, 535},    /* Slow Thermo */
    {13, 82, 212, 1005},    /* Fortress (2026-11-22) */
    {673, 472, 758, 944},   /* Argyle */
};
static lv_timer_t *gen_timer;           /**< the progress bar's poll while a puzzle is made (NULL: none) */
static volatile bool gen_done;          /**< the generator is done */
static int gen_var_req;                             /**< variant being made (generator.h) */
static int gen_level_req;                           /**< the level asked for */
static int gen_level_got;                           /**< the level it is */
static uint8_t gen_givens[81];          /**< the puzzle made */
static sudoku_geom_t *gen_board;                    /**< the puzzle's board (Jigsaw regions, Killer cages), PSRAM */
static char *gen_cages;                             /**< STATS_CAGES_LEN, PSRAM */
static char gen_marks[82];              /**< its marks (Even-Odd, Consecutive, Kropki, ...) */
static char *gen_thermos;                           /**< STATS_THERMOS_LEN, PSRAM */
static uint32_t gen_seed;                           /**< 0 = random */
static char gen_day[12];                            /**< making a daily puzzle: its date */

/* (documented in generator.h) */
void sudoku_gen_yield(void)
{
    static int64_t last;
    int64_t now = esp_timer_get_time();
    if (now - last < 100000) {
        return;
    }
    last = now;
    vTaskDelay(1);
}

/**
 * @brief The generator task (core 0): makes the puzzle, then sets gen_done.
 * @param[in] arg  unused
 */
static void gen_task(void *arg)
{
    gen_level_t got = (gen_level_t)gen_level_req;
    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
    sudoku_generate(gen_var_req, (gen_level_t)gen_level_req, gen_seed ? gen_seed : esp_random(), gen_givens,
                    gen_board, &got);
    sudoku_layout_texts(gen_board, gen_cages, STATS_CAGES_LEN, gen_marks, gen_thermos, STATS_THERMOS_LEN);
    int clues = 0;
    for (int i = 0; i < 81; i++) {
        clues += gen_givens[i] != 0;
    }
    printf("generator: %s %s asked, %s made, %d clues, %u ms\n", variant_name(gen_var_req),
           gen_level_name(gen_level_req), gen_level_name(got), clues,
           (unsigned)(esp_timer_get_time() / 1000 - t0));
    gen_level_got = got;
    gen_done = true;
    vTaskDelete(NULL);
}

/**
 * @brief While a puzzle is made: the progress bar; once done, the puzzle kept (sudoku.db, or the RAM cache) and opened.
 * @param[in] t  the timer
 */
static void gen_poll_cb(lv_timer_t *t)
{
    if (!gen_done) {
        /* the bar: 1 - e^(-elapsed / expected), about two thirds full at the usual time and
         * slowing down after it; at most 97 % until it's done */
        int lv = gen_level_req >= 0 && gen_level_req < GEN_LEVELS ? gen_level_req : 0;
        float expect = GEN_EXPECT_MS[gen_var_req][lv] < 50 ? 50.0f : (float)GEN_EXPECT_MS[gen_var_req][lv];
        float ms = (float)(esp_timer_get_time() - gen_t0) / 1000.0f;
        int v = (int)(1000.0f * (1.0f - expf(-ms / expect)));
        if (gen_bar) {
            lv_bar_set_value(gen_bar, v > 970 ? 970 : v, LV_ANIM_OFF);
        }
        return;
    }
    lv_timer_delete(gen_timer);
    gen_timer = NULL;
    if (gen_box) {
        lv_msgbox_close(gen_box);
    }
    gen_box = NULL;
    gen_bar = NULL;                                 /* (went with the box) */
    uint16_t id;
    const uint8_t *regions = gen_var_req == VAR_JIGSAW ? gen_board->region : NULL;
    const char *cages = gen_var_req == VAR_KILLER || gen_var_req == VAR_QUAD || gen_var_req == VAR_LK ? gen_cages : NULL;
    const char *marks = gen_marks[0] ? gen_marks : NULL;
    const char *thermos = gen_thermos[0] ? gen_thermos : NULL;
    int gid = is_stats_db_ready() ? stats_db_gen_add(gen_var_req, gen_level_got, gen_givens, regions, cages, marks,
              thermos, gen_day[0] ? gen_day : NULL)
              : -1;
    if (gid > 0 && gid < PUZZLE_GEN_RAM_BASE - PUZZLE_GEN_BASE) {
        id = (uint16_t)(PUZZLE_GEN_BASE + gid);
    } else {
        id = gen_ram_take();                       /* no card: until power-off */
    }
    gen_cache_put(id, gen_var_req, gen_level_got, gen_givens, regions, cages, marks, thermos, gen_day);
    if (gen_level_got != gen_level_req && !gen_day[0]) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Couldn't make a %s %s quickly; this one is %s.", gen_level_name(gen_level_req),
                 gen_var_req == VAR_CLASSIC ? "one" : variant_name(gen_var_req), gen_level_name(gen_level_got));
        sudoku_ui_notice(msg, 4000);
    }
    open_puzzle(id);
}

/** @brief seed 0 = a random puzzle; a daily one (always classic) has its date as the seed and day set */
static bool gen_bench;                              /**< devctl genbench has the generator */

/* (documented in ui_internal.h) */
bool is_gen_busy(void)
{
    return gen_timer || gen_bench;
}

/* (documented in ui_sudoku.h) */
bool gen_bench_begin(void)
{
    if (gen_timer || is_learn_busy() || gen_bench) {
        return false;
    }
    gen_bench = true;
    return true;
}

/* (documented in ui_sudoku.h) */
void gen_bench_end(void)
{
    gen_bench = false;
}

/**
 * @brief Makes a new puzzle in the background (one at a time), with a progress dialog.
 * @param[in] variant  sudoku_variant_t
 * @param[in] level  gen_level_t
 * @param[in] seed  the generator's seed, 0: random
 * @param[in] day  a daily puzzle's date, or NULL
 */
static void gen_start_ex(int variant, int level, uint32_t seed, const char *day)
{
    if (gen_timer || is_learn_busy() || gen_bench) {
        return;    /* one at a time */
    }
    if (!gen_board && !(gen_board = heap_caps_malloc(sizeof(*gen_board), MALLOC_CAP_SPIRAM))) {
        return;
    }
    if (!gen_cages && !(gen_cages = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) {
        return;
    }
    if (!gen_thermos && !(gen_thermos = heap_caps_malloc(STATS_THERMOS_LEN, MALLOC_CAP_SPIRAM))) {
        return;
    }
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
    } else if (gen_var_req == VAR_CLASSIC) {
        snprintf(text, sizeof(text), "Making a new %s puzzle...", gen_level_name(level));
    } else {
        snprintf(text, sizeof(text), "Making a new %s %s puzzle...", gen_level_name(level), variant_name(gen_var_req));
    }
    gen_box = make_msgbox(LV_SYMBOL_REFRESH "  New puzzle", text);
    gen_bar = lv_bar_create(lv_msgbox_get_content(gen_box));   /* filled by gen_poll_cb */
    lv_obj_set_size(gen_bar, LV_PCT(100), 14);
    lv_obj_set_style_margin_top(gen_bar, 16, 0);
    lv_obj_set_style_bg_color(gen_bar, C_CARD2, 0);
    lv_obj_set_style_bg_opa(gen_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(gen_bar, C_ACCENT, LV_PART_INDICATOR);
    lv_bar_set_range(gen_bar, 0, 1000);
    lv_bar_set_value(gen_bar, 0, LV_ANIM_OFF);
    gen_t0 = esp_timer_get_time();
    if (xTaskCreatePinnedToCore(gen_task, "gen", 16384, NULL, 2, NULL, WORKER_CORE) != pdPASS) {
        lv_msgbox_close(gen_box);
        gen_box = NULL;
        gen_bar = NULL;
        sudoku_ui_notice("Couldn't start the generator.", 3000);
        return;
    }
    gen_timer = lv_timer_create(gen_poll_cb, 50, NULL);
}

/* (documented in ui_internal.h) */
void gen_start(int variant, int level)
{
    gen_start_ex(variant, level, 0, NULL);
}


/* ------------------------------------------------------------- daily puzzle (the choice: daily_core.c) */
/* (documented in ui_internal.h) */
void daily_sub_text(char *buf, size_t n)
{
    char day[12], kind[32];
    int level, variant, id, streak;
    bool solved;
    if (!daily_today(day, &level, &variant)) {
        snprintf(buf, n, "Needs the date: connect Wi-Fi in Settings");
        return;
    }
    if (!stats_db_daily_status(day, &id, &solved, &streak)) {
        daily_kind_text(variant, level, kind, sizeof(kind));
        snprintf(buf, n, "Today: %s", kind);
        return;
    }
    if (id) {
        gen_lookup((uint16_t)(PUZZLE_GEN_BASE + id), NULL, &variant, &level);    /* as it was made */
    }
    daily_kind_text(variant, level, kind, sizeof(kind));
    if (solved) {
        snprintf(buf, n, LV_SYMBOL_OK "  Solved today  -  streak %d", streak);
    } else if (streak) {
        snprintf(buf, n, "Today: %s  -  streak %d", kind, streak);
    } else {
        snprintf(buf, n, "Today: %s", kind);
    }
}

/* (documented in ui_internal.h) */
void daily_cb(lv_event_t *e)
{
    char day[12];
    int level, variant, id = 0, streak = 0;
    bool solved = false;
    if (!daily_today(day, &level, &variant)) {
        sudoku_ui_notice("The daily puzzle needs today's date: connect Wi-Fi (Settings) so the clock is set.", 4000);
        return;
    }
    stats_db_daily_status(day, &id, &solved, &streak);
    if (!id) {                                        /* RAM-only (no card): look in the cache */
        uint16_t cached = gen_cache_find_day(day);
        id = cached ? cached - PUZZLE_GEN_BASE : 0;
    }
    if (id) {
        if (solved) {
            sudoku_ui_notice("You've solved today's puzzle; playing it again doesn't change the streak.", 3500);
        }
        open_puzzle((uint16_t)(PUZZLE_GEN_BASE + id));
        return;
    }
    gen_start_ex(variant, level, daily_seed(day), day);
}

static lv_obj_t *gen_pick;                          /**< the level dialog */
static lv_obj_t *gen_pick_desc;                     /**< its line about the variant chosen */
static int gen_pick_var = VAR_CLASSIC;              /**< variant chosen there (kept until power-off) */

#define GEN_CATS     4                  /**< the dialog's tabs */
#define GEN_CAT_SLOTS 16                /**< variant buttons per tab: 4 rows of 4 (the unused ones hidden) */
#define GEN_CAT_END  0xFF               /**< after a tab's last variant */

/** @brief the New puzzle dialog's tabs: their names */
static const char *const GEN_CAT_NAMES[GEN_CATS] = {"Grids", "Marks", "Lines", "Clues"};
/** @brief each tab's variants, in button order: the grids and their extra regions; marks and rules
 * between cells; lines; clues (cages, and sums or counts outside the grid) */
static const uint8_t GEN_CAT_VARS[GEN_CATS][GEN_CAT_SLOTS + 1] = {
    {
        VAR_CLASSIC, VAR_X, VAR_HYPER, VAR_JIGSAW, VAR_MINI, VAR_KIDS, VAR_DISJOINT, VAR_ASTERISK, VAR_CENTERDOT,
        VAR_ARGYLE, GEN_CAT_END
    },
    {
        VAR_KNIGHT, VAR_KING, VAR_NONCONSEC, VAR_DIAGNC, VAR_EVENODD, VAR_CONSEC, VAR_KROPKI, VAR_XV, VAR_GT, VAR_QUAD,
        VAR_FORTRESS, GEN_CAT_END
    },
    {
        VAR_THERMO, VAR_ARROW, VAR_RENBAN, VAR_WHISPER, VAR_PALINDROME, VAR_REGIONSUM, VAR_ZIPPER, VAR_BETWEEN,
        VAR_ENTROPIC, VAR_MODULAR, VAR_DUTCH, VAR_PARITY, VAR_SLOW, GEN_CAT_END
    },
    {VAR_KILLER, VAR_SANDWICH, VAR_SKY, VAR_LK, VAR_XSUM, VAR_FRAME, VAR_OUTSIDE, VAR_ROSSINI, GEN_CAT_END},
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
    [VAR_ARGYLE] = "Argyle",
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
            lv_obj_set_state(gen_lvl_btn[lv], LV_STATE_DISABLED, gen_pick_var == VAR_KIDS && lv >= GEN_HARD);
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

/**
 * @brief New puzzle: the dialog (a variant, then a level).
 * @param[in] e  the event
 */
static void new_puzzle_cb(lv_event_t *e)
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
    lv_obj_set_size(vm, LV_PCT(100), 200);          /* four rows */
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

/* (documented in ui_internal.h) */
void new_menu_cb(lv_event_t *e)
{
    bool can_export, can_delete;
    list_coll_actions(&can_export, &can_delete);
    const ctx_item_t ITEMS[] = {
        {LV_SYMBOL_PLUS, "New puzzle", new_puzzle_cb, false, false},
        {LV_SYMBOL_KEYBOARD, "Enter puzzle", enter_cb, false, false},      /* type one in from a book */
        {LV_SYMBOL_EYE_OPEN, "Learn", learn_cb, false, false},             /* practise a technique */
        {
            is_puzzles_starred() ? LV_SYMBOL_LIST : SYMBOL_STAR, is_puzzles_starred() ? "All puzzles" : "Starred puzzles",
            list_star_cb, false, false
        },                                      /* the starred ones in the right pane, or back */
        /* the collection shown: dimmed where they don't apply (Built-in, Starred; Export: saved ones only) */
        {LV_SYMBOL_UPLOAD, "Export collection", list_export_cb, !can_export, false},
        {LV_SYMBOL_TRASH, "Delete collection", list_del_cb, !can_delete, true},
    };
    ctx_menu_toggle(list_new_btn(), ITEMS, (int)(sizeof(ITEMS) / sizeof(ITEMS[0])));
}
