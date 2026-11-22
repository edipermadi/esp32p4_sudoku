/**
 * @file ui_newpuzzle.c
 * @ingroup ui_game
 * @brief New puzzle (the generator), the daily puzzle, Learn, Enter a puzzle.
 */
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
static void gen_cat_cb(lv_event_t *e);
static void gen_cancel_cb(lv_event_t *e);
static void gen_backdrop_cb(lv_event_t *e);
static void new_puzzle_cb(lv_event_t *e);
static int learn_variant(int target);
static int learn_variant_tech(int target);
static int learn_tech(int target);
static bool is_learn_match(int target, const sudoku_step_t *st);
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

/** Learn's techniques' names, LEARN_* in order. */
static const char *const LEARN_NAMES[LEARN_COUNT] = {"Hidden single", "Naked single", "Pointing", "Claiming",
                                                     "Naked pair", "Hidden pair", "Naked triple", "X-Wing",
                                                     "Swordfish", "XY-Wing", "Killer cage", "Thermometer",
                                                     "Sandwich clue", "Consecutive bars"
                                                    };

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
 * @param arg  unused
 */
static void gen_task(void *arg)
{
    gen_level_t got = (gen_level_t)gen_level_req;
    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000);
    sudoku_generate(gen_var_req, (gen_level_t)gen_level_req, gen_seed ? gen_seed : esp_random(), gen_givens,
                    gen_board, &got);
    gen_cages[0] = 0;
    if (gen_var_req == VAR_KILLER) {
        sudoku_cages_text(gen_board, gen_cages, STATS_CAGES_LEN);
    }
    if (gen_var_req == VAR_QUAD) {
        sudoku_quads_text(gen_board, gen_cages, STATS_CAGES_LEN);    /* (where cages go) */
    }
    gen_marks[0] = 0;
    if (gen_var_req == VAR_EVENODD || gen_var_req == VAR_CONSEC || gen_board->edge_kind) {
        sudoku_marks_text(gen_board, gen_marks, 82);
    }
    if (gen_var_req == VAR_SANDWICH) {
        sudoku_sandwich_text(gen_board, gen_marks, 82);
    }
    if (gen_var_req == VAR_SKY) {
        sudoku_sky_text(gen_board, gen_marks, 82);
    }
    if (gen_var_req == VAR_XSUM) {
        sudoku_xsum_text(gen_board, gen_marks, 82);
    }
    if (gen_var_req == VAR_FRAME) {
        sudoku_frame_text(gen_board, gen_marks, 82);
    }
    if (gen_var_req == VAR_OUTSIDE) {
        sudoku_outside_text(gen_board, gen_marks, 82);
    }
    if (gen_var_req == VAR_ROSSINI) {
        sudoku_rossini_text(gen_board, gen_marks, 82);
    }
    if (gen_var_req == VAR_LK) {
        sudoku_lk_text(gen_board, gen_cages, STATS_CAGES_LEN);    /* (where cages go) */
    }
    gen_thermos[0] = 0;
    if (gen_board->nthermos) {
        sudoku_thermos_text(gen_board, gen_thermos, STATS_THERMOS_LEN);    /* (any lines) */
    }
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
 * @param t  the timer
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
static lv_timer_t *learn_timer;                     /**< (Learn: its variant lessons use the generator too) */
static bool gen_bench;                              /**< devctl genbench has the generator */

/* (documented in ui_sudoku.h) */
bool gen_bench_begin(void)
{
    if (gen_timer || learn_timer || gen_bench) {
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
 * @param variant  sudoku_variant_t
 * @param level  gen_level_t
 * @param seed  the generator's seed, 0: random
 * @param day  a daily puzzle's date, or NULL
 */
static void gen_start_ex(int variant, int level, uint32_t seed, const char *day)
{
    if (gen_timer || learn_timer || gen_bench) {
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

/* ------------------------------------------------------------- daily puzzle */
/**
 * @brief One puzzle a day, the same for every player: made from the date (its seed),
 * with a variant and level by weekday (DAILY_*): Mon Classic Easy, Tue Mini
 * Medium, Wed X-Sudoku Medium, Thu Hyper Hard, Fri Killer Medium, Sat Jigsaw
 * Hard, Sun Classic Expert. Stored like any generated puzzle, with its date
 * (generated.day): a day's puzzle, once made, stays what it is. The streak
 * counts days in a row with the daily solved (stats_db).
 */
static const uint8_t DAILY_LEVEL[7] = {GEN_EXPERT, GEN_EASY, GEN_MEDIUM, GEN_MEDIUM, GEN_HARD, GEN_MEDIUM, GEN_HARD};
/** each weekday's daily variant (Sunday first) */
static const uint8_t DAILY_VARIANT[7] = {VAR_CLASSIC, VAR_CLASSIC, VAR_MINI, VAR_X, VAR_HYPER, VAR_KILLER, VAR_JIGSAW};

/**
 * @brief Today's date, level and variant.
 * @param day  gets the date
 * @param level  gets the level
 * @param variant  gets the variant
 * @return false while the clock isn't set
 */
static bool daily_today_v(char day[12], int *level, int *variant)
{
    time_t now = time(NULL);
    if (!CLOCK_VALID(now)) {
        return false;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(day, 12, "%Y-%m-%d", &tm);
    *level = DAILY_LEVEL[tm.tm_wday];
    if (variant) {
        *variant = DAILY_VARIANT[tm.tm_wday];
    }
    return true;
}

/* (documented in ui_internal.h) */
bool daily_today(char day[12], int *level, int *wday)
{
    (void)wday;
    return daily_today_v(day, level, NULL);
}

/* (documented in ui_internal.h) */
void daily_kind_text(int variant, int level, char *buf, size_t n)
{
    const char *lv = level >= 0 && level < GEN_LEVELS ? gen_level_name(level) : "";
    if (variant > VAR_CLASSIC && variant < VAR_COUNT) {
        snprintf(buf, n, "%s %s", variant_name(variant), lv);
    } else {
        snprintf(buf, n, "%s", lv);
    }
}

/**
 * @brief The daily puzzle's seed: FNV-1a of its date.
 * @param day  the date
 * @return the seed
 */
static uint32_t daily_seed(const char *day)          /* FNV-1a of the date */
{
    uint32_t h = 2166136261u;
    for (const char *p = day; *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return h ? h : 1;
}

/* (documented in ui_internal.h) */
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
    if (!daily_today_v(day, &level, &variant)) {
        sudoku_ui_notice("The daily puzzle needs today's date: connect Wi-Fi (Settings) so the clock is set.", 4000);
        return;
    }
    stats_db_daily_status(day, &id, &solved, &streak);
    if (!id) {                                        /* RAM-only (no card): look in the cache */
        for (int i = 0; i < GEN_CACHE; i++) {
            if (ui_gen_cache()[i].id && !strcmp(ui_gen_cache()[i].day, day)) {
                id = ui_gen_cache()[i].id - PUZZLE_GEN_BASE;
            }
        }
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
#define GEN_CAT_SLOTS 12                /**< variant buttons per tab: 3 rows of 4 (the unused ones hidden) */
#define GEN_CAT_END  0xFF               /**< after a tab's last variant */

/** @brief the New puzzle dialog's tabs: their names */
static const char *const GEN_CAT_NAMES[GEN_CATS] = {"Grids", "Marks", "Lines", "Clues"};
/** @brief each tab's variants, in button order: the grids and their extra regions; marks and rules
 * between cells; lines; clues (cages, and sums or counts outside the grid) */
static const uint8_t GEN_CAT_VARS[GEN_CATS][GEN_CAT_SLOTS + 1] = {
    {VAR_CLASSIC, VAR_X, VAR_HYPER, VAR_JIGSAW, VAR_MINI, VAR_KIDS, VAR_DISJOINT, VAR_ASTERISK, VAR_CENTERDOT, GEN_CAT_END},
    {
        VAR_KNIGHT, VAR_KING, VAR_NONCONSEC, VAR_DIAGNC, VAR_EVENODD, VAR_CONSEC, VAR_KROPKI, VAR_XV, VAR_GT, VAR_QUAD,
        GEN_CAT_END
    },
    {
        VAR_THERMO, VAR_ARROW, VAR_RENBAN, VAR_WHISPER, VAR_PALINDROME, VAR_REGIONSUM, VAR_ZIPPER, VAR_BETWEEN,
        VAR_ENTROPIC, VAR_MODULAR, GEN_CAT_END
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
 * @param e  the event (user data: the level)
 */
static void gen_level_cb(lv_event_t *e)
{
    int level = (int)(intptr_t)lv_event_get_user_data(e);
    gen_pick_close();
    gen_start(gen_pick_var, level);
}

/**
 * @brief A variant picked: the levels it has.
 * @param e  the event
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
 * @param cat  the tab
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
 * @param e  the event (the tabs' button matrix)
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
 * @param e  the event
 */
static void gen_cancel_cb(lv_event_t *e)
{
    gen_pick_close();
}

/**
 * @brief A tap on the dimmed background (not on the dialog) closes it too.
 * @param e  the event
 */
static void gen_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) {
        gen_pick_close();
    }
}

/**
 * @brief New puzzle: the dialog (a variant, then a level).
 * @param e  the event
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
    lv_obj_t *tabs = lv_buttonmatrix_create(lv_msgbox_get_content(m));
    lv_buttonmatrix_set_map(tabs, CAT_MAP);
    lv_buttonmatrix_set_button_ctrl_all(tabs, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(tabs, true);
    for (int c = 0; c < GEN_CATS; c++) {            /* the tab of the variant picked last */
        for (int i = 0; GEN_CAT_VARS[c][i] != GEN_CAT_END; i++) {
            if (GEN_CAT_VARS[c][i] == gen_pick_var) {
                gen_pick_cat = c;
                gen_cat_pick[c] = (uint8_t)gen_pick_var;
            }
        }
    }
    lv_buttonmatrix_set_button_ctrl(tabs, (uint32_t)gen_pick_cat, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(tabs, LV_PCT(100), 44);
    lv_obj_set_style_pad_all(tabs, 0, 0);
    lv_obj_set_style_pad_column(tabs, 8, 0);
    lv_obj_set_style_margin_top(tabs, 12, 0);
    lv_obj_set_style_border_width(tabs, 0, 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(tabs, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_ITEMS);   /* a tab: its name, underlined when shown */
    lv_obj_set_style_shadow_width(tabs, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(tabs, C_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_border_side(tabs, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_width(tabs, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_color(tabs, C_GRID_THIN, LV_PART_ITEMS);
    lv_obj_set_style_radius(tabs, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(tabs, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tabs, 4, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tabs, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(tabs, gen_cat_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *vm = lv_buttonmatrix_create(lv_msgbox_get_content(m));
    gen_var_bm = vm;
    lv_buttonmatrix_set_one_checked(vm, true);
    lv_obj_set_size(vm, LV_PCT(100), 148);          /* three rows */
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

/* ------------------------------------------------------------- learn (practice a technique) */
/**
 * @brief Learn (Puzzles screen): pick a technique; a worker task walks built-in
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

/**
 * @brief The variant lessons: a new puzzle of that variant (made on the spot, kept in RAM only).
 * @param target  LEARN_*
 * @return its variant, or -1 (a technique lesson)
 */
static int learn_variant(int target)
{
    switch (target) {
    case LEARN_CAGE:
        return VAR_KILLER;
    case LEARN_THERMO:
        return VAR_THERMO;
    case LEARN_SANDWICH:
        return VAR_SANDWICH;
    case LEARN_CONSEC:
        return VAR_CONSEC;
    }
    return -1;
}

/**
 * @brief The technique a variant lesson shows.
 * @param target  LEARN_*
 * @return TECH_*
 */
static int learn_variant_tech(int target)
{
    switch (target) {
    case LEARN_CAGE:
        return TECH_CAGE;
    case LEARN_THERMO:
        return TECH_THERMO;
    case LEARN_SANDWICH:
        return TECH_SANDWICH;
    case LEARN_CONSEC:
        return TECH_CONSEC;
    }
    return TECH_NONE;
}

/**
 * @brief the techniques that sudoku_step_with() finds a lesson position for (the newer, rarer ones:
 * the hint engine seldom reaches them first, so any position where they open a single will do)
 * @param target  LEARN_*
 * @return TECH_*, or TECH_NONE
 */
static int learn_tech(int target)
{
    switch (target) {
    case LEARN_NAKED_TRIPLE:
        return TECH_NAKED_TRIPLE;
    case LEARN_X_WING:
        return TECH_X_WING;
    case LEARN_SWORDFISH:
        return TECH_SWORDFISH;
    case LEARN_XY_WING:
        return TECH_XY_WING;
    }
    return TECH_NONE;
}

static lv_obj_t *learn_box;                         /**< the Learn dialog */
static lv_obj_t *learn_wait;                        /**< "Finding a puzzle..." */
static volatile int learn_state;                    /**< 0 searching, 1 found, 2 nothing found */
static int learn_target;                /**< the lesson asked for, LEARN_* */
static uint16_t learn_id;               /**< the lesson's puzzle */
static uint8_t learn_vals[81];          /**< its position */
static sudoku_step_t learn_step;        /**< the step it shows */

/**
 * @brief does this step need the technique (for a naked single: a cell with one candidate is enough)
 * @param target  LEARN_*
 * @param st  the step
 * @return true if so
 */
static bool is_learn_match(int target, const sudoku_step_t *st)
{
    switch (target) {
    case LEARN_HIDDEN:
        return st->kind == STEP_HIDDEN_SINGLE && !st->tech.kind;
    case LEARN_NAKED:
        return st->kind == STEP_NAKED_SINGLE && !st->tech.kind;
    case LEARN_POINTING:
        return st->tech.kind == TECH_POINTING;
    case LEARN_CLAIMING:
        return st->tech.kind == TECH_CLAIMING;
    case LEARN_NAKED_PAIR:
        return st->tech.kind == TECH_NAKED_PAIR;
    case LEARN_HIDDEN_PAIR:
        return st->tech.kind == TECH_HIDDEN_PAIR;
    }
    return false;
}

/**
 * @brief a naked single on the board: a cell only one digit fits (singles are found
 * as hidden ones first, so the hint engine seldom names this one itself)
 * @param vals  the board
 * @param sol  the solution
 * @param st  gets the step
 * @return true if there is one
 */
static bool learn_find_naked(const uint8_t vals[81], const uint8_t sol[81], sudoku_step_t *st)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    for (int c = 0; c < 81; c++) {
        if (vals[c]) {
            continue;
        }
        uint16_t m = 0x3FE;
        for (int p = 0; p < G->npeers[c]; p++) {
            m &= (uint16_t)~(1u << vals[G->peers[c][p]]);
        }
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
static uint8_t learn_givens[81];                    /**< a variant lesson's puzzle */
static sudoku_geom_t *learn_board;                  /**< PSRAM */
static char *learn_cages;                           /**< its cages (PSRAM) */
static char *learn_thermos;                         /**< its lines (PSRAM) */
static char learn_marks[82];            /**< its marks */

/**
 * @brief A variant lesson's position: new puzzles of the variant until a step needs the technique.
 * @param target  LEARN_*
 * @return 1 found, 2 nothing found
 */
static int learn_variant_find(int target)
{
    static uint8_t vals[81], sol[81];
    const int var = learn_variant(target), tech = learn_variant_tech(target);
    for (int tries = 0; tries < 6; tries++) {      /* most puzzles have one early; a few seconds each at most */
        gen_level_t got;
        if (!sudoku_generate(var, GEN_MEDIUM, esp_random(), learn_givens, learn_board, &got)) {
            continue;
        }
        if (!sudoku_solve(learn_board, learn_givens, sol)) {
            continue;
        }
        memcpy(vals, learn_givens, 81);
        for (int step = 0; step < 81; step++) {
            sudoku_step_t st;
            if (!sudoku_next_step(learn_board, vals, sol, &st) || st.cell < 0) {
                break;
            }
            if (st.tech.kind == tech) {
                memcpy(learn_vals, vals, 81);
                learn_step = st;
                learn_cages[0] = learn_marks[0] = learn_thermos[0] = 0;
                if (var == VAR_KILLER) {
                    sudoku_cages_text(learn_board, learn_cages, STATS_CAGES_LEN);
                }
                if (var == VAR_CONSEC) {
                    sudoku_marks_text(learn_board, learn_marks, sizeof(learn_marks));
                }
                if (var == VAR_SANDWICH) {
                    sudoku_sandwich_text(learn_board, learn_marks, sizeof(learn_marks));
                }
                if (var == VAR_THERMO) {
                    sudoku_thermos_text(learn_board, learn_thermos, STATS_THERMOS_LEN);
                }
                return 1;
            }
            vals[st.cell] = (uint8_t)st.digit;      /* on to the next step */
        }
    }
    return 2;
}

/**
 * @brief The lesson search (core 0): a position where the technique opens up a single; learn_state then says found or not.
 * @param arg  unused
 */
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
        if (target >= LEARN_POINTING && strstr(puzzle_collection(c)->name, "Easy")) {
            continue;
        }
        uint16_t id = (uint16_t)(puzzle_collection(c)->first + (esp_random() % puzzle_collection(c)->count));
        puzzle_load(id, vals);
        if (!sudoku_solve(G, vals, sol)) {
            continue;
        }
        sudoku_step_t st, w;
        const int tech = learn_tech(target);
        for (int step = 0; step < 81 && !found; step++) {
            if (target == LEARN_NAKED && step >= 8 && learn_find_naked(vals, sol, &st)) {
                found = 1;
            } else if (!sudoku_next_step(G, vals, sol, &st) || st.cell < 0) {
                break;
            } else if (tech && (st.tech.kind || st.kind == STEP_NONE) && sudoku_step_with(G, vals, sol, tech, &w)) {
                st = w;                             /* stuck on singles, and this technique opens one */
                found = 1;
            } else if (!tech && is_learn_match(target, &st)) {
                found = 1;
            }
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

/**
 * @brief Waits for the lesson search: then opens the lesson, or says none was found.
 * @param t  the timer
 */
static void learn_poll_cb(lv_timer_t *t)
{
    if (learn_state == 0) {
        return;
    }
    lv_timer_delete(learn_timer);
    learn_timer = NULL;
    if (learn_wait) {
        lv_msgbox_close(learn_wait);
    }
    learn_wait = NULL;
    if (learn_state != 1) {
        sudoku_ui_notice("Couldn't find an example just now: try again.", 3000);
        return;
    }
    /* the puzzle, at that point, as a practice game */
    int var = learn_variant(learn_target);
    if (var >= 0) {                                 /* made just now: kept in RAM only, as without a card */
        learn_id = gen_ram_take();
        gen_cache_put(learn_id, var, GEN_MEDIUM, learn_givens, NULL, var == VAR_KILLER ? learn_cages : NULL,
                      learn_marks[0] ? learn_marks : NULL, learn_thermos[0] ? learn_thermos : NULL, NULL);
    }
    start_game(learn_id, false);
    game_set_practice(learn_target);
    game_set_flags(0);                                    /* (practice: never the no-notes challenge) */
    challenge_ui();
    uint8_t given[81];
    puzzle_get(learn_id, given);
    for (int i = 0; i < 81; i++) {
        if (!given[i]) {
            game_put_value(i, learn_vals[i]);
        }
    }
    game_set_undo_len(0);
    game_set_undo_head(0);
    game_set_nmoves(0);
    compute_conflicts();
    update_header();
    hint_set_info(&(learn_step));                            /* the hint card on the technique */
    hint_set_step(1);
    hint_render();
}

/**
 * @brief A lesson picked: the search starts.
 * @param e  the event (user data: LEARN_*)
 */
static void learn_pick_cb(lv_event_t *e)
{
    int target = (int)(intptr_t)lv_event_get_user_data(e);
    if (learn_box) {
        lv_msgbox_close_async(learn_box);
    }
    learn_box = NULL;
    if (learn_timer || gen_bench) {
        return;    /* (genbench: the generator is busy) */
    }
    char text[120];
    if (learn_variant(target) >= 0) {
        if (gen_timer) {
            return;    /* New puzzle is using the generator (one at a time) */
        }
        if (!learn_board && !(learn_board = heap_caps_malloc(sizeof(*learn_board), MALLOC_CAP_SPIRAM))) {
            return;
        }
        if (!learn_cages && !(learn_cages = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) {
            return;
        }
        if (!learn_thermos && !(learn_thermos = heap_caps_malloc(STATS_THERMOS_LEN, MALLOC_CAP_SPIRAM))) {
            return;
        }
        snprintf(text, sizeof(text), "Making a %s puzzle where the next step uses its rule...",
                 variant_name(learn_variant(target)));
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

/**
 * @brief Closes the Learn dialog.
 * @param e  the event
 */
static void learn_close_cb(lv_event_t *e)
{
    if (learn_box) {
        lv_msgbox_close_async(learn_box);
    }
    learn_box = NULL;
}

/**
 * @brief Learn: the dialog of techniques.
 * @param e  the event
 */
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
/**
 * @brief Enter (Puzzles screen): a blank 9x9 board and a 1-9 pad to type in a
 * Classic puzzle from a book or a newspaper. Check & play: it needs exactly
 * one solution (the solver counts them); then it's graded, saved as "My
 * puzzles #n" (sudoku.db, like an imported one; RAM only without a card)
 * and opened like any other.
 */
static lv_obj_t *scr_edit;                          /**< the Enter a puzzle screen (NULL: not built) */
static lv_obj_t *ed_cells[81];                      /**< its cells */
static lv_obj_t *ed_lbl[81];                        /**< their digits */
static lv_obj_t *ed_status;                         /**< "30 digits", a conflict, ... */
static uint8_t ed_val[81];              /**< the digits typed, 0 = empty */
static int ed_sel = -1;                 /**< the selected cell, -1: none */
static uint16_t s_own_ram_next = 1;                 /**< RAM-only numbering without a card */

/**
 * @brief Redraws the board typed in: digits, the selection, conflicts, the count.
 */
static void ed_refresh(void)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    int filled = 0, bad = 0;
    for (int i = 0; i < 81; i++) {
        bool clash = false;
        for (int p = 0; ed_val[i] && p < G->npeers[i]; p++) {
            clash |= ed_val[G->peers[i][p]] == ed_val[i];
        }
        filled += ed_val[i] != 0;
        bad += clash;
        lv_color_t bg = i == ed_sel ? C_CELL_SEL : (ed_sel >= 0 && is_sudoku_geom_peer(G, i, ed_sel) ? C_CELL_PEER : C_CELL);
        if (clash) {
            bg = C_CELL_BAD;
        }
        lv_obj_set_style_bg_color(ed_cells[i], bg, 0);
        lv_obj_set_style_text_color(ed_lbl[i], clash ? C_RED : C_TEXT, 0);
        lv_label_set_text_static(ed_lbl[i], ui_digits()[ed_val[i]]);
    }
    if (bad) {
        lv_label_set_text_fmt(ed_status, LV_SYMBOL_WARNING "  %d digits break a rule", bad);
        lv_obj_set_style_text_color(ed_status, C_RED, 0);
    } else {
        lv_label_set_text_fmt(ed_status, "%d given%s  -  tap a cell, then a digit", filled, filled == 1 ? "" : "s");
        lv_obj_set_style_text_color(ed_status, C_MUTED, 0);
    }
}

/**
 * @brief A cell tapped: selected.
 * @param e  the event (user data: the cell)
 */
static void ed_cell_cb(lv_event_t *e)
{
    ed_sel = (int)(intptr_t)lv_event_get_user_data(e);
    ed_refresh();
}

/**
 * @brief A digit tapped: into the selected cell (the same digit again clears it), then on to the next cell.
 * @param e  the event (user data: the digit)
 */
static void ed_digit_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    if (ed_sel < 0) {
        return;
    }
    ed_val[ed_sel] = ed_val[ed_sel] == d ? 0 : (uint8_t)d;   /* the same digit again clears it */
    if (ed_val[ed_sel] && ed_sel < 80) {
        ed_sel++;    /* on to the next cell, as when typing */
    }
    ed_refresh();
}

/**
 * @brief Closes Enter a puzzle (back to Puzzles).
 */
static void ed_close(void)
{
    lv_screen_load(screen_list());
    if (scr_edit) {
        lv_obj_delete_async(scr_edit);
    }
    scr_edit = NULL;
}

/**
 * @brief Cancel.
 * @param e  the event
 */
static void ed_cancel_cb(lv_event_t *e)
{
    ed_close();
}

/**
 * @brief Erase: the selected cell.
 * @param e  the event
 */
static void ed_erase_cb(lv_event_t *e)
{
    if (ed_sel >= 0) {
        ed_val[ed_sel] = 0;
    }
    ed_refresh();
}

/**
 * @brief Clear: all of it.
 * @param e  the event
 */
static void ed_clear_cb(lv_event_t *e)
{
    memset(ed_val, 0, sizeof(ed_val));
    ed_sel = 0;
    ed_refresh();
}

/* the check runs in its own task: the solver recurses a level per empty cell (the UI task's stack is small) */
static volatile int ed_check_state;                 /**< 0 idle, 1 checking, 2 done */
static volatile int ed_check_n;                     /**< the solutions found (at most 2) */
static volatile int ed_check_level;                 /**< its level (gen_level_t), -1: not one solution */
static uint8_t ed_check_cells[81];      /**< the puzzle being checked */
static lv_timer_t *ed_check_timer;      /**< waits for the check */
static TaskHandle_t ed_check_handle;                /**< its stack in PSRAM (internal RAM is short) */

/**
 * @brief The check (its own task): counts the solutions (up to 2), grades a unique puzzle, then waits to be deleted.
 * @param arg  unused
 */
static void ed_check_task(void *arg)
{
    const sudoku_geom_t *G = sudoku_geom(VAR_CLASSIC);
    int n = sudoku_count_solutions(G, ed_check_cells, 2);
    ed_check_level = n == 1 ? sudoku_grade(G, ed_check_cells) : -1;
    ed_check_n = n;
    ed_check_state = 2;
    vTaskSuspend(NULL);                             /* deleted by ed_check_done (a PSRAM-stack task can't delete itself) */
}

/**
 * @brief The check is done: the puzzle saved and opened, or why not.
 * @param t  the timer
 */
static void ed_check_done(lv_timer_t *t)
{
    if (ed_check_state != 2 || eTaskGetState(ed_check_handle) != eSuspended) {
        return;
    }
    vTaskDeleteWithCaps(ed_check_handle);
    ed_check_handle = NULL;
    lv_timer_delete(ed_check_timer);
    ed_check_timer = NULL;
    ed_check_state = 0;
    if (!scr_edit) {
        return;    /* (cancelled meanwhile) */
    }
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
    if (level < 0 || level >= GEN_LEVELS) {
        level = GEN_EXPERT;
    }
    int number = 0;
    int gid = is_stats_db_ready() ? stats_db_gen_add_own(ed_check_cells, level, &number) : -1;
    uint16_t id;
    if (gid > 0 && gid < PUZZLE_GEN_RAM_BASE - PUZZLE_GEN_BASE) {
        id = (uint16_t)(PUZZLE_GEN_BASE + gid);
    } else {
        id = gen_ram_take();                       /* no card: until power-off */
        number = s_own_ram_next++;
    }
    gen_cache_put(id, VAR_CLASSIC, level, ed_check_cells, NULL, NULL, NULL, NULL, NULL);
    gen_cache_source(id, STATS_OWN_SOURCE, number);
    char msg[96];
    snprintf(msg, sizeof(msg), "Saved as %s #%d: one solution, rated %s.", STATS_OWN_SOURCE, number,
             gen_level_name(level));
    sudoku_ui_notice(msg, 3500);
    if (scr_edit) {
        lv_obj_delete_async(scr_edit);
    }
    scr_edit = NULL;
    open_puzzle(id);
}

/**
 * @brief Check & play: the puzzle typed in is checked in the background.
 * @param e  the event
 */
static void ed_play_cb(lv_event_t *e)
{
    if (ed_check_state) {
        return;
    }
    int filled = 0;
    for (int i = 0; i < 81; i++) {
        filled += ed_val[i] != 0;
    }
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

/**
 * @brief Puzzles > Enter puzzle: the screen to type one in.
 * @param e  the event
 */
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
        make_btn(scr_edit, PAD_X, GRID_Y + 1 + (d - 1) * (h + PAD_GAP), PAD_W, h, ui_digits()[d], &lv_font_montserrat_28,
                 ed_digit_cb, (void *)(intptr_t)d);
    }
    lv_obj_t *hint = make_label(scr_edit, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(hint, SCR_W - 16);
    lv_label_set_text(hint, "Type in a Classic puzzle's given digits, then Check & play: it needs exactly one solution.");
    lv_obj_set_pos(hint, 8, BOT_Y + (BOT_H - 18) / 2);
    ed_refresh();
    lv_screen_load(scr_edit);
}

/* (documented in ui_internal.h) */
void new_menu_cb(lv_event_t *e)
{
    const ctx_item_t ITEMS[] = {
        {LV_SYMBOL_PLUS, "New puzzle", new_puzzle_cb, false, false},
        {LV_SYMBOL_KEYBOARD, "Enter puzzle", enter_cb, false, false},      /* type one in from a book */
        {LV_SYMBOL_EYE_OPEN, "Learn", learn_cb, false, false},             /* practise a technique */
        {
            is_puzzles_starred() ? LV_SYMBOL_LIST : SYMBOL_STAR, is_puzzles_starred() ? "All puzzles" : "Starred puzzles",
            list_star_cb, false, false
        },                                      /* the starred ones in the right pane, or back */
    };
    ctx_menu_toggle(list_new_btn(), ITEMS, (int)(sizeof(ITEMS) / sizeof(ITEMS[0])));
}

/* ---- accessors: the other UI files reach this file's state through these */
const char *const *learn_names(void) { return LEARN_NAMES; }
lv_obj_t *screen_edit(void) { return scr_edit; }
