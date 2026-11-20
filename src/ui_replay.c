/* sudoku_ui.c split: replay, screenshots, mistake insights, MP4 export. See sudoku_ui.c for the whole. */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void rp_render(void);
static void rp_seek(int k, bool keep_playing);
static void rp_timer_cb(lv_timer_t *t);
static void rp_btn_cb(lv_event_t *e);
static void rp_slider_cb(lv_event_t *e);
static void rp_close(void);
static void rp_back_cb(lv_event_t *e);
static bool rp_prepare(uint16_t id, const game_move_t *moves, int n);
static void ss_task(void *arg);
static void ss_poll_cb(lv_timer_t *t);
static void ins_close_cb(lv_event_t *e);
static lv_obj_t *ins_heading(const char *text);
static void ins_show(const stats_insights_t *in);
static void ins_poll_cb(lv_timer_t *t);
static lv_obj_t *rp_build(uint16_t id, const char *sub, const char *player);
static void rx_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px);
static void rx_cancel_cb(lv_event_t *e);
static void rx_finish(bool ok);
static int rx_k_at(int f, bool *playing);
static void rx_timer_cb(lv_timer_t *t);
static void rx_name(const stats_replay_t *r, char *out, size_t n);
static void export_start(const stats_replay_t *r);

static const uint8_t RP_SPEEDS[] = {1, 2, 4, 8};

lv_obj_t *scr_replay, *rp_return;            /* the screen, and where Back goes */
static lv_obj_t *rp_cells[81], *rp_lbl[81];
static const sudoku_geom_t *rp_geo;                 /* the replayed puzzle's variant */
static sudoku_geom_t *rp_geo_buf;                   /* PSRAM: a replayed Jigsaw's regions */
static lv_obj_t *rp_play_lbl, *rp_speed_lbl, *rp_slider, *rp_pos, *rp_title, *rp_sub;
static lv_timer_t *rp_timer;
static uint8_t rp_given[81];
static game_move_t *rp_moves;                       /* PSRAM, GAME_MOVES_MAX */
static uint32_t *rp_vt;                             /* when each move plays, gaps shortened */
static int rp_n, rp_k;
static uint32_t rp_clock, rp_last_tick;
static bool rp_playing;
static int rp_speed;
static stats_replay_t *rp_loaded;                   /* from sudoku.db (History) */

static void rp_render(void)
{
    uint8_t val[81];
    uint8_t kind[81];
    memcpy(val, rp_given, sizeof(val));
    memset(kind, MOVE_HAND, sizeof(kind));
    for (int i = 0; i < rp_k; i++) {
        val[rp_moves[i].cell] = rp_moves[i].digit;
        kind[rp_moves[i].cell] = rp_moves[i].kind;
    }
    int last = rp_k ? rp_moves[rp_k - 1].cell : -1;
    for (int i = 0; i < 81; i++) {
        if (!rp_geo->used[i]) continue;
        bool bad = false;
        for (int j = 0; j < 81 && val[i] && !bad; j++) bad = j != i && val[j] == val[i] && sudoku_geom_peer(rp_geo, i, j);
        lv_color_t bg = cell_base_bg(rp_geo, i, rp_given[i]);
        if (i == last) bg = C_CELL_SEL;
        lv_obj_set_style_bg_color(rp_cells[i], bg, 0);
        lv_color_t fg = rp_given[i] ? C_TEXT : (kind[i] == MOVE_FILL ? C_ORANGE : C_DIGIT_USER);
        if (bad && !rp_given[i]) fg = C_RED;
        lv_obj_set_style_text_color(rp_lbl[i], fg, 0);
        lv_label_set_text_static(rp_lbl[i], DIGITS[val[i] <= 9 ? val[i] : 0]);
    }
    char now[16], total[16];
    fmt_time(now, sizeof(now), (rp_k ? rp_moves[rp_k - 1].t_ms : 0) / 1000);
    fmt_time(total, sizeof(total), (rp_n ? rp_moves[rp_n - 1].t_ms : 0) / 1000);
    lv_label_set_text_fmt(rp_pos, "Move %d / %d   %s / %s", rp_k, rp_n, now, total);
    lv_slider_set_value(rp_slider, rp_k, LV_ANIM_OFF);
    lv_label_set_text(rp_play_lbl, rp_playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void rp_seek(int k, bool keep_playing)
{
    rp_k = k < 0 ? 0 : (k > rp_n ? rp_n : k);
    rp_clock = rp_k ? rp_vt[rp_k - 1] : 0;
    if (!keep_playing) rp_playing = false;
    rp_render();
}

static void rp_timer_cb(lv_timer_t *t)
{
    uint32_t now = lv_tick_get(), dt = now - rp_last_tick;
    rp_last_tick = now;
    if (!rp_playing) return;
    rp_clock += dt * RP_SPEEDS[rp_speed];
    int k = rp_k;
    while (k < rp_n && rp_vt[k] <= rp_clock) k++;
    if (k == rp_n) rp_playing = false;
    if (k != rp_k || !rp_playing) {
        rp_k = k;
        rp_render();
    }
}

static void rp_btn_cb(lv_event_t *e)
{
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case 0: rp_seek(0, false); break;                         /* start */
    case 1: rp_seek(rp_k - 1, false); break;                  /* one back */
    case 2:                                                   /* play / pause */
        if (!rp_playing && rp_k >= rp_n) rp_seek(0, false);   /* at the end: from the start */
        rp_playing = !rp_playing;
        rp_last_tick = lv_tick_get();
        rp_render();
        break;
    case 3: rp_seek(rp_k + 1, false); break;                  /* one forward */
    case 4: rp_seek(rp_n, false); break;                      /* end */
    case 5:                                                   /* speed */
        rp_speed = (rp_speed + 1) % (int)sizeof(RP_SPEEDS);
        lv_label_set_text_fmt(rp_speed_lbl, LV_SYMBOL_PLAY "  Speed %ux", RP_SPEEDS[rp_speed]);
        break;
    }
}

static void rp_slider_cb(lv_event_t *e)
{
    rp_seek(lv_slider_get_value(rp_slider), rp_playing);
}

static void rp_close(void)
{
    if (rp_timer) lv_timer_delete(rp_timer);
    rp_timer = NULL;
    lv_screen_load(rp_return ? rp_return : scr_home);
    lv_obj_delete_async(scr_replay);
    scr_replay = NULL;
}

static void rp_back_cb(lv_event_t *e)
{
    rp_close();
}

/* Loads a log for puzzle `id`: the givens, the moves and their play times. */
static bool rp_prepare(uint16_t id, const game_move_t *moves, int n)
{
    if (!rp_moves) rp_moves = heap_caps_malloc(sizeof(game_move_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    if (!rp_vt) rp_vt = heap_caps_malloc(sizeof(uint32_t) * GAME_MOVES_MAX, MALLOC_CAP_SPIRAM);
    if (!rp_moves || !rp_vt) return false;
    if (n > GAME_MOVES_MAX) n = GAME_MOVES_MAX;
    if (n > 0 && moves != rp_moves) memcpy(rp_moves, moves, sizeof(game_move_t) * n);
    rp_n = n;
    uint32_t prev_t = 0, vt = 0;
    for (int i = 0; i < n; i++) {
        uint32_t gap = rp_moves[i].t_ms >= prev_t ? rp_moves[i].t_ms - prev_t : 0;
        vt += gap > RP_MAX_GAP_MS ? RP_MAX_GAP_MS : gap;
        rp_vt[i] = vt;
        prev_t = rp_moves[i].t_ms;
    }
    if (!puzzle_get(id, rp_given)) return false;
    if (!rp_geo_buf) rp_geo_buf = heap_caps_malloc(sizeof(*rp_geo_buf), MALLOC_CAP_SPIRAM);
    rp_geo = puzzle_geom(id, rp_geo_buf, NULL);
    return true;
}
static uint16_t *ss_frame;                          /* 64-byte aligned: the encoder reads it by DMA */
static volatile int ss_state;                       /* 0 idle, 1 writing, 2 done, 3 failed */
static char ss_path[64];
static lv_timer_t *ss_timer;

static void ss_task(void *arg)
{
    bool ok = false;
    jpeg_encoder_handle_t enc = NULL;
    jpeg_encode_engine_cfg_t ecfg = {.timeout_ms = 1000};
    jpeg_encode_memory_alloc_cfg_t mcfg = {.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER};
    size_t out_cap = 0;
    uint8_t *out = jpeg_alloc_encoder_mem(SS_OUT_MAX, &mcfg, &out_cap);
    uint8_t *rgb = heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_W * 3, MALLOC_CAP_SPIRAM);
    esp_err_t err = out && rgb ? jpeg_new_encoder_engine(&ecfg, &enc) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        for (size_t i = 0; i < (size_t)SCR_W * SCR_W; i++) {   /* RGB565 -> bytes B, G, R */
            uint16_t px = ss_frame[i];
            int r = px >> 11, g = (px >> 5) & 0x3F, b = px & 0x1F;
            rgb[i * 3] = (uint8_t)(b << 3 | b >> 2);
            rgb[i * 3 + 1] = (uint8_t)(g << 2 | g >> 4);
            rgb[i * 3 + 2] = (uint8_t)(r << 3 | r >> 2);
        }
        jpeg_encode_cfg_t cfg = {
            .width = SCR_W,
            .height = SCR_W,
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB888,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV444,   /* sharp coloured text and lines */
            .image_quality = SS_QUALITY,
        };
        uint32_t len = 0;
        err = jpeg_encoder_process(enc, &cfg, rgb, SCR_W * SCR_W * 3, out, out_cap, &len);
        if (err == ESP_OK && len > 0) {
            FILE *f = fopen(ss_path, "wb");
            if (f) {
                ok = fwrite(out, 1, len, f) == len;
                ok = fclose(f) == 0 && ok;
                if (!ok) unlink(ss_path);
            }
        }
    }
    if (err != ESP_OK) printf("screenshot: JPEG encoder: %s\n", esp_err_to_name(err));
    if (enc) jpeg_del_encoder_engine(enc);
    if (out) heap_caps_free(out);
    heap_caps_free(rgb);
    ss_state = ok ? 2 : 3;
    vTaskDelete(NULL);
}

static void ss_poll_cb(lv_timer_t *t)
{
    if (ss_state < 2) return;
    lv_timer_delete(ss_timer);
    ss_timer = NULL;
    char msg[120];
    if (ss_state == 2) snprintf(msg, sizeof(msg), "Saved %s. Copy it off with Settings > USB drive.",
                                ss_path + strlen(BSP_SD_MOUNT_POINT) + 1);
    else snprintf(msg, sizeof(msg), "Couldn't save the screenshot.");
    printf("screenshot: %s\n", msg);
    ss_state = 0;
    sudoku_ui_notice(msg, 4000);
}

void screenshot_cb(lv_event_t *e)
{
    if (ss_state) return;                           /* (one at a time) */
    if (!stats_db_ready()) {
        sudoku_ui_notice("Screenshots go on the microSD card: there's none to write to.", 3000);
        return;
    }
    if (!ss_frame) ss_frame = heap_caps_aligned_alloc(64, (size_t)SCR_W * SCR_W * 2, MALLOC_CAP_SPIRAM);
    lv_refr_now(NULL);
    const uint16_t *fb = bsp_display_frame();
    if (!ss_frame || !fb) return;
    memcpy(ss_frame, fb, (size_t)SCR_W * SCR_W * 2);
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    mkdir(BSP_SD_MOUNT_POINT "/screenshots", 0775);
    if (now > 1700000000) strftime(ss_path, sizeof(ss_path), BSP_SD_MOUNT_POINT "/screenshots/%Y%m%d-%H%M%S.jpg", &tm);
    else snprintf(ss_path, sizeof(ss_path), BSP_SD_MOUNT_POINT "/screenshots/shot-%lu.jpg",
                  (unsigned long)(esp_timer_get_time() / 1000000));
    ss_state = 1;
    if (xTaskCreatePinnedToCore(ss_task, "screenshot", 4096, NULL, 2, NULL, WORKER_CORE) != pdPASS) {
        ss_state = 0;
        return;
    }
    ss_timer = lv_timer_create(ss_poll_cb, 200, NULL);
}

/* ------------------------------------------------------------- mistake insights */
static lv_obj_t *ins_box, *ins_body;
static lv_timer_t *ins_timer;

static void ins_close_cb(lv_event_t *e)
{
    if (ins_timer) lv_timer_delete(ins_timer);
    ins_timer = NULL;
    if (ins_box) lv_msgbox_close_async(ins_box);
    ins_box = ins_body = NULL;
}

static lv_obj_t *ins_heading(const char *text)
{
    lv_obj_t *l = make_label(ins_body, &lv_font_montserrat_18, C_TEXT);
    lv_label_set_text(l, text);
    lv_obj_set_style_margin_top(l, 14, 0);
    return l;
}

static void ins_show(const stats_insights_t *in)
{
    lv_obj_clean(ins_body);
    char buf[160];
    if (in->solves == 0 || in->placed == 0) {
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED,
                       "Nothing to look at yet: insights come from the move logs of solved 9x9 puzzles.");
        return;
    }
    snprintf(buf, sizeof(buf), "From your last %d solve%s: %lu of the %lu digits you placed by hand were wrong "
             "(%lu.%lu %%).", in->solves, in->solves == 1 ? "" : "s", (unsigned long)in->wrong,
             (unsigned long)in->placed, (unsigned long)(in->wrong * 100 / in->placed),
             (unsigned long)(in->wrong * 1000 / in->placed % 10));
    make_wrap_text(ins_body, &lv_font_montserrat_18, C_TEXT, buf);
    if (!in->wrong) {
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_GREEN, "Not one wrong digit. Nothing to work on here!");
        return;
    }

    /* which digits: a bar each */
    ins_heading("Digits you put in the wrong place");
    int maxd = 1;
    for (int d = 1; d <= 9; d++) if (in->by_digit[d] > maxd) maxd = in->by_digit[d];
    for (int d = 1; d <= 9; d++) {
        lv_obj_t *row = lv_obj_create(ins_body);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 26);
        lv_obj_t *n = make_label(row, &lv_font_montserrat_18, C_TEXT);
        lv_label_set_text_fmt(n, "%d", d);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_t *bar = lv_obj_create(row);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, in->by_digit[d] ? 4 + 420 * in->by_digit[d] / maxd : 2, 16);
        lv_obj_set_style_bg_color(bar, in->by_digit[d] == maxd ? C_RED : C_ORANGE, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_align(bar, LV_ALIGN_LEFT_MID, 30, 0);
        lv_obj_t *c = make_label(row, &lv_font_montserrat_16, C_MUTED);
        lv_label_set_text_fmt(c, "%u", (unsigned)in->by_digit[d]);
        lv_obj_align(c, LV_ALIGN_RIGHT_MID, -4, 0);
    }

    /* where: the nine boxes, the busier the redder */
    ins_heading("Where on the board");
    int maxb = 1;
    for (int b = 0; b < 9; b++) if (in->by_box[b] > maxb) maxb = in->by_box[b];
    lv_obj_t *g = lv_obj_create(ins_body);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, 3 * 76 + 2 * 6, 3 * 56 + 2 * 6);
    for (int b = 0; b < 9; b++) {
        lv_obj_t *c = lv_obj_create(g);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 76, 56);
        lv_obj_set_pos(c, (b % 3) * 82, (b / 3) * 62);
        lv_obj_set_style_radius(c, 6, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        int pct = in->by_box[b] * 100 / maxb;
        lv_obj_set_style_bg_color(c, lv_color_hex(mix_hex(ui_pal->red, ui_pal->card2, pct * 85 / 100)), 0);
        lv_obj_t *l = make_label(c, &lv_font_montserrat_18, pct > 55 ? lv_color_white() : C_TEXT);
        lv_label_set_text_fmt(l, "%u", (unsigned)in->by_box[b]);
        lv_obj_center(l);
    }

    /* mix-ups: the most frequent digit-for-digit swaps */
    ins_heading("Mix-ups");
    uint16_t done[10][10] = {{0}};
    for (int k = 0; k < 3; k++) {
        int bp = 0, bb = 0, bn = 0;
        for (int p2 = 1; p2 <= 9; p2++) {
            for (int q = 1; q <= 9; q++) {
                if (!done[p2][q] && in->pair[p2][q] > bn) { bn = in->pair[p2][q]; bp = p2; bb = q; }
            }
        }
        if (!bn) break;
        done[bp][bb] = 1;
        snprintf(buf, sizeof(buf), "A %d where a %d belongs: %d time%s", bp, bb, bn, bn == 1 ? "" : "s");
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED, buf);
    }
}

static void ins_poll_cb(lv_timer_t *t)
{
    static stats_insights_t in;                     /* (~250 bytes: off the LVGL stack) */
    if (!stats_db_take_insights(&in)) return;
    lv_timer_delete(ins_timer);
    ins_timer = NULL;
    if (ins_body) ins_show(&in);
}

void insights_cb(lv_event_t *e)
{
    if (ins_box) return;
    lv_obj_t *m = make_msgbox(SYMBOL_BULB "  Mistake insights", NULL);
    ins_box = m;
    lv_obj_set_width(m, 640);
    lv_obj_set_style_text_font(lv_msgbox_get_header(m), &font_icons_20, 0);
    ins_body = lv_msgbox_get_content(m);
    lv_obj_set_height(ins_body, 520);
    lv_obj_set_flex_flow(ins_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ins_body, 4, 0);
    lv_obj_set_scroll_dir(ins_body, LV_DIR_VER);
    make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED, "Reading your move logs...");
    msgbox_button(m, "Close", ins_close_cb, false);
    if (!stats_db_ready()) {
        lv_obj_clean(ins_body);
        make_wrap_text(ins_body, &lv_font_montserrat_18, C_MUTED, "This needs sudoku.db on a microSD card.");
        return;
    }
    stats_db_query_insights();
    ins_timer = lv_timer_create(ins_poll_cb, 100, NULL);
}

static lv_obj_t *rp_shot;                           /* the Replay screen's camera (hidden in exported videos) */

static lv_obj_t *rp_build(uint16_t id, const char *sub, const char *player)
{
    lv_obj_t *scr = make_screen();
    make_btn(scr, 8, 6, 104, 48, LV_SYMBOL_LEFT "  Back", &lv_font_montserrat_18, rp_back_cb, NULL);
    char name[40];
    puzzle_name(id, name, sizeof(name));
    rp_title = make_label(scr, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text_fmt(rp_title, "Replay  -  %s", name);
    lv_obj_set_pos(rp_title, 126, 6);
    rp_sub = make_label(scr, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(rp_sub, sub);
    const stats_player_t *who = player_find(0, player);
    if (who && who->avatar && who->avatar <= AVATAR_COUNT) {   /* whose game: their avatar first */
        lv_obj_set_pos(player_badge(scr, who, -1, 22, 8), 126, 32);
        lv_obj_set_pos(rp_sub, 126 + 22 + 6, 36);
    } else {
        lv_obj_set_pos(rp_sub, 126, 36);
    }

    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_add_event_cb(grid, board_regions_draw_cb, LV_EVENT_DRAW_POST_END, NULL);   /* Jigsaw borders */
    lv_obj_set_style_bg_color(grid, C_GRID_THICK, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(grid, false);
    lv_obj_t *boxes[9];
    for (int b = 0; b < 9; b++) {
        lv_obj_t *box = lv_obj_create(grid);
        lv_obj_remove_style_all(box);
        lv_obj_set_style_bg_color(box, C_GRID_THIN, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(box, false);
        boxes[b] = box;
    }
    for (int i = 0; i < 81; i++) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(cell, false);
        rp_cells[i] = cell;
        rp_lbl[i] = make_label(cell, DIGIT_FONT_N(rp_geo->n), C_TEXT);
        lv_obj_center(rp_lbl[i]);
    }
    lv_obj_t *clues[18];
    for (int i = 0; i < 18; i++) {
        clues[i] = make_label(scr, &lv_font_montserrat_20, C_ACCENT);
        lv_obj_set_style_text_align(clues[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    board_layout(rp_geo, grid, boxes, rp_cells, clues, GRID_X);

    /* second row (as the game's): where the replay is on the left, the speed on the right */
    rp_pos = make_label(scr, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(rp_pos, SCR_W - 8 - 200 - 8 - 16 - 64);
    rp_shot = make_btn(scr, SCR_W - 8 - 200 - 8 - 56, TOOL_Y, 56, 48, SYMBOL_CAMERA, &font_icons_20,
                       screenshot_cb, NULL);       /* a screenshot of this moment */
    lv_label_set_long_mode(rp_pos, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(rp_pos, 16, TOOL_Y + (48 - 24) / 2);
    lv_obj_t *sp = make_btn(scr, SCR_W - 8 - 200, TOOL_Y, 200, 48, NULL, &lv_font_montserrat_20, rp_btn_cb,
                            (void *)(intptr_t)5);
    rp_speed_lbl = make_label(sp, &lv_font_montserrat_20, C_ACCENT);
    lv_obj_center(rp_speed_lbl);
    lv_label_set_text_fmt(rp_speed_lbl, LV_SYMBOL_PLAY "  Speed %ux", RP_SPEEDS[rp_speed]);

    /* controls down the right, as tall as the board: start, back, play, forward, end */
    static const char *const SYM[] = {LV_SYMBOL_PREV, LV_SYMBOL_LEFT, LV_SYMBOL_PLAY, LV_SYMBOL_RIGHT,
                                      LV_SYMBOL_NEXT};
    const int gap = 8, bh = (GRID_SIZE - 4 * gap) / 5;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = make_btn(scr, PAD_X, GRID_Y + i * (bh + gap), PAD_W, bh, SYM[i],
                               &lv_font_montserrat_28, rp_btn_cb, (void *)(intptr_t)i);
        if (i == 2) {
            lv_obj_set_style_bg_color(b, C_ACCENT, 0);
            rp_play_lbl = lv_obj_get_child(b, 0);
            lv_obj_set_style_text_color(rp_play_lbl, lv_color_white(), 0);
        }
    }

    /* bottom: the slider, the full width */
    rp_slider = lv_slider_create(scr);
    lv_obj_set_size(rp_slider, SCR_W - 64, 14);
    lv_obj_set_pos(rp_slider, 32, BOT_Y + (BOT_H - 14) / 2);
    lv_slider_set_range(rp_slider, 0, rp_n > 0 ? rp_n : 1);
    lv_obj_set_style_bg_color(rp_slider, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rp_slider, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(rp_slider, C_ACCENT, LV_PART_KNOB);
    lv_obj_set_ext_click_area(rp_slider, 16);
    lv_obj_add_event_cb(rp_slider, rp_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return scr;
}

/* Opens the Replay screen for a log of puzzle `id`; sub = the line under the title. */
void open_replay(uint16_t id, const game_move_t *moves, int n, const char *sub, const char *player)
{
    if (!rp_prepare(id, moves, n)) return;
    if (scr_replay) rp_close();
    rp_return = lv_screen_active();
    scr_replay = rp_build(id, sub, player);
    rp_playing = false;
    rp_seek(0, false);
    rp_last_tick = lv_tick_get();
    rp_timer = lv_timer_create(rp_timer_cb, 40, NULL);
    lv_screen_load(scr_replay);
    if (n == 0) sudoku_ui_notice("No moves were recorded for this game.", 3000);
}

bool rp_want_export;                 /* the replay asked for is to be exported, not shown */
bool rx_busy;                        /* an export is running */

void history_replay_poll(void)
{
    if (!rp_loaded) rp_loaded = heap_caps_malloc(sizeof(*rp_loaded), MALLOC_CAP_SPIRAM);
    if (!rp_loaded || !stats_db_take_replay(rp_loaded)) return;
    lv_obj_t *act = lv_screen_active();
    if (act != scr_history && act != scr_plays) return;      /* left meanwhile */
    if (rx_busy) return;
    if (!rp_loaded->found) {
        sudoku_ui_notice("This solve has no recorded moves.", 3000);
        return;
    }
    if (rp_want_export) {
        rp_want_export = false;
        export_start(rp_loaded);
        return;
    }
    char t[16], sub[96];
    fmt_time(t, sizeof(t), rp_loaded->seconds);
    snprintf(sub, sizeof(sub), "%s   -   solved in %s   -   %u hint%s   -   orange = Fill",
             rp_loaded->player, t, (unsigned)rp_loaded->hints, rp_loaded->hints == 1 ? "" : "s");
    open_replay(rp_loaded->puzzle, rp_loaded->moves, rp_loaded->n, sub, rp_loaded->player);
}

static lv_display_t *rx_disp;
static uint8_t *rx_fb;              /* RGB888 (see video_enc.c: RGB565 gave the video a green cast) */
static video_enc_t *rx_enc;
static lv_timer_t *rx_timer;
static int rx_frame, rx_frames, rx_last_k;
static bool rx_last_playing, rx_cancel;
static lv_obj_t *rx_ovl, *rx_bar, *rx_lbl;
static char rx_path[128];

static void rx_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    lv_display_flush_ready(d);                      /* the picture stays in rx_fb */
}

static void rx_cancel_cb(lv_event_t *e)
{
    rx_cancel = true;
}

static void rx_finish(bool ok)
{
    if (rx_timer) lv_timer_delete(rx_timer);
    rx_timer = NULL;
    uint32_t bytes = video_enc_bytes(rx_enc);
    if (ok) ok = video_enc_close(rx_enc);
    else video_enc_abort(rx_enc);
    rx_enc = NULL;
    if (rx_disp) lv_display_delete(rx_disp);       /* and the replay screen built on it */
    rx_disp = NULL;
    heap_caps_free(rx_fb);
    rx_fb = NULL;
    if (rx_ovl) lv_obj_delete(rx_ovl);
    rx_ovl = NULL;
    rx_busy = false;
    char msg[200];
    if (ok) {
        const char *name = strrchr(rx_path, '/');
        snprintf(msg, sizeof(msg), "Saved replays%s (%.1f MB). Copy it to a computer with Settings > USB drive.",
                 name ? name : "", bytes / 1048576.0);
        printf("export: %s, %u bytes, %d frames\n", rx_path, (unsigned)bytes, rx_frames);
    } else {
        snprintf(msg, sizeof(msg), rx_cancel ? "Export cancelled." : "Export failed: couldn't write the video.");
    }
    sudoku_ui_notice(msg, ok ? 6000 : 4000);
}

/* replay position at video frame f: moves shown, and whether it "plays" */
static int rx_k_at(int f, bool *playing)
{
    int64_t t = (int64_t)f * 1000 / RX_FPS - RX_INTRO_MS;
    *playing = false;
    if (t < 0 || rp_n == 0) return 0;
    uint32_t clock = (uint32_t)(t * RX_SPEED);
    int k = 0;
    while (k < rp_n && rp_vt[k] <= clock) k++;
    *playing = k < rp_n;
    return k;
}

static void rx_timer_cb(lv_timer_t *t)
{
    if (rx_cancel || !stats_db_ready()) {          /* cancelled, or the card went away */
        rx_finish(false);
        return;
    }
    uint32_t t0 = lv_tick_get();
    do {
        bool playing;
        int k = rx_k_at(rx_frame, &playing);
        bool changed = rx_frame == 0 || k != rx_last_k || playing != rx_last_playing;
        if (changed) {
            rp_k = k;
            rp_playing = playing;
            rp_render();
            lv_refr_now(rx_disp);                  /* into rx_fb */
            rx_last_k = k;
            rx_last_playing = playing;
        }
        if (!video_enc_frame(rx_enc, rx_fb, changed)) {
            rx_finish(false);
            return;
        }
        rx_frame++;
    } while (rx_frame < rx_frames && lv_tick_elaps(t0) < 30);   /* keep the progress dialog alive */

    if (rx_frame >= rx_frames) {
        rx_finish(true);
        return;
    }
    int pct = rx_frame * 100 / rx_frames;
    lv_bar_set_value(rx_bar, pct, LV_ANIM_OFF);
    /* LVGL's printf has no %f (LV_SPRINTF_USE_FLOAT off): tenths of a MB by hand */
    uint32_t tenths = (uint32_t)(((uint64_t)video_enc_bytes(rx_enc) * 10 + 524288) / 1048576);
    lv_label_set_text_fmt(rx_lbl, "Frame %d of %d   -   %u.%u MB", rx_frame, rx_frames,
                          (unsigned)(tenths / 10), (unsigned)(tenths % 10));
}

/* file name from the player, puzzle and when it was solved */
static void rx_name(const stats_replay_t *r, char *out, size_t n)
{
    char raw[96], name[40], when[20] = "";
    puzzle_name(r->puzzle, name, sizeof(name));
    if (r->finished_at) {
        time_t tt = (time_t)r->finished_at;
        struct tm tm;
        localtime_r(&tt, &tm);
        strftime(when, sizeof(when), "%Y%m%d-%H%M", &tm);
    } else {
        snprintf(when, sizeof(when), "solve%lld", (long long)r->solve_id);
    }
    snprintf(raw, sizeof(raw), "%s_%s_%s", r->player, name, when);
    size_t j = 0;
    for (size_t i = 0; raw[i] && j + 1 < n; i++) {   /* letters, digits, - and _ only */
        char c = raw[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) c = (c == ' ' || c == '#') ? '-' : 0;
        if (!c || (c == '-' && j && out[j - 1] == '-')) continue;
        out[j++] = c;
    }
    out[j] = 0;
}

static void export_start(const stats_replay_t *r)
{
    if (rx_busy) return;
    if (!stats_db_ready()) {
        sudoku_ui_notice("Can't export: no sudoku.db on a microSD card.", 3000);
        return;
    }
    if (!rp_prepare(r->puzzle, r->moves, r->n)) {
        printf("export: puzzle %u not available (%d moves)\n", (unsigned)r->puzzle, r->n);
        sudoku_ui_notice("Can't export: this puzzle couldn't be loaded.", 3000);
        return;
    }
    uint32_t play_ms = rp_n ? rp_vt[rp_n - 1] / RX_SPEED : 0;
    rx_frames = (int)((RX_INTRO_MS + play_ms + RX_OUTRO_MS) * RX_FPS / 1000) + 1;

    char file[80];
    rx_name(r, file, sizeof(file));
    mkdir(RX_DIR, 0775);                            /* fine if it exists */
    snprintf(rx_path, sizeof(rx_path), RX_DIR "/%s.mp4", file);

    rx_fb = heap_caps_aligned_alloc(64, SCR_W * SCR_W * 3, MALLOC_CAP_SPIRAM);
    rx_enc = rx_fb ? video_enc_open(rx_path, SCR_W, SCR_W, RX_FPS, rx_frames + 8) : NULL;
    if (!rx_enc) {
        printf("export: %s didn't open (frame buffer %s)\n", rx_path, rx_fb ? "ok" : "no memory");
        heap_caps_free(rx_fb);
        rx_fb = NULL;
        sudoku_ui_notice("Can't export: the video encoder or the file on the card couldn't be opened.", 4000);
        return;
    }

    /* the Replay screen on an invisible display that renders into rx_fb */
    lv_display_t *main_disp = lv_display_get_default();
    rx_disp = lv_display_create(SCR_W, SCR_W);
    lv_display_set_color_format(rx_disp, LV_COLOR_FORMAT_RGB888);   /* (the encoder input: video_enc.c) */
    lv_display_set_buffers(rx_disp, rx_fb, NULL, SCR_W * SCR_W * 3, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(rx_disp, rx_flush_cb);
    lv_display_set_theme(rx_disp, lv_display_get_theme(main_disp));
#if LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(rx_disp);           /* no FPS/CPU box in the video */
#endif
    lv_display_set_default(rx_disp);
    char t[16], sub[96];
    fmt_time(t, sizeof(t), r->seconds);
    snprintf(sub, sizeof(sub), "%s   -   solved in %s   -   %u hint%s   -   orange = Fill",
             r->player, t, (unsigned)r->hints, r->hints == 1 ? "" : "s");
    lv_obj_t *scr = rp_build(r->puzzle, sub, r->player);
    lv_obj_set_hidden(rp_shot, true);               /* (not in the video) */
    lv_screen_load(scr);
    lv_display_set_default(main_disp);

    /* progress dialog on the real screen */
    rx_ovl = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(rx_ovl);
    lv_obj_set_size(rx_ovl, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(rx_ovl, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(rx_ovl, (lv_opa_t)P->scrim_opa, 0);
    lv_obj_set_clickable(rx_ovl, true);                   /* nothing else while it runs */
    lv_obj_t *card = lv_obj_create(rx_ovl);
    lv_obj_set_size(card, 520, 250);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_scrollable(card, false);
    lv_obj_t *tl = make_label(card, &lv_font_montserrat_22, C_TEXT);
    lv_label_set_text(tl, LV_SYMBOL_VIDEO "  Exporting video");
    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t *fl = make_label(card, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(fl, 470);
    lv_label_set_long_mode(fl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(fl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(fl, "replays/%s.mp4", file);
    lv_obj_align(fl, LV_ALIGN_TOP_MID, 0, 42);
    rx_bar = lv_bar_create(card);
    lv_obj_set_size(rx_bar, 460, 16);
    lv_bar_set_range(rx_bar, 0, 100);
    lv_obj_set_style_bg_color(rx_bar, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rx_bar, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_align(rx_bar, LV_ALIGN_TOP_MID, 0, 80);
    rx_lbl = make_label(card, &lv_font_montserrat_16, C_MUTED);
    lv_label_set_text(rx_lbl, "Starting...");
    lv_obj_align(rx_lbl, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_t *cb = make_btn(card, 0, 0, 200, 52, "Cancel", &lv_font_montserrat_18, rx_cancel_cb, NULL);
    lv_obj_set_style_bg_color(cb, C_CARD2, 0);
    lv_obj_align(cb, LV_ALIGN_BOTTOM_MID, 0, 0);

    rx_busy = true;
    rx_cancel = false;
    rx_frame = 0;
    rx_last_k = -1;
    rx_last_playing = false;
    rx_timer = lv_timer_create(rx_timer_cb, 1, NULL);
    printf("export: %s, %d frames\n", rx_path, rx_frames);
}
