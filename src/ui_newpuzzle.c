/**
 * @file ui_newpuzzle.c
 * @ingroup ui_game
 * @brief New puzzle's generator: the task, its progress, the devctl benchmark; the daily puzzle's button (the
 *        dialog: ui_gen_dialog.c; Enter a puzzle: ui_enter_puzzle.c).
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void gen_task(void *arg);
static void gen_poll_cb(lv_timer_t *t);
static void gen_start_ex(int variant, int level, uint32_t seed, const char *day);


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
    {312, 208, 2438, 2081}, /* Sandwich */
    {1, 1, 1, 1},           /* Kids 4x4 (Easy and Medium only) */
    {7, 94, 326, 191},      /* Anti-King */
    {1020, 1045, 3376, 1803}, /* Non-Consecutive */
    {7, 43, 168, 119},      /* Disjoint Groups */
    {6, 93, 337, 186},      /* Asterisk */
    {28, 116, 352, 189},    /* Kropki */
    {15, 133, 302, 1743},   /* XV */
    {7, 24, 197, 489},      /* Greater-Than */
    {9, 72, 204, 600},      /* Quadruple */
    {9, 320, 518, 497},   /* Arrow */
    {9, 174, 525, 477},   /* Renban */
    {9, 261, 991, 564}, /* German Whispers */
    {38, 159, 677, 304},    /* Skyscraper */
    {15, 471, 1102, 1225},  /* Little Killer */
    {11, 284, 905, 756},    /* Palindrome (2026-11-22) */
    {21, 403, 1070, 1206},  /* Region Sum Lines */
    {27, 314, 1181, 875},   /* X-Sums (its rule tabled, 2026-11-23: Hard was 3012) */
    {12, 86, 186, 946},     /* Zipper Lines (2026-11-22; lines that don't cross, logic-first digging) */
    {15, 112, 641, 262},    /* Frame (re-measured 2026-11-22) */
    {17, 90, 302, 1362},    /* Between Lines (the same) */
    {15, 85, 223, 829},     /* Entropic Lines (the same) */
    {13, 81, 192, 905},     /* Modular Lines */
    {9, 79, 333, 242},      /* Center Dot */
    {37, 117, 1406, 1360},  /* Outside Sudoku (2026-11-22) */
    {21, 168, 788, 255},   /* Rossini */
    {274, 512, 798, 839},   /* Diagonal Non-Consecutive */
    {12, 271, 735, 713},  /* Dutch Whispers (2026-11-22) */
    {9, 187, 784, 754},     /* Parity Lines */
    {11, 188, 696, 535},    /* Slow Thermo */
    {13, 82, 212, 1005},    /* Fortress (2026-11-22) */
    {673, 472, 758, 944},   /* Argyle */
    {20, 73, 182, 438},     /* Nabner (2026-11-23) */
    {26, 382, 1147, 1081},  /* Numbered Rooms */
    {25, 154, 548, 822},    /* Sum Skyscrapers */
    {14, 77, 187, 1570},    /* Lockout Lines (2026-11-23; its pair check capped: Expert was 2020) */
    {49, 395, 292, 119},    /* Sumless Killer */
    {11, 78, 218, 1172},    /* Double Arrow */
    {11, 405, 1186, 590},   /* Diagonal Consecutive (2026-11-23) */
    {12, 100, 156, 1806},   /* 10 Lines (its rule in bit shifts, 2026-11-23: Expert was 5321) */
    {6, 86, 214, 198},      /* Girandola */
    {19, 126, 447, 288},   /* 159 Sudoku (2026-11-23) */
    {23, 86, 3988, 3988},  /* Product Killer (no Expert: Hard's) */
    {23, 197, 425, 271},   /* Extra Regions */
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
    const char *cages = gen_var_req == VAR_KILLER || gen_var_req == VAR_SUMLESS || gen_var_req == VAR_PRODUCT ||
                        gen_var_req == VAR_REGIONS || gen_var_req == VAR_QUAD || gen_var_req == VAR_LK ? gen_cages : NULL;
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
