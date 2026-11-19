/*
 * System monitor page (see sysmon_ui.h).
 *
 * Everything here only reads: heap counters, the partition table, NVS's
 * in-RAM entry counts, FreeRTOS run-time stats (esp_timer based, enabled in
 * the core's sdkconfig) and LVGL's sysmon report. No flash writes (they make
 * the panel blink). The microSD free space is counted by a short-lived task:
 * FatFs may scan the whole FAT the first time, which takes seconds on a big
 * card, and must not stall the LVGL loop.
 */
#include "sysmon_ui.h"
#include "ui_palette.h"
#include "stats_db.h"
#include "bsp/bsp_board.h"
#include "lvgl_private.h"            /* the display's perf subject, its screens */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "esp_private/esp_clk.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "driver/temperature_sensor.h"

#define REFRESH_MS   1000
#define SD_EVERY     10               /* ticks between microSD free-space counts */
#define MAX_TASKS    48

/* table columns: name, core, priority, state, CPU, free stack */
static const int16_t COL_W[] = {196, 60, 60, 84, 96, 108};   /* = the card's 604 px */
#define COLS ((int)(sizeof(COL_W) / sizeof(COL_W[0])))

static lv_obj_t *s_scr;
static lv_timer_t *s_timer;
static bool s_shown;
static int s_ticks;

/* overview */
static lv_obj_t *s_uptime, *s_temp, *s_load0, *s_load1, *s_ntasks, *s_reset;
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

/* FreeRTOS run-time samples: the previous one, to turn counters into a share */
static TaskStatus_t *s_ts;
typedef struct { UBaseType_t num; uint32_t rt; } prev_t;
static prev_t s_prev[MAX_TASKS];
static int s_prev_n;
static uint32_t s_prev_total;
static bool s_have_prev;
typedef struct { int idx; uint32_t permille; } row_t;   /* permille of one core */

/* flash (read once) */
static uint32_t s_flash_size, s_app_len, s_app_part, s_part_end;
static bool s_flash_read;

/* microSD (sd_task) */
static volatile bool s_sd_busy, s_sd_valid;
static volatile esp_err_t s_sd_err;
static volatile uint64_t s_sd_total, s_sd_free;

static temperature_sensor_handle_t s_tsens;

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

static void section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *l = label(parent, &lv_font_montserrat_16, C_MUTED, title);
    lv_obj_set_style_pad_left(l, 8, 0);
    lv_obj_set_style_margin_top(l, 8, 0);
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
    if (strcmp(lv_label_get_text(l), txt) != 0) lv_label_set_text(l, txt);
}

static const char *reset_text(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "Power-on";
    case ESP_RST_EXT:       return "Reset pin";
    case ESP_RST_SW:        return "Software restart";
    case ESP_RST_PANIC:     return "Crash (panic)";
    case ESP_RST_INT_WDT:   return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "Task watchdog";
    case ESP_RST_WDT:       return "Watchdog";
    case ESP_RST_DEEPSLEEP: return "Wake from sleep";
    case ESP_RST_BROWNOUT:  return "Brownout";
    case ESP_RST_USB:       return "USB";
    case ESP_RST_JTAG:      return "JTAG";
    default:                return "Unknown";
    }
}

/* ------------------------------------------------------------- sources */
static void perf_cb(lv_observer_t *o, lv_subject_t *subject)
{
    const lv_sysmon_perf_info_t *p = lv_subject_get_pointer(subject);
    if (!p) return;
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
        if (us > s_draw_us_max) s_draw_us_max = us;
    }
}

static uint32_t count_tree(lv_obj_t *o)
{
    if (!o) return 0;
    uint32_t n = 1, c = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < c; i++) n += count_tree(lv_obj_get_child(o, (int32_t)i));
    return n;
}

static void read_flash(void)
{
    if (s_flash_read) return;
    s_flash_read = true;
    esp_flash_get_size(NULL, &s_flash_size);
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run) {
        s_app_part = run->size;
        esp_partition_pos_t pos = {.offset = run->address, .size = run->size};
        esp_image_metadata_t md;
        if (esp_image_get_metadata(&pos, &md) == ESP_OK) s_app_len = md.image_len;
    }
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p->address + p->size > s_part_end) s_part_end = p->address + p->size;
    }
    esp_partition_iterator_release(it);
}

static void sd_task(void *arg)
{
    uint64_t total = 0, free_b = 0;
    esp_err_t err = esp_vfs_fat_info(BSP_SD_MOUNT_POINT, &total, &free_b);
    s_sd_total = total;
    s_sd_free = free_b;
    s_sd_err = err;
    s_sd_valid = true;
    s_sd_busy = false;
    vTaskDelete(NULL);
}

static void sd_query(void)
{
    if (s_sd_busy) return;
    /* only while sudoku.db has the card mounted (not while it's lent over USB) */
    if (!stats_db_ready()) {
        s_sd_err = ESP_ERR_INVALID_STATE;
        s_sd_valid = true;
        return;
    }
    s_sd_busy = true;
    if (xTaskCreatePinnedToCore(sd_task, "sysmon_sd", 4096, NULL, 2, NULL, 0) != pdPASS) s_sd_busy = false;
}

static int row_cmp(const void *a, const void *b)
{
    const row_t *x = a, *y = b;
    if (x->permille != y->permille) return x->permille < y->permille ? 1 : -1;
    return strcmp(s_ts[x->idx].pcTaskName, s_ts[y->idx].pcTaskName);
}

/* ------------------------------------------------------------- views */
static void show_tasks(void)
{
    if (!s_ts) return;
    uint32_t total = 0;
    UBaseType_t n = uxTaskGetSystemState(s_ts, MAX_TASKS, &total);
    if (n == 0) return;                             /* more than MAX_TASKS: keep the last view */

    static row_t rows[MAX_TASKS];
    uint32_t dt = total - s_prev_total;
    uint32_t idle[2] = {0, 0};
    for (UBaseType_t i = 0; i < n; i++) {
        rows[i].idx = (int)i;
        rows[i].permille = 0;
        if (!s_have_prev || dt == 0) continue;
        for (int j = 0; j < s_prev_n; j++) {
            if (s_prev[j].num == s_ts[i].xTaskNumber) {
                uint64_t d = s_ts[i].ulRunTimeCounter - s_prev[j].rt;
                uint32_t pm = (uint32_t)(d * 1000 / dt);
                rows[i].permille = pm > 1000 ? 1000 : pm;
                break;
            }
        }
        if (!strcmp(s_ts[i].pcTaskName, "IDLE0")) idle[0] = rows[i].permille;
        if (!strcmp(s_ts[i].pcTaskName, "IDLE1")) idle[1] = rows[i].permille;
    }

    char buf[48];
    if (s_have_prev && dt) {
        snprintf(buf, sizeof(buf), "%u.%u %%", (unsigned)((1000 - idle[0]) / 10), (unsigned)((1000 - idle[0]) % 10));
        set_text(s_load0, buf);
        snprintf(buf, sizeof(buf), "%u.%u %%", (unsigned)((1000 - idle[1]) / 10), (unsigned)((1000 - idle[1]) % 10));
        set_text(s_load1, buf);
    }
    snprintf(buf, sizeof(buf), "%u", (unsigned)n);
    set_text(s_ntasks, buf);

    /* remember this sample for the next tick */
    for (UBaseType_t i = 0; i < n; i++) {
        s_prev[i].num = s_ts[i].xTaskNumber;
        s_prev[i].rt = s_ts[i].ulRunTimeCounter;
    }
    s_prev_n = (int)n;
    s_prev_total = total;
    bool had_prev = s_have_prev;
    s_have_prev = true;

    qsort(rows, n, sizeof(row_t), row_cmp);
    lv_table_set_row_count(s_table, n);
    for (UBaseType_t r = 0; r < n; r++) {
        const TaskStatus_t *t = &s_ts[rows[r].idx];
        lv_table_set_cell_value(s_table, r, 0, t->pcTaskName);
        if (t->xCoreID == tskNO_AFFINITY) {
            lv_table_set_cell_value(s_table, r, 1, "-");
        } else {
            lv_table_set_cell_value_fmt(s_table, r, 1, "%d", (int)t->xCoreID);
        }
        lv_table_set_cell_value_fmt(s_table, r, 2, "%u", (unsigned)t->uxCurrentPriority);
        const char *st = "?";
        switch (t->eCurrentState) {
        case eRunning:   st = "Run"; break;
        case eReady:     st = "Ready"; break;
        case eBlocked:   st = "Wait"; break;
        case eSuspended: st = "Susp"; break;
        case eDeleted:   st = "Del"; break;
        default: break;
        }
        lv_table_set_cell_value(s_table, r, 3, st);
        if (had_prev) {
            lv_table_set_cell_value_fmt(s_table, r, 4, "%u.%u%%", (unsigned)(rows[r].permille / 10),
                                        (unsigned)(rows[r].permille % 10));
        } else {
            lv_table_set_cell_value(s_table, r, 4, "...");
        }
        fmt_bytes(buf, sizeof(buf), t->usStackHighWaterMark);   /* bytes on ESP-IDF */
        lv_table_set_cell_value(s_table, r, 5, buf);
    }
}

/* with_min: the low-water mark. Not for PSRAM: its heap reads 0 from boot on
 * (the core registers it that way), which says nothing about real use. */
static void show_mem_one(uint32_t caps, bool with_min, lv_obj_t *bar, lv_obj_t *val, lv_obj_t *sub)
{
    size_t total = heap_caps_get_total_size(caps);
    size_t free_b = heap_caps_get_free_size(caps);
    size_t min_b = heap_caps_get_minimum_free_size(caps);
    size_t big = heap_caps_get_largest_free_block(caps);
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
        snprintf(buf, sizeof(buf), "Used %s  |  largest block %s", c, d);
    }
    set_text(sub, buf);
}

static void show_storage(void)
{
    char a[16], b[16], buf[96];
    read_flash();
    if (s_app_part) {
        fmt_bytes(a, sizeof(a), s_app_len);
        fmt_bytes(b, sizeof(b), s_app_part);
        snprintf(buf, sizeof(buf), "%s of %s", a, b);
        set_text(s_app_val, buf);
        set_bar(s_app_bar, s_app_len, s_app_part);
        const esp_partition_t *run = esp_ota_get_running_partition();
        snprintf(buf, sizeof(buf), "Running from %s at 0x%06x", run ? run->label : "?",
                 run ? (unsigned)run->address : 0);
        set_text(s_app_sub, buf);
    }
    fmt_bytes(a, sizeof(a), s_part_end);
    fmt_bytes(b, sizeof(b), s_flash_size);
    snprintf(buf, sizeof(buf), "%s of %s", a, b);
    set_text(s_part_val, buf);

    nvs_stats_t ns;
    if (nvs_get_stats(NULL, &ns) == ESP_OK && ns.total_entries) {
        snprintf(buf, sizeof(buf), "%u of %u entries", (unsigned)ns.used_entries, (unsigned)ns.total_entries);
        set_text(s_nvs_val, buf);
        set_bar(s_nvs_bar, ns.used_entries, ns.total_entries);
    }

    if (s_sd_valid) {
        if (s_sd_err == ESP_OK && s_sd_total) {
            fmt_bytes(a, sizeof(a), s_sd_free);
            fmt_bytes(b, sizeof(b), s_sd_total);
            snprintf(buf, sizeof(buf), "%s free of %s", a, b);
            set_text(s_sd_val, buf);
            set_bar(s_sd_bar, s_sd_total - s_sd_free, s_sd_total);
            uint64_t card = (uint64_t)bsp_sdcard_sector_count() * bsp_sdcard_sector_size();
            fmt_bytes(a, sizeof(a), card);
            snprintf(buf, sizeof(buf), "FAT volume at " BSP_SD_MOUNT_POINT "  |  card %s", a);
            set_text(s_sd_sub, buf);
        } else {
            set_text(s_sd_val, s_sd_err == ESP_ERR_INVALID_STATE ? "Not mounted" : "No card");
            set_bar(s_sd_bar, 0, 1);
            set_text(s_sd_sub, s_sd_err == ESP_ERR_INVALID_STATE ? "No card, not FAT, or lent over USB" : "");
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
    for (uint32_t i = 0; i < d->screen_cnt; i++) objs += count_tree(d->screens[i]);
    objs += count_tree(d->top_layer) + count_tree(d->sys_layer) + count_tree(d->bottom_layer);
    snprintf(buf, sizeof(buf), "%u on %u screens", (unsigned)objs, (unsigned)d->screen_cnt);
    set_text(s_lv_objs, buf);
    uint32_t timers = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) timers++;
    snprintf(buf, sizeof(buf), "%u", (unsigned)timers);
    set_text(s_lv_timers, buf);
}

static void show_overview(void)
{
    char buf[48];
    uint64_t s = (uint64_t)(esp_timer_get_time() / 1000000);
    unsigned d = (unsigned)(s / 86400), h = (unsigned)(s / 3600 % 24), m = (unsigned)(s / 60 % 60), sec = (unsigned)(s % 60);
    if (d) {
        snprintf(buf, sizeof(buf), "%ud %02u:%02u:%02u", d, h, m, sec);
    } else {
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, sec);
    }
    set_text(s_uptime, buf);

    float c;
    if (s_tsens && temperature_sensor_get_celsius(s_tsens, &c) == ESP_OK) {
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
    if ((s_ticks++ % SD_EVERY) == 0) sd_query();
    show_overview();
    show_mem_one(MALLOC_CAP_INTERNAL, true, s_int_bar, s_int_val, s_int_sub);
    show_mem_one(MALLOC_CAP_SPIRAM, false, s_ps_bar, s_ps_val, s_ps_sub);
    char a[16], b[16], buf[48];
    fmt_bytes(a, sizeof(a), heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    fmt_bytes(b, sizeof(b), heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    snprintf(buf, sizeof(buf), "%s  (block %s)", a, b);
    set_text(s_dma_val, buf);
    show_storage();
    show_lvgl();
    show_tasks();
}

/* ------------------------------------------------------------- build */
void sysmon_ui_create(lv_obj_t *scr, lv_event_cb_t back_cb)
{
    if (s_timer) {                                  /* rebuilt on a theme change */
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (!s_ts) s_ts = heap_caps_calloc(MAX_TASKS, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
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
    lv_obj_t *bl = label(back, &lv_font_montserrat_18, C_TEXT, LV_SYMBOL_LEFT "  Settings");
    lv_obj_center(bl);
    lv_obj_t *h = label(scr, &lv_font_montserrat_28, C_TEXT, "System monitor");
    lv_obj_set_pos(h, 180, 20);

    /* scrolling body */
    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, 0, 72);
    lv_obj_set_size(body, 720, 720 - 72);
    lv_obj_set_style_pad_hor(body, 40, 0);
    lv_obj_set_style_pad_top(body, 4, 0);
    lv_obj_set_style_pad_bottom(body, 24, 0);
    lv_obj_set_style_pad_row(body, 10, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    esp_chip_info_t ci;
    esp_chip_info(&ci);
    char buf[64];
    snprintf(buf, sizeof(buf), "SYSTEM  |  ESP32-P4 v%u.%u, %u cores, %u MHz", (unsigned)(ci.revision / 100),
             (unsigned)(ci.revision % 100), (unsigned)ci.cores, (unsigned)(esp_clk_cpu_freq() / 1000000));
    section(body, buf);
    lv_obj_t *c = card(body);
    s_uptime = kv(c, "Uptime");
    s_temp = kv(c, "CPU temperature");
    s_load0 = kv(c, "CPU load, core 0");
    s_load1 = kv(c, "CPU load, core 1");
    s_ntasks = kv(c, "Tasks");
    s_reset = kv(c, "Last reset");
    lv_label_set_text(s_reset, reset_text(esp_reset_reason()));

    section(body, "MEMORY");
    c = card(body);
    bar_row(c, "Internal RAM", &s_int_bar, &s_int_val, &s_int_sub);
    bar_row(c, "PSRAM", &s_ps_bar, &s_ps_val, &s_ps_sub);
    s_dma_val = kv(c, "DMA-capable free");

    section(body, "STORAGE");
    c = card(body);
    bar_row(c, "Firmware", &s_app_bar, &s_app_val, &s_app_sub);
    s_part_val = kv(c, "Flash partitioned");
    bar_row(c, "Settings (NVS)", &s_nvs_bar, &s_nvs_val, NULL);
    bar_row(c, "microSD", &s_sd_bar, &s_sd_val, &s_sd_sub);

    section(body, "GRAPHICS (LVGL)");
    c = card(body);
    s_fps = kv(c, "Frames drawn per second");
    s_lv_cpu = kv(c, "LVGL busy");
    s_lv_times = kv(c, "Draw time, avg / max");
    s_lv_objs = kv(c, "Objects");
    s_lv_timers = kv(c, "Timers");

    section(body, "TASKS  |  CPU is a share of one core");
    c = card(body);
    lv_obj_set_style_pad_row(c, 4, 0);
    static const char *const HDR[] = {"Name", "Core", "Prio", "State", "CPU", "Stack free"};
    lv_obj_t *hr = lv_obj_create(c);
    lv_obj_remove_style_all(hr);
    lv_obj_set_size(hr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hr, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < COLS; i++) {
        lv_obj_t *l = label(hr, &lv_font_montserrat_14, C_MUTED, HDR[i]);
        lv_obj_set_width(l, COL_W[i]);
        lv_obj_set_style_pad_left(l, 6, 0);
    }
    s_table = lv_table_create(c);
    lv_obj_set_size(s_table, LV_PCT(100), LV_SIZE_CONTENT);
    lv_table_set_column_count(s_table, COLS);
    for (int i = 0; i < COLS; i++) lv_table_set_column_width(s_table, i, COL_W[i]);
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
    if (!s_shown) lv_timer_pause(s_timer);          /* (a rebuild while open keeps running) */
}

void sysmon_ui_set_shown(bool shown)
{
    if (!s_timer || shown == s_shown) return;
    s_shown = shown;
    if (shown) {
        if (!s_tsens) {
            temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
            if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK) s_tsens = NULL;
        }
        if (s_tsens) temperature_sensor_enable(s_tsens);
        s_have_prev = false;
        s_ticks = 0;
        s_window_t0 = 0;                            /* frame counts from the next tick */
        tick(NULL);                                 /* fill at once (task CPU from the next tick) */
        lv_timer_resume(s_timer);
    } else {
        lv_timer_pause(s_timer);
        if (s_tsens) temperature_sensor_disable(s_tsens);
    }
}
