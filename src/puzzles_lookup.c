/**
 * @file puzzles_lookup.c
 * @ingroup puzzles_lookup
 * @brief Any puzzle by its id: the generated-puzzle cache, the givens and board of a puzzle, its name, and the
 *        collections listed on the Puzzles screen, without LVGL.
 */
#include "puzzles_lookup.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "daily_core.h"

/* this file's own functions, in any order */
static bool gen_own_name(uint16_t id, char *buf, size_t n);
static bool gen_lookup_full(uint16_t id, uint8_t *givens, uint8_t *regions, char *cages, char *marks, char *thermos,
                            int *variant, int *level, char day[12]);

/** @brief collections imported from puzzles/ on the card (stats_db_import_puzzles) */
static stats_import_t s_imports[STATS_MAX_IMPORTS];
static int s_nimports;                    /**< how many */
/** @brief collections made on the board (the game screen's Save), after the imported ones */
static stats_saved_t *s_saved;            /**< STATS_MAX_SAVED of them, PSRAM */
static int s_nsaved;                      /**< the saved collections (Puzzles > Saved) */
static gen_cache_t *s_gen_cache;          /**< GEN_CACHE entries, PSRAM */
static int s_gen_cache_next;              /**< the cache slot used next (round) */
static uint16_t s_gen_ram_next = PUZZLE_GEN_RAM_BASE;   /**< the next RAM-only puzzle id */

/* (documented in puzzles_lookup.h) */
uint16_t gen_ram_take(void)
{
    uint16_t id = s_gen_ram_next++;
    if (s_gen_ram_next == 0xFFFF) {
        s_gen_ram_next = PUZZLE_GEN_RAM_BASE;
    }
    return id;
}

/* (documented in puzzles_lookup.h) */
void gen_cache_put(uint16_t id, int variant, int level, const uint8_t givens[81], const uint8_t *regions,
                   const char *cages, const char *marks, const char *thermos, const char *day)
{
    for (int i = 0; i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id == id) {
            return;
        }
    }
    s_gen_cache[s_gen_cache_next].id = id;
    if (regions) {
        memcpy(s_gen_cache[s_gen_cache_next].regions, regions, 81);
    } else {
        memset(s_gen_cache[s_gen_cache_next].regions, 0, 81);
    }
    snprintf(s_gen_cache[s_gen_cache_next].cages, STATS_CAGES_LEN, "%s", cages ? cages : "");
    snprintf(s_gen_cache[s_gen_cache_next].marks, 82, "%s", marks ? marks : "");
    snprintf(s_gen_cache[s_gen_cache_next].thermos, STATS_THERMOS_LEN, "%s", thermos ? thermos : "");
    s_gen_cache[s_gen_cache_next].source[0] = 0;
    s_gen_cache[s_gen_cache_next].number = 0;
    s_gen_cache[s_gen_cache_next].variant = (uint8_t)variant;
    s_gen_cache[s_gen_cache_next].level = (uint8_t)level;
    memcpy(s_gen_cache[s_gen_cache_next].givens, givens, 81);
    snprintf(s_gen_cache[s_gen_cache_next].day, sizeof(s_gen_cache[0].day), "%s", day ? day : "");
    s_gen_cache_next = (s_gen_cache_next + 1) % GEN_CACHE;
}

/* (documented in puzzles_lookup.h) */
void gen_cache_source(uint16_t id, const char *source, int number)
{
    for (int i = 0; i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id != id) {
            continue;
        }
        snprintf(s_gen_cache[i].source, sizeof(s_gen_cache[i].source), "%s", source);
        s_gen_cache[i].number = number;
    }
}

/**
 * @brief a typed-in puzzle's collection and number from the cache (not imported ones: import_of)
 * @param[in] id  the puzzle
 * @param[out] buf  gets "My puzzles #3"
 * @param[in] n  its size
 * @return false: not a typed-in one
 */
static bool gen_own_name(uint16_t id, char *buf, size_t n)
{
    for (int i = 0; i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id == id && s_gen_cache[i].source[0]) {
            snprintf(buf, n, "%s #%d", s_gen_cache[i].source, s_gen_cache[i].number);
            return true;
        }
    }
    return false;
}

/**
 * @brief givens, variant, level and daily date of a generated puzzle (any may be NULL); false if unknown
 * @param[in] id  the puzzle
 * @param[out] givens  gets them
 * @param[out] regions  gets Jigsaw's regions
 * @param[out] cages  gets Killer's cages
 * @param[out] marks  gets the marks
 * @param[out] thermos  gets the lines
 * @param[out] variant  gets it
 * @param[out] level  gets it
 * @param[out] day  gets the daily date ("" if none)
 * @return false if unknown
 */
static bool gen_lookup_full(uint16_t id, uint8_t *givens, uint8_t *regions, char *cages, char *marks, char *thermos,
                            int *variant, int *level, char day[12])
{
    for (int i = 0; i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id == id) {
            if (givens) {
                memcpy(givens, s_gen_cache[i].givens, 81);
            }
            if (regions) {
                memcpy(regions, s_gen_cache[i].regions, 81);
            }
            if (cages) {
                snprintf(cages, STATS_CAGES_LEN, "%s", s_gen_cache[i].cages);
            }
            if (marks) {
                snprintf(marks, 82, "%s", s_gen_cache[i].marks);
            }
            if (thermos) {
                snprintf(thermos, STATS_THERMOS_LEN, "%s", s_gen_cache[i].thermos);
            }
            if (variant) {
                *variant = s_gen_cache[i].variant;
            }
            if (level) {
                *level = s_gen_cache[i].level;
            }
            if (day) {
                snprintf(day, 12, "%s", s_gen_cache[i].day);
            }
            return true;
        }
    }
    if (id >= PUZZLE_GEN_RAM_BASE) {
        return false;    /* RAM-only, and gone */
    }
    uint8_t g[81], reg[81] = {0};
    static char *cg;                                     /* STATS_CAGES_LEN, PSRAM (LVGL task only) */
    if (!cg && !(cg = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) {
        return false;
    }
    int lv, var = VAR_CLASSIC;
    char d[12];
    char mk[82] = "";
    static char th[STATS_THERMOS_LEN];                   /* (LVGL task only) */
    th[0] = 0;
    if (!stats_db_gen_get(id - PUZZLE_GEN_BASE, g, reg, cg, mk, th, &var, &lv, d)) {
        return false;
    }
    if (var < 0 || var >= VAR_COUNT) {
        var = VAR_CLASSIC;
    }
    gen_cache_put(id, var, lv, g, reg, cg, mk, th, d);
    if (var == VAR_CLASSIC && !d[0]) {              /* typed in on the board ("My puzzles #3")? */
        char src[STATS_IMPORT_NAME];
        int num;
        if (stats_db_gen_source(id - PUZZLE_GEN_BASE, src, &num)) {
            gen_cache_source(id, src, num);
        }
    }
    if (marks) {
        snprintf(marks, 82, "%s", mk);
    }
    if (thermos) {
        snprintf(thermos, STATS_THERMOS_LEN, "%s", th);
    }
    if (givens) {
        memcpy(givens, g, 81);
    }
    if (regions) {
        memcpy(regions, reg, 81);
    }
    if (cages) {
        snprintf(cages, STATS_CAGES_LEN, "%s", cg);
    }
    if (variant) {
        *variant = var;
    }
    if (level) {
        *level = lv;
    }
    if (day) {
        snprintf(day, 12, "%s", d);
    }
    return true;
}

/* (documented in puzzles_lookup.h) */
bool gen_lookup_day(uint16_t id, uint8_t *givens, int *variant, int *level, char day[12])
{
    return gen_lookup_full(id, givens, NULL, NULL, NULL, NULL, variant, level, day);
}

/* (documented in collections.h) */
bool gen_lookup(uint16_t id, uint8_t *givens, int *variant, int *level)
{
    return gen_lookup_full(id, givens, NULL, NULL, NULL, NULL, variant, level, NULL);
}

/* (documented in puzzles_lookup.h) */
const sudoku_geom_t *puzzle_geom(uint16_t id, sudoku_geom_t *buf, int *variant)
{
    int v = VAR_CLASSIC;
    uint8_t reg[81];
    static char *cages;                             /* STATS_CAGES_LEN, PSRAM (LVGL task only) */
    if (!cages && !(cages = heap_caps_malloc(STATS_CAGES_LEN, MALLOC_CAP_SPIRAM))) {
        return sudoku_geom(VAR_CLASSIC);
    }
    cages[0] = 0;
    char marks[82] = "";
    static char thermos[STATS_THERMOS_LEN];         /* (LVGL task only) */
    thermos[0] = 0;
    if (puzzle_is_generated(id)) {
        gen_lookup_full(id, NULL, reg, cages, marks, thermos, &v, NULL, NULL);
    }
    if (variant) {
        *variant = v;
    }
    if (v == VAR_JIGSAW && buf && sudoku_geom_jigsaw(reg, buf)) {
        return buf;
    }
    if ((v == VAR_KILLER || v == VAR_SUMLESS) && buf && sudoku_geom_killer_text(cages, buf)) {
        return buf;
    }
    if (v == VAR_PRODUCT && buf && sudoku_geom_product_text(cages, buf)) {
        return buf;
    }
    if (v == VAR_REGIONS && buf && sudoku_geom_regions_text(cages, buf)) {
        return buf;
    }
    if (v == VAR_QUAD && buf && sudoku_geom_quad_text(cages, buf)) {
        return buf;    /* (its circles are in cages) */
    }
    if (v == VAR_MATHRAX && buf && sudoku_geom_mathrax_text(cages, buf)) {
        return buf;    /* (and Mathrax's) */
    }
    if ((v == VAR_EVENODD || v == VAR_CONSEC || v == VAR_KROPKI || v == VAR_XV || v == VAR_GT || v == VAR_FORTRESS ||
            v == VAR_DIAGCONSEC) &&
            buf &&
            sudoku_geom_marks(v, marks, buf)) {
        return buf;
    }
    if ((v == VAR_THERMO || v == VAR_ARROW || v == VAR_RENBAN || v == VAR_WHISPER || v == VAR_PALINDROME ||
            v == VAR_REGIONSUM || v == VAR_ZIPPER || v == VAR_BETWEEN || v == VAR_ENTROPIC || v == VAR_MODULAR ||
            v == VAR_DUTCH || v == VAR_PARITY || v == VAR_SLOW || v == VAR_NABNER || v == VAR_LOCKOUT ||
            v == VAR_DARROW || v == VAR_TEN || v == VAR_PILL || v == VAR_AVG) &&
            buf &&
            sudoku_geom_lines_text(v, thermos, buf)) {
        return buf;
    }
    if (v == VAR_SANDWICH && buf && sudoku_geom_sandwich_text(marks, buf)) {
        return buf;    /* (its sums are in marks) */
    }
    if (v == VAR_PSKY && buf && sudoku_geom_psky_text(marks, buf)) {
        return buf;    /* (its clues are in marks) */
    }
    if (v == VAR_SKY && buf && sudoku_geom_sky_text(marks, buf)) {
        return buf;    /* (its clues too) */
    }
    if (v == VAR_XSUM && buf && sudoku_geom_xsum_text(marks, buf)) {
        return buf;    /* (and X-Sums') */
    }
    if (v == VAR_NROOMS && buf && sudoku_geom_nroom_text(marks, buf)) {
        return buf;    /* (and Numbered Rooms') */
    }
    if (v == VAR_SKYSUM && buf && sudoku_geom_skysum_text(marks, buf)) {
        return buf;    /* (and Sum Skyscrapers') */
    }
    if (v == VAR_B9 && buf && sudoku_geom_b9_text(marks, buf)) {
        return buf;    /* (and Before Nine's) */
    }
    if (v == VAR_N9 && buf && sudoku_geom_n9_text(marks, buf)) {
        return buf;    /* (and Next to Nine's) */
    }
    if (v == VAR_BF && buf && sudoku_geom_bf_text(marks, buf)) {
        return buf;    /* (Battlefield's clues are in marks) */
    }
    if (v == VAR_S9 && buf && sudoku_geom_s9_text(marks, buf)) {
        return buf;    /* (Search Nine's arrows are in marks) */
    }
    if (v == VAR_CLONE && buf && sudoku_geom_clone_text(cages, buf)) {
        return buf;    /* (its areas are in cages) */
    }
    if (v == VAR_FRAME && buf && sudoku_geom_frame_text(marks, buf)) {
        return buf;    /* (and Frame's) */
    }
    if (v == VAR_OUTSIDE && buf && sudoku_geom_outside_text(marks, buf)) {
        return buf;    /* (and Outside Sudoku's) */
    }
    if (v == VAR_ROSSINI && buf && sudoku_geom_rossini_text(marks, buf)) {
        return buf;    /* (and Rossini's arrows) */
    }
    if (v == VAR_LK && buf && sudoku_geom_lk_text(cages, buf)) {
        return buf;    /* (its sums in cages) */
    }
    return sudoku_geom(v);                          /* (a broken map or cages: a classic board) */
}

/* (documented in puzzles_lookup.h) */
bool puzzle_get(uint16_t id, uint8_t cells[81])
{
    if (!puzzle_is_generated(id)) {
        if (id >= PUZZLE_COUNT) {
            return false;
        }
        puzzle_load(id, cells);
        return true;
    }
    return gen_lookup(id, cells, NULL, NULL);
}

/* (documented in collections.h) */
int import_of(uint16_t id, int *number)
{
    int gid = (int)id - PUZZLE_GEN_BASE;
    for (int k = 0; puzzle_is_generated(id) && id < PUZZLE_GEN_RAM_BASE && k < s_nimports; k++) {
        if (gid >= s_imports[k].first_gid && gid < s_imports[k].first_gid + s_imports[k].count) {
            if (number) {
                *number = gid - s_imports[k].first_gid + 1;
            }
            return k;
        }
    }
    return -1;
}

/** @brief the list's collections: the built-in ones, then the imported ones, then the saved ones */
int coll_total(void) { return PUZZLE_COLLECTION_COUNT + s_nimports + ui_nsaved(); }
int coll_saved_index(int c) { return c - PUZZLE_COLLECTION_COUNT - s_nimports; }   /**< >= 0: a saved one */
int coll_tab(int c)
{
    return c < PUZZLE_COLLECTION_COUNT ? COLL_TAB_BUILTIN : coll_saved_index(c) >= 0 ? COLL_TAB_SAVED : COLL_TAB_IMPORTED;
}
/* (documented in collections.h) */
const char *coll_name(int c)
{
    if (c < PUZZLE_COLLECTION_COUNT) {
        return puzzle_collection(c)->name;
    }
    if (coll_saved_index(c) < 0) {
        return s_imports[c - PUZZLE_COLLECTION_COUNT].name;
    }
    return s_saved[coll_saved_index(c)].name;
}
/* (documented in collections.h) */
int coll_id(int c)
{
    if (c < PUZZLE_COLLECTION_COUNT) {
        return c + 1;
    }
    if (coll_saved_index(c) < 0) {
        return s_imports[c - PUZZLE_COLLECTION_COUNT].id;
    }
    return s_saved[coll_saved_index(c)].id;
}

/* (documented in collections.h) */
int coll_find_id(int tab, int id)
{
    for (int c = 0; c < coll_total(); c++) {
        if (coll_tab(c) == tab && coll_id(c) == id) {
            return c;
        }
    }
    return -1;
}

/* (documented in collections.h) */
int64_t coll_created(int c)
{
    if (c < PUZZLE_COLLECTION_COUNT) {
        return 0;
    }
    if (coll_saved_index(c) < 0) {
        return s_imports[c - PUZZLE_COLLECTION_COUNT].created_at;
    }
    return s_saved[coll_saved_index(c)].created_at;
}
/* (documented in collections.h) */
uint16_t coll_first(int c)
{
    if (c < PUZZLE_COLLECTION_COUNT) {
        return puzzle_collection(c)->first;
    }
    if (coll_saved_index(c) < 0) {
        return (uint16_t)(PUZZLE_GEN_BASE + s_imports[c - PUZZLE_COLLECTION_COUNT].first_gid);
    }
    return s_saved[coll_saved_index(c)].count ? s_saved[coll_saved_index(c)].puzzles[0] : 0;
}
/* (documented in collections.h) */
int coll_size(int c)
{
    if (c < PUZZLE_COLLECTION_COUNT) {
        return puzzle_collection(c)->count;
    }
    if (coll_saved_index(c) < 0) {
        return s_imports[c - PUZZLE_COLLECTION_COUNT].count;
    }
    return s_saved[coll_saved_index(c)].count;
}
/* (documented in collections.h) */
uint16_t coll_puzzle(int c, int i)
{
    int k = c >= PUZZLE_COLLECTION_COUNT ? coll_saved_index(c) : -1;
    return k >= 0 ? s_saved[k].puzzles[i] : (uint16_t)(coll_first(c) + i);
}
/* (documented in collections.h) */
int coll_find(int c, uint16_t id)
{
    if (c < 0 || c >= coll_total()) {
        return -1;
    }
    for (int i = 0; i < coll_size(c); i++) {
        if (coll_puzzle(c, i) == id) {
            return i;
        }
    }
    return -1;
}
/* (documented in collections.h) */
int coll_of(uint16_t id)
{
    if (!puzzle_is_generated(id)) {
        return id < PUZZLE_COUNT ? puzzle_collection_of(id) : -1;
    }
    int k = import_of(id, NULL);
    return k < 0 ? -1 : PUZZLE_COLLECTION_COUNT + k;
}

/* (documented in collections.h) */
void imports_reload(void)
{
    int n = is_stats_db_ready() ? stats_db_list_imports(s_imports) : -1;
    s_nimports = n < 0 ? 0 : n;
    if (!s_saved) {
        s_saved = heap_caps_calloc(STATS_MAX_SAVED, sizeof(stats_saved_t), MALLOC_CAP_SPIRAM);
    }
    int m = s_saved && is_stats_db_ready() ? stats_db_list_saved(s_saved) : -1;
    ui_set_nsaved(m < 0 ? 0 : m);
}

/* (documented in collections.h) */
void puzzle_name(uint16_t id, char *buf, size_t n)
{
    if (import_of(id, NULL) >= 0) {
        puzzle_name_from(id, VAR_CLASSIC, -1, "", 0, "", buf, n);
        return;
    }
    if (puzzle_is_generated(id)) {                  /* (looked up first, so the cache knows its source) */
        gen_lookup(id, NULL, NULL, NULL);
        if (gen_own_name(id, buf, n)) {
            return;    /* typed in: "My puzzles #3" */
        }
        int lv = -1, var = VAR_CLASSIC;
        char day[12] = "";
        gen_lookup_day(id, NULL, &var, &lv, day);
        puzzle_name_from(id, var, lv, "", 0, day, buf, n);
        return;
    }
    int c = puzzle_collection_of(id);
    snprintf(buf, n, "%s #%u", puzzle_collection(c)->name, (unsigned)(id - puzzle_collection(c)->first + 1));
}

/* (documented in collections.h) */
void puzzle_name_from(uint16_t id, int variant, int level, const char *source, int number, const char *day, char *buf,
                      size_t n)
{
    int num, k = import_of(id, &num);
    if (k >= 0) {                                   /* imported: "Collection #12" */
        snprintf(buf, n, "%s #%d", s_imports[k].name, num);
        return;
    }
    if (source && source[0]) {                      /* typed in: "My puzzles #3" */
        snprintf(buf, n, "%s #%d", source, number);
        return;
    }
    if (day && day[0]) {                            /* "Daily Hard, 18 Nov" */
        struct tm tm = {0};
        char when[16] = "";
        if (sscanf(day, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            strftime(when, sizeof(when), "%d %b", &tm);
        }
        char kind[32];
        daily_kind_text(variant, level, kind, sizeof(kind));
        snprintf(buf, n, "Daily %s, %s", kind, when);
        return;
    }
    const unsigned gnum = id >= PUZZLE_GEN_RAM_BASE ? (unsigned)(id - PUZZLE_GEN_RAM_BASE + 1)
                          : (unsigned)(id - PUZZLE_GEN_BASE);
    snprintf(buf, n, "%s %s #%u", stats_kind_prefix(stats_kind_of(variant, false)),
             level >= 0 && level < GEN_LEVELS ? gen_level_name(level) : "", gnum);
}

/* (documented in puzzles_lookup.h) */
uint16_t gen_cache_find_day(const char *day)
{
    for (int i = 0; s_gen_cache && i < GEN_CACHE; i++) {
        if (s_gen_cache[i].id && !strcmp(s_gen_cache[i].day, day)) {
            return s_gen_cache[i].id;
        }
    }
    return 0;
}

/* ---- accessors (documented in collections.h and puzzles_lookup.h) */
int ui_nimports(void) { return s_nimports; }
const stats_saved_t *ui_saved(void) { return s_saved; }
int ui_nsaved(void) { return s_nsaved; }
void ui_set_nsaved(int v) { s_nsaved = v; }
const gen_cache_t *ui_gen_cache(void) { return s_gen_cache; }
void ui_set_gen_cache(gen_cache_t *v) { s_gen_cache = v; }
