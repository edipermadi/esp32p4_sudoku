/* ui_sudoku.c split: palette, layout, widgets and helpers, the generated-puzzle cache, context menus. See ui_sudoku.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static bool gen_own_name(uint16_t id, char *buf, size_t n);
static bool gen_lookup_full(uint16_t id, uint8_t *givens, uint8_t *regions, char *cages, char *marks, char *thermos, int *variant, int *level, char day[12]);
static int puzzle_variant(uint16_t id);
static void ctx_backdrop_cb(lv_event_t *e);
static void ctx_item_cb(lv_event_t *e);

/* ------------------------------------------------------------- palette */
/* palette_t and the C_* colour macros are in ui_palette.h (shared with ui_wifi.c) */
static const palette_t PAL_DARK = {
    .bg = 0x101218, .card = 0x1B1E27, .card2 = 0x2A2F3D, .text = 0xEDEFF4, .muted = 0x8B92A5,
    .accent = 0x4F8CFF, .green = 0x3DD68C, .orange = 0xFFA94D, .red = 0xFF6B6B,
    .grid_thick = 0x8B92A5, .grid_thin = 0x3A3F4E,
    .cell = 0x1B1E27, .cell_given = 0x22262F, .cell_peer = 0x272D3D, .cell_same = 0x2F3F63,
    .cell_sel = 0x3A5BA0, .cell_bad = 0x4A2329, .cell_solved = 0x173A2A, .digit_user = 0x7FB0FF,
    .list_solved = 0x1E7A4F, .list_active = 0xA0621C, .notice = 0x5A2A1A, .scrim_opa = LV_OPA_60,
};

static const palette_t PAL_LIGHT = {
    .bg = 0xF2F3F7, .card = 0xFFFFFF, .card2 = 0xE2E5EC, .text = 0x1B1E27, .muted = 0x6B7285,
    .accent = 0x2F6FE0, .green = 0x1E9E62, .orange = 0xD9822B, .red = 0xD93A3A,
    .grid_thick = 0x3A3F4E, .grid_thin = 0xC5CAD6,
    .cell = 0xFFFFFF, .cell_given = 0xEEF0F4, .cell_peer = 0xE3E9F5, .cell_same = 0xC9D8F5,
    .cell_sel = 0x9DBBF2, .cell_bad = 0xF8D7DA, .cell_solved = 0xD4F1E2, .digit_user = 0x2F6FE0,
    .list_solved = 0x2FA36B, .list_active = 0xD98A2B, .notice = 0xFFE8D6, .scrim_opa = LV_OPA_40,
};

static const palette_t *ui_pal = &PAL_DARK;
static palette_t s_pal;                   /* the active scheme with the chosen accent (palette_build) */

/* accent colours (Settings): dark-mode and light-mode shades; 0 = the schemes' own blue */
static const uint32_t ACCENTS[ACCENT_COUNT][2] = {
    {0x4F8CFF, 0x2F6FE0}, {0x2DD4BF, 0x0D9488}, {0x4ADE80, 0x16A34A}, {0xA78BFA, 0x7C3AED}, {0xF472B6, 0xDB2777},
};

/* the backlight for "on": the day's setting, or less at night */
int effective_brightness(void)
{
    int b = store_brightness();
    if (is_ui_night() && store_night_brightness() < b) {
        b = store_night_brightness();
    }
    return b;
}

uint32_t mix_hex(uint32_t a, uint32_t b, int pct)   /* pct % of a over b */
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        out |= (uint32_t)((ca * pct + cb * (100 - pct)) / 100) << sh;
    }
    return out;
}

/* the dark or light scheme, with the accent and the cell highlights made from it */
void palette_build(bool dark, int accent)
{
    s_pal = dark ? PAL_DARK : PAL_LIGHT;
    if (accent > 0 && accent < ACCENT_COUNT) {
        uint32_t a = ACCENTS[accent][dark ? 0 : 1];
        s_pal.accent = a;
        s_pal.cell_sel = mix_hex(a, s_pal.cell, dark ? 55 : 45);
        s_pal.cell_same = mix_hex(a, s_pal.cell, 25);
        s_pal.cell_peer = mix_hex(a, s_pal.cell, dark ? 8 : 10);
        s_pal.digit_user = dark ? mix_hex(a, 0xFFFFFF, 75) : a;
    }
    if (is_store_cvd()) {
        /* Colour-blind friendly (Settings > Display): the Okabe-Ito colours. Red and green
         * look alike to most colour-blind people; vermillion (mistakes), bluish green (solved,
         * complete) and yellow-orange (in progress) stay apart, and apart from the blues,
         * in lightness as well as hue. */
        if (dark) {
            s_pal.red = 0xFF8A3D;
            s_pal.green = 0x3CC9A7;
            s_pal.orange = 0xF2B638;
            s_pal.cell_bad = 0x55300F;
            s_pal.cell_solved = 0x113D36;
            s_pal.list_solved = 0x0E7D66;
            s_pal.list_active = 0xA87C10;
        } else {
            s_pal.red = 0xD55E00;
            s_pal.green = 0x00876A;
            s_pal.orange = 0xC98A00;
            s_pal.cell_bad = 0xFBDCC4;
            s_pal.cell_solved = 0xCDEFE6;
            s_pal.list_solved = 0x009E73;
            s_pal.list_active = 0xE69F00;
        }
    }
    ui_pal = &s_pal;
}

/* the players' colours (by their names on leaderboards) */
lv_color_t player_color(int k)
{
    static const uint32_t C[STATS_PLAYER_COLORS] = {0x3E7BFA, 0xE5484D, 0x30A46C, 0xF5A524,
                                                    0x9E6CF0, 0xE93D82, 0x12A594, 0x8B7355
                                                   };
    return lv_color_hex(C[(k % STATS_PLAYER_COLORS + STATS_PLAYER_COLORS) % STATS_PLAYER_COLORS]);
}

/* a small round dot in a player's colour */
lv_obj_t *player_dot(lv_obj_t *parent, int color, int size)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, size, size);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, player_color(color), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_clickable(d, false);
    return d;
}

/* ------------------------------------------------------------- widgets */

/* a player in that list, by id (non-zero) or else by name (names are unique, ignoring case);
 * NULL: not there (yet) */
const stats_player_t *player_find(int32_t id, const char *name)
{
    for (int i = 0; i < players_count(); i++) {
        if (id ? players_all()[i].id == id : (name && !strcasecmp(players_all()[i].name, name))) {
            return &players_all()[i];
        }
    }
    return NULL;
}

/* A player's avatar shrunk to size px or, without one, a dot (dot px) of their colour, centred
 * in a size x size box that takes no taps. color < 0: the colour from the player list. */
lv_obj_t *player_badge(lv_obj_t *parent, const stats_player_t *pl, int color, int size, int dot)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    if (color < 0) {
        color = pl ? pl->color : 0;
    }
    if (pl && pl->avatar && pl->avatar <= AVATAR_COUNT) {
        lv_obj_t *im = lv_image_create(box);
        lv_image_set_src(im, avatar_image(pl->avatar - 1));
        if (size < AVATAR_SIZE) {
            lv_image_set_scale(im, (uint32_t)(256 * size / AVATAR_SIZE));
        }
        lv_obj_center(im);
    } else {
        lv_obj_center(player_dot(box, color, dot));
    }
    return box;
}
/* collections imported from puzzles/ on the card (stats_db_import_puzzles) */
static stats_import_t s_imports[STATS_MAX_IMPORTS];
static int s_nimports;
/* collections made on the board (the game screen's Save), after the imported ones */
static stats_saved_t *s_saved;            /* STATS_MAX_SAVED of them, PSRAM */

/* ============================================================= helpers */

void fmt_time(char *buf, size_t n, uint32_t s)
{
    if (s >= 3600) {
        snprintf(buf, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    } else {
        snprintf(buf, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    }
}

/* "st", "nd", "rd" or "th" after n */
const char *ordinal(int n)
{
    int t = n % 100;
    if (t >= 11 && t <= 13) {
        return "th";
    }
    return n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
}
static gen_cache_t *s_gen_cache;          /* GEN_CACHE entries, PSRAM */
static int s_gen_cache_next;
static uint16_t s_gen_ram_next = PUZZLE_GEN_RAM_BASE;

/* the next RAM-only puzzle id (no card: they last until power-off) */
uint16_t gen_ram_take(void)
{
    uint16_t id = s_gen_ram_next++;
    if (s_gen_ram_next == 0xFFFF) {
        s_gen_ram_next = PUZZLE_GEN_RAM_BASE;
    }
    return id;
}

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

/* a typed-in puzzle's collection and number from the cache (not imported ones: import_of) */
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

/* givens, variant, level and daily date of a generated puzzle (any may be NULL); false if unknown */
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

bool gen_lookup_day(uint16_t id, uint8_t *givens, int *variant, int *level, char day[12])
{
    return gen_lookup_full(id, givens, NULL, NULL, NULL, NULL, variant, level, day);
}

bool gen_lookup(uint16_t id, uint8_t *givens, int *variant, int *level)
{
    return gen_lookup_full(id, givens, NULL, NULL, NULL, NULL, variant, level, NULL);
}

/* variant of any puzzle: the built-in ones are classic */
static int puzzle_variant(uint16_t id)
{
    int v = VAR_CLASSIC;
    if (puzzle_is_generated(id)) {
        gen_lookup(id, NULL, &v, NULL);
    }
    return v;
}

/* The board of any puzzle: a fixed variant's geometry, or for a Jigsaw
 * puzzle its regions built into buf. *variant (may be NULL) is set too. */
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
    if (v == VAR_KILLER && buf && sudoku_geom_killer_text(cages, buf)) {
        return buf;
    }
    if (v == VAR_QUAD && buf && sudoku_geom_quad_text(cages, buf)) {
        return buf;    /* (its circles are in cages) */
    }
    if ((v == VAR_EVENODD || v == VAR_CONSEC || v == VAR_KROPKI || v == VAR_XV || v == VAR_GT) && buf &&
            sudoku_geom_marks(v, marks, buf)) {
        return buf;
    }
    if ((v == VAR_THERMO || v == VAR_ARROW || v == VAR_RENBAN || v == VAR_WHISPER) && buf &&
            sudoku_geom_lines_text(v, thermos, buf)) {
        return buf;
    }
    if (v == VAR_SANDWICH && buf && sudoku_geom_sandwich_text(marks, buf)) {
        return buf;    /* (its sums are in marks) */
    }
    if (v == VAR_SKY && buf && sudoku_geom_sky_text(marks, buf)) {
        return buf;    /* (its clues too) */
    }
    if (v == VAR_LK && buf && sudoku_geom_lk_text(cages, buf)) {
        return buf;    /* (its sums in cages) */
    }
    return sudoku_geom(v);                          /* (a broken map or cages: a classic board) */
}

/* the givens of any puzzle, built-in or generated */
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

/* the imported collection a puzzle is in (-1: none) and its number there */
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

/* the list's collections: the built-in ones, then the imported ones, then the saved ones */
int coll_total(void) { return PUZZLE_COLLECTION_COUNT + s_nimports + ui_nsaved(); }
int coll_saved_index(int c) { return c - PUZZLE_COLLECTION_COUNT - s_nimports; }   /* >= 0: a saved one */
int coll_tab(int c) { return c < PUZZLE_COLLECTION_COUNT ? 0 : 1; }
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
/* built-in and imported collections are runs of ids */
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
/* puzzle i (from 0) of collection c */
uint16_t coll_puzzle(int c, int i)
{
    int k = c >= PUZZLE_COLLECTION_COUNT ? coll_saved_index(c) : -1;
    return k >= 0 ? s_saved[k].puzzles[i] : (uint16_t)(coll_first(c) + i);
}
/* where puzzle id is in collection c, -1: not there */
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
/* the collection of a puzzle (built-in or imported); -1 for others */
int coll_of(uint16_t id)
{
    if (!puzzle_is_generated(id)) {
        return id < PUZZLE_COUNT ? puzzle_collection_of(id) : -1;
    }
    int k = import_of(id, NULL);
    return k < 0 ? -1 : PUZZLE_COLLECTION_COUNT + k;
}

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

void puzzle_name(uint16_t id, char *buf, size_t n)
{
    int num, k = import_of(id, &num);
    if (k >= 0) {                                   /* imported: "Collection #12" */
        snprintf(buf, n, "%s #%d", s_imports[k].name, num);
        return;
    }
    if (puzzle_is_generated(id)) {                  /* (looked up first, so the cache knows its source) */
        gen_lookup(id, NULL, NULL, NULL);
        if (gen_own_name(id, buf, n)) {
            return;    /* typed in: "My puzzles #3" */
        }
    }
    if (puzzle_is_generated(id)) {
        int lv = -1, var = VAR_CLASSIC;
        char day[12] = "";
        gen_lookup_day(id, NULL, &var, &lv, day);
        if (day[0]) {                                     /* "Daily Hard, 18 Nov" */
            struct tm tm = {0};
            char when[16] = "";
            if (sscanf(day, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
                tm.tm_year -= 1900;
                tm.tm_mon -= 1;
                strftime(when, sizeof(when), "%d %b", &tm);
            }
            char kind[32];
            daily_kind_text(var, lv, kind, sizeof(kind));
            snprintf(buf, n, "Daily %s, %s", kind, when);
            return;
        }
        unsigned num = id >= PUZZLE_GEN_RAM_BASE ? (unsigned)(id - PUZZLE_GEN_RAM_BASE + 1)
                       : (unsigned)(id - PUZZLE_GEN_BASE);
        snprintf(buf, n, "%s %s #%u", stats_kind_prefix(stats_kind_of(var, false)),
                 lv >= 0 && lv < GEN_LEVELS ? gen_level_name(lv) : "", num);
        return;
    }
    int c = puzzle_collection_of(id);
    snprintf(buf, n, "%s #%u", puzzle_collection(c)->name, (unsigned)(id - puzzle_collection(c)->first + 1));
}

lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                   const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, C_CARD, 0);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (text) {
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, C_TEXT, 0);
        lv_obj_center(l);
    }
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    return b;
}

lv_obj_t *make_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, C_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s, false);
    return s;
}

lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

/* ------------------------------------------------------------- icon buttons, context menus */
/* an icon button of a title row (a toggle, or a menu's): accent with a white icon while on */
void icon_btn_set(lv_obj_t *b, bool on, lv_color_t off_c)
{
    lv_obj_set_state(b, LV_STATE_CHECKED, on);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), on ? lv_color_white() : off_c, 0);
}
static lv_obj_t *ctx_menu, *ctx_btn;    /* the open menu's backdrop, and its button */

void ctx_menu_close(void)
{
    if (!ctx_menu) {
        return;
    }
    lv_obj_delete_async(ctx_menu);
    ctx_menu = NULL;
    if (ctx_btn) {
        icon_btn_set(ctx_btn, false, C_TEXT);
    }
    ctx_btn = NULL;
}

static void ctx_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) {
        ctx_menu_close();
    }
}

static void ctx_item_cb(lv_event_t *e)
{
    void (*cb)(lv_event_t *) = (void (*)(lv_event_t *))lv_event_get_user_data(e);
    ctx_menu_close();
    cb(NULL);
}

/* the menu's card, w wide, under btn (any open menu closes first); its content is the caller's */
lv_obj_t *ctx_card_open(lv_obj_t *btn, int w)
{
    ctx_menu_close();
    ctx_menu = lv_obj_create(lv_layer_top());       /* the backdrop: invisible, catches taps outside */
    lv_obj_remove_style_all(ctx_menu);
    lv_obj_set_size(ctx_menu, SCR_W, SCR_W);
    lv_obj_set_clickable(ctx_menu, true);
    lv_obj_add_event_cb(ctx_menu, ctx_backdrop_cb, LV_EVENT_CLICKED, NULL);
    ctx_btn = btn;
    icon_btn_set(btn, true, C_TEXT);

    lv_area_t a;
    lv_obj_get_coords(btn, &a);
    lv_obj_t *card = lv_obj_create(ctx_menu);
    lv_obj_set_pos(card, LV_MAX(8, a.x2 + 1 - w), a.y2 + 7);   /* (a button on the left: from the edge) */
    lv_obj_set_size(card, w, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_CARD2, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_shadow_offset_y(card, 4, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(card, false);
    return card;
}

/* opens btn's menu (or closes it, when it is open) */
void ctx_menu_toggle(lv_obj_t *btn, const ctx_item_t *items, int n)
{
    bool was = ctx_menu && ctx_btn == btn;
    ctx_menu_close();
    if (was) {
        return;
    }
    lv_obj_t *card = ctx_card_open(btn, 260);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = make_btn(card, 0, 0, LV_PCT(100), 56, NULL, NULL, items[i].disabled ? NULL : ctx_item_cb,
                               (void *)items[i].cb);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_clickable(b, !items[i].disabled);
        if (items[i].disabled) {
            lv_obj_set_style_opa(b, LV_OPA_40, 0);
        }
        lv_obj_t *ic = make_label(b, &font_icons_20, items[i].danger ? C_RED : C_ACCENT);   /* own column; LVGL's
                                                                     symbols and a few FontAwesome ones (Montserrat 20 fallback) */
        lv_label_set_text(ic, items[i].icon);
        lv_obj_set_width(ic, 32);
        lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *l = make_label(b, &lv_font_montserrat_18, items[i].danger ? C_RED : C_TEXT);
        lv_label_set_text(l, items[i].text);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 8 + 32 + 12, 0);
    }
}

/* ------------------------------------------------------------- dialog text */
/* Text that wraps to its parent's width, left-aligned (dialogs). Font: NULL
 * = the parent's (inherited, like a label's). */
lv_obj_t *make_wrap_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    if (font) {
        lv_obj_set_style_text_font(l, font, 0);
    }
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

/* ---- accessors: the other UI files reach this file's state through these */
const palette_t *ui_palette_dark(void) { return &PAL_DARK; }
const palette_t *ui_palette(void) { return ui_pal; }
int ui_nimports(void) { return s_nimports; }
const stats_saved_t *ui_saved(void) { return s_saved; }
const gen_cache_t *ui_gen_cache(void) { return s_gen_cache; }
void ui_set_gen_cache(gen_cache_t *v) { s_gen_cache = v; }
