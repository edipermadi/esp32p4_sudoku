/**
 * @file ui_share.c
 * @ingroup ui_game
 * @brief Share: the puzzle as a QR code, and sent to another board by sound (Gibberlink) or received from one (the texts: share_core.c).
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void qr_draw_cb(lv_event_t *e);
static void pz_send_poll(lv_timer_t *t);
static void pz_send_cb(lv_event_t *e);
static void pz_check_fn(void *arg);
static void pz_play_cb(lv_event_t *e);
static void pz_check_poll(lv_timer_t *t);
static bool pz_take(char *line, char *why, size_t n);

/* ------------------------------------------------------------- share (QR code) */
/**
 * @brief The Share button: the puzzle as a QR code to scan with a phone. A Classic
 * (or imported) puzzle is the usual 81-digit string, givens row by row with 0
 * for empty, which most Sudoku apps can import; a variant is its name, the
 * givens and its layout, in the text forms sudoku.db keeps them in.
 */
static uint8_t *qr_code;                            /**< qrcodegen_BUFFER_LEN_MAX each, PSRAM */
static uint8_t *qr_temp;                            /**< qrcodegen's work space, PSRAM */


/**
 * @brief the code: black modules on white (whatever the theme), with a quiet zone of 3
 * @param[in] e  the draw event
 */
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

/* (documented in ui_internal.h) */
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



static lv_timer_t *pz_send_timer;       /**< waits until the parts are sent */

/**
 * @brief Once the parts are sent: says so.
 * @param[in] t  the timer
 */
static void pz_send_poll(lv_timer_t *t)
{
    if (gibberlink_sending()) {
        return;
    }
    lv_timer_delete(pz_send_timer);
    pz_send_timer = NULL;
    sudoku_ui_notice("Sent by sound.", 2500);
}

/**
 * @brief Send by sound: the puzzle's parts queued for Gibberlink.
 * @param[in] e  the event (user data: the dialog)
 */
static void pz_send_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    if (pz_send_timer || gibberlink_sending()) {
        sudoku_ui_notice("Still sending the last one.", 2500);
        return;
    }
    static char line[STATS_CAGES_LEN + 400];
    int v = VAR_CLASSIC, lv = -1;
    if (puzzle_is_generated(game_id())) {
        gen_lookup(game_id(), NULL, &v, &lv);
    }
    int len = share_pz_line(game_geo(), game_var(), lv, game_given(), line, sizeof(line));
    int parts = (len + PZ_PART - 1) / PZ_PART;
    if (!len || parts > PZ_MAX_PARTS) {
        sudoku_ui_notice("This puzzle is too long to send by sound.", 3000);
        return;
    }
    unsigned id = esp_random() & 0xFF;
    for (int i = 0; i < parts; i++) {
        char msg[176];
        share_pz_part(id, i, parts, line, len, msg, sizeof(msg));
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
/** @brief The parts of a puzzle coming by sound. */
static struct {
    int id;                                       /**< the puzzle's id, -1: nothing coming */
    int n;                                        /**< its parts */
    uint32_t got;                                 /**< bit i-1: part i is here */
    uint32_t start;                               /**< lv_tick of the first part */
    char part[PZ_MAX_PARTS][PZ_PART + 1];         /**< the parts */
} pz_rx = {.id = -1};
static pz_got_t *pz_got;                          /**< PSRAM */
static lv_timer_t *pz_check_timer;                /**< waits for the check */
static TaskHandle_t pz_check_task;                /**< the check (sudoku_grade) */

/**
 * @brief The received puzzle's check (its own task): graded, then it waits to be deleted.
 * @param[in] arg  unused
 */
static void pz_check_fn(void *arg)
{
    pz_got->graded = sudoku_grade(&pz_got->geom, pz_got->givens);
    vTaskSuspend(NULL);                           /* (deleted by pz_check_poll) */
}

static uint16_t pz_play_id;                       /**< the one just received (its dialog's Play) */

/**
 * @brief Play (the received puzzle's dialog).
 * @param[in] e  the event (user data: the dialog)
 */
static void pz_play_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
    open_puzzle(pz_play_id);
}

/**
 * @brief The received puzzle's check is done: kept in Puzzles > Saved > "Received" and offered to play, or why not.
 * @param[in] t  the timer
 */
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
    const uint8_t *regions = is_jigsaw_variant(p->variant) ? p->geom.region : NULL;
    const char *cages = p->cages[0] ? p->cages : NULL, *marks = p->marks[0] ? p->marks : NULL;
    const char *thermos = p->thermos[0] ? p->thermos : NULL;
    int number = 0;                               /* "Received #3": numbered like My puzzles */
    int gid = is_stats_db_ready() ? stats_db_gen_add_from(PZ_SOURCE, p->variant, level, p->givens, regions, cages, marks,
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
             kept ? ": it's in Puzzles > Saved > Received"
             : gid > 0 ? (CLOCK_VALID(time(NULL)) ? "" : " (not kept in Received: the clock isn't set)")
             : " (kept until power-off: no card)");
    pz_play_id = id;
    lv_obj_t *m = make_msgbox(LV_SYMBOL_AUDIO "  Puzzle received", text);
    msgbox_button(m, "Later", mbox_close_cb, false);
    msgbox_button(m, LV_SYMBOL_PLAY "  Play", pz_play_cb, true);
}

/**
 * @brief the whole line: its fields read (sdx_parse), the board made, the check started; why on failure
 * @param[in,out] line  the line
 * @param[out] why  gets why not
 * @param[in] n  its size
 * @return true if the check started
 */
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

/* (documented in ui_sudoku.h) */
bool sudoku_ui_puzzle_part(const char *args, char *out, size_t n)
{
    unsigned id;
    int i, cnt;
    const char *data;
    if (!share_pz_part_read(args, &id, &i, &cnt, &data)) {
        snprintf(out, n, "ERR pz: pz <id> <i>/<n> <part> (a part is %d characters at most)", PZ_PART);
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

/* (documented in ui_internal.h) */
void share_cb(lv_event_t *e)
{
    if (game_id() == STORE_NO_GAME) {
        return;
    }
    static char text[STATS_CAGES_LEN + 128];
    char name[48], title[64];
    puzzle_name(game_id(), name, sizeof(name));
    snprintf(title, sizeof(title), SYMBOL_QR "  %s", name);
    lv_obj_t *m = share_text(game_geo(), game_var(), game_given(), text, sizeof(text))
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
