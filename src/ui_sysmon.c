/*
 * System monitor page (see ui_sysmon.h): the screen. The numbers it shows come from
 * sysmon_core.c (sysmon_core.h), which reads them without LVGL; LVGL's own statistics (frame
 * rate, draw times, objects, timers) are gathered here, as they need LVGL.
 */
#include "ui_sysmon.h"
#include "ui_palette.h"
#include "sysmon_core.h"
#include "bsp/bsp_board.h"
#include "lvgl_private.h"            /* the display's perf subject, its screens */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "wifi_mgr.h"

#define REFRESH_MS   1000
/* (Nothing may switch interrupts off on the LVGL core for long: see sysmon_core.h. So the task
 * list is asked for only while the Tasks tab is open.) */
#define SD_EVERY     10               /* ticks between microSD free-space counts */

/* table columns: name, core, priority, state, CPU, free stack */
static const int16_t COL_W[] = {196, 60, 60, 84, 96, 108};   /* = the card's 604 px */
#define COLS ((int)(sizeof(COL_W) / sizeof(COL_W[0])))

static lv_obj_t *s_scr;

enum { SM_SYSTEM = 0, SM_MEMORY, SM_STORAGE, SM_GRAPHICS, SM_TASKS, SM_TABS };
#define TAB_Y   72
#define TAB_H   52
#define PANE_Y  (TAB_Y + TAB_H + 12)
static lv_obj_t *s_tab_btn[SM_TABS], *s_pane[SM_TABS];
static int s_tab;                    /* the open tab; kept between visits */
static lv_timer_t *s_timer;
static bool s_shown;
static int s_ticks;

/* overview */
static lv_obj_t *s_uptime, *s_temp, *s_load0, *s_load1, *s_ntasks, *s_reset;
static lv_obj_t *s_ip, *s_wifi_mac, *s_eth_mac;
/* memory */
static lv_obj_t *s_int_bar, *s_int_val, *s_int_sub;
static lv_obj_t *s_ps_bar, *s_ps_val, *s_ps_sub;
static lv_obj_t *s_dma_val;
/* storage */
static lv_obj_t *s_app_bar, *s_app_val, *s_app_sub;
static lv_obj_t *s_part_val;
static lv_obj_t *s_nvs_bar, *s_nvs_val;
static lv_obj_t *s_sd_bar, *s_sd_val, *s_sd_sub;
/* lvgl */
static lv_obj_t *s_fps, *s_lv_cpu, *s_lv_times, *s_lv_objs, *s_lv_timers;
/* tasks */
static lv_obj_t *s_table;

/* LVGL: its sysmon's busy % (perf_cb, every 300 ms), averaged per tick; frames
 * and draw times counted here (disp_ev): sysmon's FPS counts refresh-timer
 * runs, drawn or not, so an idle screen shows "50". */
static uint32_t s_cpu_sum, s_cpu_n;
static int64_t s_render_t0, s_window_t0;
static uint32_t s_frames, s_draw_us_sum, s_draw_us_max;

static sysmon_load_t s_load;           /* the System tab's CPU load (from the previous tick) */
static void show_task_table(void);

/* ------------------------------------------------------------- helpers */
static void fmt_bytes(char *buf, size_t n, uint64_t b)
{
    if (b < 1024) {
        snprintf(buf, n, "%u B", (unsigned)b);
    } else if (b < 1024 * 1024) {
        snprintf(buf, n, "%u.%u KB", (unsigned)(b / 1024), (unsigned)(b % 1024 * 10 / 1024));
    } else if (b < 1024ULL * 1024 * 1024) {
        uint64_t k = b / 1024;
        snprintf(buf, n, "%u.%u MB", (unsigned)(k / 1024), (unsigned)(k % 1024 * 10 / 1024));
    } else {
        uint64_t m = b / (1024 * 1024);
        snprintf(buf, n, "%u.%02u GB", (unsigned)(m / 1024), (unsigned)(m % 1024 * 100 / 1024));
    }
}

static lv_obj_t *plain_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt);
    return l;
}

/* a muted note under a card */
static void section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *l = label(parent, &lv_font_montserrat_16, C_MUTED, title);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_pad_hor(l, 8, 0);
}

static lv_obj_t *card(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_pad_all(c, 18, 0);
    lv_obj_set_style_pad_row(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(c, false);
    return c;
}

/* "Title ........ value" */
static lv_obj_t *kv(lv_obj_t *c, const char *title)
{
    lv_obj_t *row = plain_row(c);
    label(row, &lv_font_montserrat_18, C_MUTED, title);
    lv_obj_t *v = label(row, &lv_font_montserrat_20, C_TEXT, "-");
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    return v;
}

/* "Title ........ value", a bar, a muted line under it */
static void bar_row(lv_obj_t *c, const char *title, lv_obj_t **bar, lv_obj_t **val, lv_obj_t **sub)
{
    lv_obj_t *col = lv_obj_create(c);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 8, 0);
    *val = kv(col, title);
    *bar = lv_bar_create(col);
    lv_obj_set_size(*bar, LV_PCT(100), 14);
    lv_bar_set_range(*bar, 0, 1000);
    lv_obj_set_style_radius(*bar, 7, 0);
    lv_obj_set_style_radius(*bar, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(*bar, C_CARD2, 0);
    lv_obj_set_style_bg_opa(*bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(*bar, C_ACCENT, LV_PART_INDICATOR);
    if (sub) {
        *sub = label(col, &lv_font_montserrat_16, C_MUTED, "");
        lv_obj_set_width(*sub, LV_PCT(100));
        lv_label_set_long_mode(*sub, LV_LABEL_LONG_MODE_DOTS);
    }
}

static void set_bar(lv_obj_t *bar, uint64_t used, uint64_t total)
{
    int32_t v = total ? (int32_t)(used * 1000 / total) : 0;
    lv_bar_set_value(bar, v, LV_ANIM_OFF);
    lv_color_t c = v >= 900 ? C_RED : (v >= 750 ? C_ORANGE : C_ACCENT);
    lv_obj_set_style_bg_color(bar, c, LV_PART_INDICATOR);
}

static void set_text(lv_obj_t *l, const char *txt)
{
    if (strcmp(lv_label_get_text(l), txt) != 0) {
        lv_label_set_text(l, txt);
    }
}

/* ------------------------------------------------------------- sources */
static void perf_cb(lv_observer_t *o, lv_subject_t *subject)
{
    const lv_sysmon_perf_info_t *p = lv_subject_get_pointer(subject);
    if (!p) {
        return;
    }
    s_cpu_sum += p->calculated.cpu;
    s_cpu_n++;
}

/* RENDER_START/READY: only sent when something was redrawn (incl. the flush) */
static void disp_ev(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();
    if (lv_event_get_code(e) == LV_EVENT_RENDER_START) {
        s_render_t0 = now;
    } else if (s_render_t0) {
        uint32_t us = (uint32_t)(now - s_render_t0);
        s_render_t0 = 0;
        s_frames++;
        s_draw_us_sum += us;
        if (us > s_draw_us_max) {
            s_draw_us_max = us;
        }
    }
}

static uint32_t count_tree(lv_obj_t *o)
{
    if (!o) {
        return 0;
    }
    uint32_t n = 1, c = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < c; i++) {
        n += count_tree(lv_obj_get_child(o, (int32_t)i));
    }
    return n;
}

/* ------------------------------------------------------------- views */
/* System tab: CPU load per core from the idle tasks' run time (no locks), the number of tasks */
static void show_cpu(void)
{
    char buf[24];
    int pm[2];
    if (sysmon_load_permille(&s_load, pm)) {
        for (int c = 0; c < 2; c++) {
            snprintf(buf, sizeof(buf), "%d.%d %%", pm[c] / 10, pm[c] % 10);
            set_text(c ? s_load1 : s_load0, buf);
        }
    }
    snprintf(buf, sizeof(buf), "%u", sysmon_task_count());
    set_text(s_ntasks, buf);
}

/* Tasks tab only: the list the core-0 reader made last (a second old), and a fresh one asked for */
static void show_tasks(void)
{
    if (sysmon_tasks_poll()) {
        show_task_table();
    }
}

/* the table from the latest list (show_tasks) */
static void show_task_table(void)
{
    char buf[16];
    uint32_t n = (uint32_t)sysmon_tasks_count();
    lv_table_set_row_count(s_table, n);
    for (uint32_t r = 0; r < n; r++) {
        uint32_t permille;
        const TaskStatus_t *t = sysmon_task((int)r, &permille);
        lv_table_set_cell_value(s_table, r, 0, t->pcTaskName);
        if (t->xCoreID == tskNO_AFFINITY) {
            lv_table_set_cell_value(s_table, r, 1, "-");
        } else {
            lv_table_set_cell_value_fmt(s_table, r, 1, "%d", (int)t->xCoreID);
        }
        lv_table_set_cell_value_fmt(s_table, r, 2, "%u", (unsigned)t->uxCurrentPriority);
        const char *st = "?";
        switch (t->eCurrentState) {
        case eRunning:
            st = "Run";
            break;
        case eReady:
            st = "Ready";
            break;
        case eBlocked:
            st = "Wait";
            break;
        case eSuspended:
            st = "Susp";
            break;
        case eDeleted:
            st = "Del";
            break;
        default:
            break;
        }
        lv_table_set_cell_value(s_table, r, 3, st);
        if (is_sysmon_tasks_cpu_valid()) {
            lv_table_set_cell_value_fmt(s_table, r, 4, "%u.%u%%", (unsigned)(permille / 10), (unsigned)(permille % 10));
        } else {
            lv_table_set_cell_value(s_table, r, 4, "...");
        }
        fmt_bytes(buf, sizeof(buf), t->usStackHighWaterMark);   /* bytes on ESP-IDF */
        lv_table_set_cell_value(s_table, r, 5, buf);
    }
}

/* with_min: the low-water mark and the largest free block (internal RAM only: see sysmon_heap()) */
static void show_mem_one(uint32_t caps, bool with_min, lv_obj_t *bar, lv_obj_t *val, lv_obj_t *sub)
{
    sysmon_heap_t hp;
    sysmon_heap(caps, with_min, &hp);
    size_t total = hp.total, free_b = hp.free, min_b = hp.min_free, big = hp.largest;
    char a[16], b[16], c[16], d[16], buf[96];
    if (total == 0) {
        set_text(val, "None");
        set_bar(bar, 0, 1);
        set_text(sub, "");
        return;
    }
    fmt_bytes(a, sizeof(a), free_b);
    fmt_bytes(b, sizeof(b), total);
    snprintf(buf, sizeof(buf), "%s free of %s", a, b);
    set_text(val, buf);
    set_bar(bar, total - free_b, total);
    fmt_bytes(d, sizeof(d), big);
    if (with_min) {
        fmt_bytes(c, sizeof(c), min_b);
        snprintf(buf, sizeof(buf), "Lowest free %s  |  largest block %s", c, d);
    } else {
        fmt_bytes(c, sizeof(c), total - free_b);
        snprintf(buf, sizeof(buf), "Used %s", c);
    }
    set_text(sub, buf);
}

static void show_storage(void)
{
    char a[16], b[16], buf[96];
    const sysmon_flash_t *fl = sysmon_flash();
    if (fl->app_part) {
        fmt_bytes(a, sizeof(a), fl->app_len);
        fmt_bytes(b, sizeof(b), fl->app_part);
        snprintf(buf, sizeof(buf), "%s of %s", a, b);
        set_text(s_app_val, buf);
        set_bar(s_app_bar, fl->app_len, fl->app_part);
        snprintf(buf, sizeof(buf), "Running from %s at 0x%06x", fl->run_label ? fl->run_label : "?",
                 (unsigned)fl->run_addr);
        set_text(s_app_sub, buf);
    }
    fmt_bytes(a, sizeof(a), fl->part_end);
    fmt_bytes(b, sizeof(b), fl->flash_size);
    snprintf(buf, sizeof(buf), "%s of %s", a, b);
    set_text(s_part_val, buf);

    uint32_t used, total;
    if (sysmon_nvs(&used, &total)) {
        snprintf(buf, sizeof(buf), "%u of %u entries", (unsigned)used, (unsigned)total);
        set_text(s_nvs_val, buf);
        set_bar(s_nvs_bar, used, total);
    }

    uint64_t sd_total, sd_free;
    esp_err_t sd_err;
    if (sysmon_sd(&sd_total, &sd_free, &sd_err)) {
        if (sd_err == ESP_OK && sd_total) {
            fmt_bytes(a, sizeof(a), sd_free);
            fmt_bytes(b, sizeof(b), sd_total);
            snprintf(buf, sizeof(buf), "%s free of %s", a, b);
            set_text(s_sd_val, buf);
            set_bar(s_sd_bar, sd_total - sd_free, sd_total);
            fmt_bytes(a, sizeof(a), sysmon_sd_card_bytes());
            snprintf(buf, sizeof(buf), "FAT volume at " BSP_SD_MOUNT_POINT "  |  card %s", a);
            set_text(s_sd_sub, buf);
        } else {
            set_text(s_sd_val, sd_err == ESP_ERR_INVALID_STATE ? "Not mounted" : "No card");
            set_bar(s_sd_bar, 0, 1);
            set_text(s_sd_sub, sd_err == ESP_ERR_INVALID_STATE ? "No card, not FAT, or lent over USB" : "");
        }
    } else {
        set_text(s_sd_val, "Counting...");
    }
}

static void show_lvgl(void)
{
    char buf[64];
    int64_t now = esp_timer_get_time();
    uint32_t ms = (uint32_t)((now - s_window_t0) / 1000);
    if (s_window_t0 && ms) {
        uint32_t f10 = (uint32_t)((uint64_t)s_frames * 10000 / ms);          /* frames/s x10 */
        snprintf(buf, sizeof(buf), "%u.%u", (unsigned)(f10 / 10), (unsigned)(f10 % 10));
        set_text(s_fps, buf);
        if (s_frames) {
            uint32_t avg = s_draw_us_sum / s_frames;
            snprintf(buf, sizeof(buf), "%u.%u / %u.%u ms", (unsigned)(avg / 1000), (unsigned)(avg % 1000 / 100),
                     (unsigned)(s_draw_us_max / 1000), (unsigned)(s_draw_us_max % 1000 / 100));
            set_text(s_lv_times, buf);
        }
    }
    if (s_cpu_n) {
        snprintf(buf, sizeof(buf), "%u %%", (unsigned)(s_cpu_sum / s_cpu_n));
        set_text(s_lv_cpu, buf);
    }
    s_window_t0 = now;
    s_frames = s_draw_us_sum = s_draw_us_max = 0;
    s_cpu_sum = s_cpu_n = 0;
    lv_display_t *d = lv_obj_get_display(s_scr);
    uint32_t objs = 0;
    for (uint32_t i = 0; i < d->screen_cnt; i++) {
        objs += count_tree(d->screens[i]);
    }
    objs += count_tree(d->top_layer) + count_tree(d->sys_layer) + count_tree(d->bottom_layer);
    snprintf(buf, sizeof(buf), "%u on %u screens", (unsigned)objs, (unsigned)d->screen_cnt);
    set_text(s_lv_objs, buf);
    uint32_t timers = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) {
        timers++;
    }
    snprintf(buf, sizeof(buf), "%u", (unsigned)timers);
    set_text(s_lv_timers, buf);
}

static void show_overview(void)
{
    char buf[48];
    uint64_t s = sysmon_uptime_s();
    unsigned d = (unsigned)(s / 86400), h = (unsigned)(s / 3600 % 24), m = (unsigned)(s / 60 % 60), sec = (unsigned)(s % 60);
    if (d) {
        snprintf(buf, sizeof(buf), "%ud %02u:%02u:%02u", d, h, m, sec);
    } else {
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, sec);
    }
    set_text(s_uptime, buf);

    wifi_status_t ws;                               /* (a snapshot: wifi_mgr never blocks here) */
    wifi_mgr_get_status(&ws);
    set_text(s_ip, ws.state == WIFI_ST_CONNECTED ? ws.ip
             : ws.state == WIFI_ST_OFF ? "Wi-Fi off"
             : ws.state == WIFI_ST_CONNECTING ? "Connecting..." : "Not connected");
    set_text(s_wifi_mac, ws.mac[0] ? ws.mac : (ws.state == WIFI_ST_OFF ? "Wi-Fi off" : "-"));

    float c;
    if (sysmon_temp_read(&c)) {
        int t10 = (int)(c * 10 + (c >= 0 ? 0.5f : -0.5f));
        snprintf(buf, sizeof(buf), "%d.%d \xC2\xB0" "C", t10 / 10, abs(t10 % 10));
        set_text(s_temp, buf);
        lv_obj_set_style_text_color(s_temp, c >= 75 ? C_RED : (c >= 60 ? C_ORANGE : C_TEXT), 0);
    } else {
        set_text(s_temp, "Unavailable");
    }
}

static void tick(lv_timer_t *t)
{
    if ((s_ticks++ % SD_EVERY) == 0) {
        sysmon_sd_query();
    }
    show_overview();
    show_cpu();
    show_mem_one(MALLOC_CAP_INTERNAL, true, s_int_bar, s_int_val, s_int_sub);
    show_mem_one(MALLOC_CAP_SPIRAM, false, s_ps_bar, s_ps_val, s_ps_sub);
    char a[16], b[16], buf[48];
    sysmon_heap_t dma;
    sysmon_heap(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL, true, &dma);
    fmt_bytes(a, sizeof(a), dma.free);
    fmt_bytes(b, sizeof(b), dma.largest);
    snprintf(buf, sizeof(buf), "%s  (block %s)", a, b);
    set_text(s_dma_val, buf);
    show_storage();
    show_lvgl();
    if (s_tab == SM_TASKS) {
        show_tasks();    /* (only on its tab: see the top of the file) */
    }
}

/* ------------------------------------------------------------- history */
/* The last hour (sysmon_hist_sample(), kept from start-up whether the page is open or not), as
 * graphs on the System and Memory tabs. */
static lv_timer_t *h_timer;
static lv_obj_t *s_ch_cpu, *s_ch_temp, *s_ch_ram, *s_ch_cpu_lbl, *s_ch_temp_lbl, *s_ch_ram_lbl;
static lv_obj_t *s_ch_rssi, *s_ch_rssi_lbl;
static lv_chart_series_t *s_se_c0, *s_se_c1, *s_se_temp, *s_se_ram, *s_se_rssi;

/* how a signal level reads (the usual rule of thumb) */
static const char *rssi_word(int dbm)
{
    return dbm >= -55 ? "excellent" : dbm >= -67 ? "good" : dbm >= -75 ? "fair" : "weak";
}

static int32_t hist_pt(int16_t v) { return v == SYSMON_NONE ? LV_CHART_POINT_NONE : v; }

/* the labels over the graphs, and the temperature graph's range (it follows the data) */
static void hist_show(void)
{
    int n = sysmon_hist_count();
    if (!s_ch_cpu || n == 0) {
        return;
    }
    sysmon_sample_t last, k;
    sysmon_hist_get(n - 1, &last);
    int peak = 0, tmin = INT16_MAX, tmax = INT16_MIN, rmin = INT16_MAX, smin = INT16_MAX, smax = INT16_MIN;
    for (int i = 0; i < n; i++) {
        sysmon_hist_get(i, &k);
        for (int c = 0; c < 2; c++) {
            if (k.load[c] != SYSMON_NONE && k.load[c] > peak) {
                peak = k.load[c];
            }
        }
        if (k.temp != SYSMON_NONE) {
            if (k.temp < tmin) {
                tmin = k.temp;
            }
            if (k.temp > tmax) {
                tmax = k.temp;
            }
        }
        if (k.ram < rmin) {
            rmin = k.ram;
        }
        if (k.rssi != SYSMON_NONE) {
            if (k.rssi < smin) {
                smin = k.rssi;
            }
            if (k.rssi > smax) {
                smax = k.rssi;
            }
        }
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "CPU load: now %d%% / %d%%, peak %d%%", last.load[0], last.load[1], peak);
    set_text(s_ch_cpu_lbl, buf);
    if (tmin <= tmax) {
        snprintf(buf, sizeof(buf), "Temperature: now %d.%d, %d.%d to %d.%d \xC2\xB0" "C", last.temp / 10,
                 abs(last.temp % 10), tmin / 10, abs(tmin % 10), tmax / 10, abs(tmax % 10));
        lv_chart_set_axis_range(s_ch_temp, LV_CHART_AXIS_PRIMARY_Y, (tmin / 10 - 2) * 10, (tmax / 10 + 3) * 10);
    } else {
        snprintf(buf, sizeof(buf), "Temperature: no reading");
    }
    set_text(s_ch_temp_lbl, buf);
    char a[16], b[16];
    fmt_bytes(a, sizeof(a), (uint64_t)last.ram * 1024);
    fmt_bytes(b, sizeof(b), (uint64_t)rmin * 1024);
    snprintf(buf, sizeof(buf), "Free internal RAM: now %s, lowest %s", a, b);
    set_text(s_ch_ram_lbl, buf);
    if (last.rssi != SYSMON_NONE) {
        snprintf(buf, sizeof(buf), "Wi-Fi signal: now %d dBm (%s), %d to %d", last.rssi, rssi_word(last.rssi), smin,
                 smax);
    } else if (smin <= smax) {
        snprintf(buf, sizeof(buf), "Wi-Fi signal: not connected now; was %d to %d dBm", smin, smax);
    } else {
        snprintf(buf, sizeof(buf), "Wi-Fi signal: not connected");
    }
    set_text(s_ch_rssi_lbl, buf);
}

/* every SYSMON_HIST_MS: a sample, and one more point on each graph (shifted left) */
static void hist_sample(lv_timer_t *t)
{
    sysmon_sample_t k;
    if (!sysmon_hist_sample(&k)) {
        return;    /* (the first idle reading) */
    }
    if (s_ch_cpu) {
        lv_chart_set_next_value(s_ch_cpu, s_se_c0, k.load[0]);
        lv_chart_set_next_value(s_ch_cpu, s_se_c1, k.load[1]);
        lv_chart_set_next_value(s_ch_temp, s_se_temp, hist_pt(k.temp));
        lv_chart_set_next_value(s_ch_ram, s_se_ram, k.ram);
        lv_chart_set_next_value(s_ch_rssi, s_se_rssi, hist_pt(k.rssi));
        hist_show();
    }
}

/* a graph card: a title line, the graph, "1 hour ago ... now" under it */
static lv_obj_t *graph_card(lv_obj_t *pane, lv_obj_t **title)
{
    lv_obj_t *c = card(pane);
    lv_obj_set_style_pad_row(c, 8, 0);
    *title = label(c, &lv_font_montserrat_18, C_TEXT, "Collecting (a point every 10 s)...");
    lv_obj_t *ch = lv_chart_create(c);
    lv_obj_set_size(ch, LV_PCT(100), 140);
    lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ch, SYSMON_HIST_N);
    lv_chart_set_update_mode(ch, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(ch, 5, 7);
    lv_obj_set_style_bg_color(ch, C_BG, 0);
    lv_obj_set_style_bg_opa(ch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ch, 0, 0);
    lv_obj_set_style_radius(ch, 8, 0);
    lv_obj_set_style_pad_all(ch, 6, 0);
    lv_obj_set_style_line_color(ch, C_CARD2, LV_PART_MAIN);     /* the grid */
    lv_obj_set_style_line_width(ch, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(ch, 0, LV_PART_INDICATOR);           /* no dots: 360 points */
    lv_obj_set_style_height(ch, 0, LV_PART_INDICATOR);
    lv_obj_set_clickable(ch, false);                            /* drags scroll the page */
    lv_obj_set_scrollable(ch, false);
    lv_obj_t *row = lv_obj_create(c);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(row, &lv_font_montserrat_14, C_MUTED, "1 hour ago");
    label(row, &lv_font_montserrat_14, C_MUTED, "now");
    return ch;
}

/* the graphs from the samples kept (after a rebuild, or on the page's first build) */
static void hist_fill(void)
{
    lv_chart_set_all_values(s_ch_cpu, s_se_c0, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(s_ch_cpu, s_se_c1, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(s_ch_temp, s_se_temp, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(s_ch_ram, s_se_ram, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(s_ch_rssi, s_se_rssi, LV_CHART_POINT_NONE);
    sysmon_sample_t k;
    for (int i = 0; i < sysmon_hist_count(); i++) {           /* oldest first */
        sysmon_hist_get(i, &k);
        lv_chart_set_next_value(s_ch_cpu, s_se_c0, k.load[0]);
        lv_chart_set_next_value(s_ch_cpu, s_se_c1, k.load[1]);
        lv_chart_set_next_value(s_ch_temp, s_se_temp, hist_pt(k.temp));
        lv_chart_set_next_value(s_ch_ram, s_se_ram, k.ram);
        lv_chart_set_next_value(s_ch_rssi, s_se_rssi, hist_pt(k.rssi));
    }
    hist_show();
}

/* ------------------------------------------------------------- sorting */
static const char *const HDR[] = {"Name", "Core", "Prio", "State", "CPU", "Stack free"};
#define SORTABLE(c) ((c) == SYSMON_COL_NAME || (c) == SYSMON_COL_CORE || (c) == SYSMON_COL_CPU || (c) == SYSMON_COL_STACK)
static lv_obj_t *s_hdr[6];

/* the sorted column in the accent colour, with an arrow for the direction */
static void show_sort_header(void)
{
    for (int i = 0; i < COLS; i++) {
        if (i == sysmon_tasks_sort_col()) {
            lv_label_set_text_fmt(s_hdr[i], "%s %s", HDR[i], is_sysmon_tasks_sort_desc() ? LV_SYMBOL_DOWN : LV_SYMBOL_UP);
            lv_obj_set_style_text_color(s_hdr[i], C_ACCENT, 0);
        } else {
            lv_label_set_text(s_hdr[i], HDR[i]);
            lv_obj_set_style_text_color(s_hdr[i], SORTABLE(i) ? C_TEXT : C_MUTED, 0);
        }
    }
}

static void on_sort(lv_event_t *e)
{
    int col = (int)(intptr_t)lv_event_get_user_data(e);
    if (col == sysmon_tasks_sort_col()) {
        sysmon_tasks_sort(col, !is_sysmon_tasks_sort_desc());
    } else {
        sysmon_tasks_sort(col, col == SYSMON_COL_CPU);   /* busiest first; the others from the top */
    }
    show_sort_header();
    if (sysmon_tasks_count() > 0) {
        show_task_table();
    }
}

/* ------------------------------------------------------------- tabs */
static void select_tab(int tab)
{
    s_tab = tab;
    for (int i = 0; i < SM_TABS; i++) {
        lv_obj_set_state(s_tab_btn[i], LV_STATE_CHECKED, i == tab);
        lv_obj_set_style_text_color(lv_obj_get_child(s_tab_btn[i], 0), i == tab ? lv_color_white() : C_TEXT, 0);
        lv_obj_set_hidden(s_pane[i], i != tab);
    }
    lv_obj_scroll_to_y(s_pane[tab], 0, LV_ANIM_OFF);
    if (s_shown && tab == SM_TASKS) {               /* at once, not at the next tick */
        sysmon_tasks_restart();                     /* (CPU shares from the next list on) */
        show_tasks();
    }
}

static void on_tab(lv_event_t *e)
{
    select_tab((int)(intptr_t)lv_event_get_user_data(e));
}

/* ------------------------------------------------------------- build */
void sysmon_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb)
{
    if (s_timer) {                                  /* rebuilt on a theme change */
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_scr = scr;

    /* top bar, as on the Wi-Fi page */
    lv_obj_t *back = lv_button_create(scr);
    lv_obj_set_pos(back, 12, 10);
    lv_obj_set_size(back, 150, 52);
    lv_obj_set_style_bg_color(back, C_CARD, 0);
    lv_obj_set_style_bg_color(back, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = label(back, &lv_font_montserrat_18, C_TEXT, LV_SYMBOL_LEFT "  Home");
    lv_obj_center(bl);
    lv_obj_t *h = label(scr, &lv_font_montserrat_28, C_TEXT, "System monitor");
    lv_obj_set_pos(h, 180, 20);

    /* the tabs, under the title (as on Settings); each tab's cards: a scrolling column */
    static const char *const TABS[SM_TABS] = {LV_SYMBOL_SETTINGS "  System", LV_SYMBOL_DRIVE "  Memory",
                                              LV_SYMBOL_SD_CARD "  Storage", LV_SYMBOL_IMAGE "  Graphics",
                                              LV_SYMBOL_LIST "  Tasks"
                                             };
    const int gap = 8, tw = (720 - 24 - (SM_TABS - 1) * gap) / SM_TABS;
    for (int i = 0; i < SM_TABS; i++) {
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, 12 + i * (tw + gap), TAB_Y);
        lv_obj_set_size(b, tw, TAB_H);
        lv_obj_set_style_bg_color(b, C_CARD, 0);
        lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_add_event_cb(b, on_tab, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_center(label(b, &lv_font_montserrat_16, C_TEXT, TABS[i]));
        s_tab_btn[i] = b;

        lv_obj_t *pane = lv_obj_create(scr);
        lv_obj_remove_style_all(pane);
        lv_obj_set_pos(pane, 40, PANE_Y);
        lv_obj_set_size(pane, 720 - 80, 720 - PANE_Y);
        lv_obj_set_flex_flow(pane, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(pane, 10, 0);
        lv_obj_set_style_pad_bottom(pane, 24, 0);
        lv_obj_set_scroll_dir(pane, LV_DIR_VER);
        s_pane[i] = pane;
    }

    char buf[64];
    lv_obj_t *c = card(s_pane[SM_SYSTEM]);
    lv_obj_t *chip = kv(c, "Chip");
    sysmon_chip_text(buf, sizeof(buf));
    lv_label_set_text(chip, buf);
    s_uptime = kv(c, "Uptime");
    s_temp = kv(c, "CPU temperature");
    s_load0 = kv(c, "CPU load, core 0");
    s_load1 = kv(c, "CPU load, core 1");
    s_ntasks = kv(c, "Tasks");
    s_reset = kv(c, "Last reset");
    lv_label_set_text(s_reset, sysmon_reset_text());
    s_ip = kv(c, "IP address");
    s_wifi_mac = kv(c, "Wi-Fi MAC");
    s_eth_mac = kv(c, "Ethernet MAC");
    uint8_t m[6];
    if (sysmon_eth_mac(m)) {                        /* the P4's own, from its factory MAC (eFuse) */
        lv_label_set_text_fmt(s_eth_mac, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    }

    s_ch_cpu = graph_card(s_pane[SM_SYSTEM], &s_ch_cpu_lbl);
    lv_chart_set_axis_range(s_ch_cpu, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    s_se_c0 = lv_chart_add_series(s_ch_cpu, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    s_se_c1 = lv_chart_add_series(s_ch_cpu, C_ORANGE, LV_CHART_AXIS_PRIMARY_Y);
    section(s_pane[SM_SYSTEM], "CPU load, last hour: core 0 in the accent colour, core 1 (the screen) in orange.");
    s_ch_temp = graph_card(s_pane[SM_SYSTEM], &s_ch_temp_lbl);
    s_se_temp = lv_chart_add_series(s_ch_temp, C_RED, LV_CHART_AXIS_PRIMARY_Y);
    s_ch_rssi = graph_card(s_pane[SM_SYSTEM], &s_ch_rssi_lbl);
    lv_chart_set_axis_range(s_ch_rssi, LV_CHART_AXIS_PRIMARY_Y, -95, -30);   /* dBm: weak at the bottom */
    s_se_rssi = lv_chart_add_series(s_ch_rssi, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    section(s_pane[SM_SYSTEM], "Wi-Fi signal, last hour, from -95 dBm (bottom) to -30 dBm (top); a gap while "
            "not connected. Above -67 dBm is good.");

    c = card(s_pane[SM_MEMORY]);
    bar_row(c, "Internal RAM", &s_int_bar, &s_int_val, &s_int_sub);
    bar_row(c, "PSRAM", &s_ps_bar, &s_ps_val, &s_ps_sub);
    s_dma_val = kv(c, "DMA-capable free");

    s_ch_ram = graph_card(s_pane[SM_MEMORY], &s_ch_ram_lbl);
    sysmon_heap_t ram;
    sysmon_heap(MALLOC_CAP_INTERNAL, false, &ram);
    lv_chart_set_axis_range(s_ch_ram, LV_CHART_AXIS_PRIMARY_Y, 0, (int32_t)(ram.total / 1024));
    s_se_ram = lv_chart_add_series(s_ch_ram, C_GREEN, LV_CHART_AXIS_PRIMARY_Y);
    section(s_pane[SM_MEMORY], "Free internal RAM, last hour (the graph's height is all of it).");

    c = card(s_pane[SM_STORAGE]);
    bar_row(c, "Firmware", &s_app_bar, &s_app_val, &s_app_sub);
    s_part_val = kv(c, "Flash partitioned");
    bar_row(c, "Settings (NVS)", &s_nvs_bar, &s_nvs_val, NULL);
    bar_row(c, "microSD", &s_sd_bar, &s_sd_val, &s_sd_sub);

    c = card(s_pane[SM_GRAPHICS]);
    s_fps = kv(c, "Frames drawn per second");
    s_lv_cpu = kv(c, "LVGL busy");
    s_lv_times = kv(c, "Draw time, avg / max");
    s_lv_objs = kv(c, "Objects");
    s_lv_timers = kv(c, "Timers");

    c = card(s_pane[SM_TASKS]);
    lv_obj_set_style_pad_row(c, 4, 0);
    /* the column names; Name, Core, CPU and Stack free sort the table (tap again: reversed) */
    lv_obj_t *hr = lv_obj_create(c);
    lv_obj_remove_style_all(hr);
    lv_obj_set_size(hr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hr, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < COLS; i++) {
        lv_obj_t *l = label(hr, &lv_font_montserrat_14, C_MUTED, HDR[i]);
        lv_obj_set_width(l, COL_W[i]);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_pad_left(l, 6, 0);
        lv_obj_set_style_pad_ver(l, 10, 0);            /* a finger-sized target */
        lv_obj_set_style_radius(l, 6, 0);
        if (SORTABLE(i)) {
            lv_obj_set_clickable(l, true);
            lv_obj_set_style_bg_color(l, C_CARD2, LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(l, LV_OPA_COVER, LV_STATE_PRESSED);
            lv_obj_add_event_cb(l, on_sort, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
        s_hdr[i] = l;
    }
    show_sort_header();
    s_table = lv_table_create(c);
    lv_obj_set_size(s_table, LV_PCT(100), LV_SIZE_CONTENT);
    lv_table_set_column_count(s_table, COLS);
    for (int i = 0; i < COLS; i++) {
        lv_table_set_column_width(s_table, i, COL_W[i]);
    }
    lv_obj_set_style_bg_opa(s_table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_table, 0, 0);
    lv_obj_set_style_pad_all(s_table, 0, 0);
    lv_obj_set_style_bg_color(s_table, C_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_table, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_table, C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_table, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_table, LV_BORDER_SIDE_TOP, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_table, C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(s_table, 7, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(s_table, 6, LV_PART_ITEMS);
    lv_obj_set_scrollable(s_table, false);
    lv_obj_set_clickable(s_table, false);           /* drags scroll the page */
    section(s_pane[SM_TASKS], "Tap Name, Core, CPU or Stack free to sort, and again to reverse. CPU is a share of "
            "one core. Stack free: the least it has had.");

    select_tab(s_tab);                              /* (a rebuild keeps the tab that was open) */
    hist_fill();
    if (!h_timer) {                                 /* the history runs from start-up, page or not */
        h_timer = lv_timer_create(hist_sample, SYSMON_HIST_MS, NULL);
        hist_sample(NULL);                          /* (the first idle reading) */
    }

    /* LVGL's sysmon report (every 300 ms; its on-screen box stays hidden) */
    lv_display_t *d = lv_obj_get_display(scr);
    if (d && d->perf_sysmon_backend.subject) {
        lv_subject_add_observer_obj(d->perf_sysmon_backend.subject, perf_cb, scr, NULL);
    }
    static bool s_disp_cb;                          /* the display outlives the page: once */
    if (d && !s_disp_cb) {
        s_disp_cb = true;
        lv_display_add_event_cb(d, disp_ev, LV_EVENT_RENDER_START, NULL);
        lv_display_add_event_cb(d, disp_ev, LV_EVENT_RENDER_READY, NULL);
    }

    s_timer = lv_timer_create(tick, REFRESH_MS, NULL);
    if (!s_shown) {
        lv_timer_pause(s_timer);    /* (a rebuild while open keeps running) */
    }
}

void sysmon_ui_set_shown(bool shown)
{
    if (!s_timer || shown == s_shown) {
        return;
    }
    s_shown = shown;
    if (shown) {
        sysmon_temp_hold(true);                     /* (on while shown: a reading every tick) */
        sysmon_tasks_restart();
        sysmon_load_reset(&s_load);
        s_ticks = 0;
        s_window_t0 = 0;                            /* frame counts from the next tick */
        tick(NULL);                                 /* fill at once (task CPU from the next tick) */
        select_tab(s_tab);                          /* the tab last open, scrolled to its top */
        lv_timer_resume(s_timer);
    } else {
        lv_timer_pause(s_timer);
        sysmon_temp_hold(false);
    }
}
