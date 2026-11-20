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
#include "esp_mac.h"
#include "wifi_mgr.h"

#define REFRESH_MS   1000
/*
 * Nothing here may switch interrupts off on the LVGL core (core 1, which also serves
 * the display) for long: the whole screen blinks cyan. Measured: the largest free
 * PSRAM block (heap_caps_get_largest_free_block walks the 28 MB heap under its lock)
 * 4 ms, uxTaskGetSystemState 1.5 ms; once a second, either made the screen blink
 * (2026-11-20). So: no PSRAM largest block at all, CPU load from the idle tasks'
 * counters, and the full task list only while the Tasks tab is open, read by a task on
 * core 0 (task_reader), whose interrupts the display doesn't use.
 */
#define SD_EVERY     10               /* ticks between microSD free-space counts */
#define MAX_TASKS    48

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

/* FreeRTOS run-time samples: the previous one, to turn counters into a share */
static TaskStatus_t *s_ts;
/* the task list is read on core 0 (task_reader): uxTaskGetSystemState holds the kernel lock
 * with interrupts off for ~1.5 ms, and on core 1 that makes the screen blink */
static TaskStatus_t *s_ts_w;              /* the reader's copy (MAX_TASKS) */
static TaskHandle_t s_reader;
static volatile bool s_reader_busy, s_reader_done;
static volatile UBaseType_t s_reader_n;
static volatile uint32_t s_reader_total;
typedef struct { UBaseType_t num; uint32_t rt; } prev_t;
static prev_t s_prev[MAX_TASKS];
static int s_prev_n;
static uint32_t s_prev_total;
static bool s_have_prev;
static bool s_have_cpu;               /* show_cpu has a sample before this one */
typedef struct { int idx; uint32_t permille; } row_t;   /* permille of one core */
static row_t rows[MAX_TASKS];        /* the latest sample, in the chosen order (indexes into s_ts) */
static int s_rows_n;
static bool s_rows_cpu;              /* rows[].permille is valid (a sample before it) */
static void show_task_table(void);

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

/* the table's order: a column (COL_*) and its direction, kept between visits */
enum { COL_NAME = 0, COL_CORE, COL_PRIO, COL_STATE, COL_CPU, COL_STACK };
static int s_sort_col = COL_CPU;
static bool s_sort_desc = true;

static int core_key(const TaskStatus_t *t)
{
    return t->xCoreID == tskNO_AFFINITY ? 99 : (int)t->xCoreID;   /* unpinned after the cores */
}

static int row_cmp(const void *a, const void *b)
{
    const row_t *x = a, *y = b;
    const TaskStatus_t *tx = &s_ts[x->idx], *ty = &s_ts[y->idx];
    int c = 0;
    switch (s_sort_col) {
    case COL_CORE:  c = core_key(tx) - core_key(ty); break;
    case COL_CPU:   c = x->permille < y->permille ? -1 : (x->permille > y->permille); break;
    case COL_STACK: c = tx->usStackHighWaterMark < ty->usStackHighWaterMark ? -1
                      : (tx->usStackHighWaterMark > ty->usStackHighWaterMark); break;
    default:        c = strcasecmp(tx->pcTaskName, ty->pcTaskName); break;
    }
    if (s_sort_desc) c = -c;
    if (c == 0) c = strcasecmp(tx->pcTaskName, ty->pcTaskName);   /* ties: by name, A-Z */
    return c;
}

/* ------------------------------------------------------------- views */
/* System tab: CPU load per core from the idle tasks' run time (no locks), the number of tasks */
static void show_cpu(void)
{
    static uint32_t idle_prev[2];
    static int64_t t_prev;
    int64_t now = esp_timer_get_time();
    char buf[24];
    for (int c = 0; c < 2; c++) {
        uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(c);   /* microseconds (esp_timer) */
        uint32_t dt = (uint32_t)(now - t_prev), di = idle - idle_prev[c];
        if (t_prev && dt && s_have_cpu) {
            int pm = 1000 - (int)((uint64_t)di * 1000 / dt);    /* per mille */
            if (pm < 0) pm = 0;
            if (pm > 1000) pm = 1000;
            snprintf(buf, sizeof(buf), "%d.%d %%", pm / 10, pm % 10);
            set_text(c ? s_load1 : s_load0, buf);
        }
        idle_prev[c] = idle;
    }
    t_prev = now;
    s_have_cpu = true;
    snprintf(buf, sizeof(buf), "%u", (unsigned)uxTaskGetNumberOfTasks());
    set_text(s_ntasks, buf);
}

static void task_reader(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t total = 0;
        s_reader_n = uxTaskGetSystemState(s_ts_w, MAX_TASKS, &total);
        s_reader_total = total;
        s_reader_done = true;
        s_reader_busy = false;
    }
}

/* Tasks tab only: the list the core-0 reader made last (a second old), and a fresh one asked for */
static void show_tasks(void)
{
    if (!s_ts) return;
    if (!s_ts_w) s_ts_w = heap_caps_calloc(MAX_TASKS, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
    if (!s_ts_w) return;
    if (!s_reader && xTaskCreatePinnedToCore(task_reader, "sysmon_tasks", 3072, NULL, 2, &s_reader, 0) != pdPASS) {
        s_reader = NULL;
        return;
    }
    bool have = s_reader_done;
    UBaseType_t n = s_reader_n;
    uint32_t total = s_reader_total;
    if (have) {
        memcpy(s_ts, s_ts_w, sizeof(TaskStatus_t) * (n <= MAX_TASKS ? n : MAX_TASKS));
        s_reader_done = false;
    }
    if (!s_reader_busy) {                           /* the next one, for the next tick */
        s_reader_busy = true;
        xTaskNotifyGive(s_reader);
    }
    if (!have || n == 0) return;                    /* (none yet, or more than MAX_TASKS: keep the last view) */

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

    (void)idle;                                     /* (the System tab's load: show_cpu, from cheaper counters) */

    /* remember this sample for the next tick */
    for (UBaseType_t i = 0; i < n; i++) {
        s_prev[i].num = s_ts[i].xTaskNumber;
        s_prev[i].rt = s_ts[i].ulRunTimeCounter;
    }
    s_prev_n = (int)n;
    s_prev_total = total;
    s_rows_cpu = s_have_prev;
    s_have_prev = true;
    qsort(rows, n, sizeof(row_t), row_cmp);
    s_rows_n = (int)n;
    show_task_table();
}

/* the table from the latest sample (show_tasks) */
static void show_task_table(void)
{
    char buf[16];
    UBaseType_t n = (UBaseType_t)s_rows_n;
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
        if (s_rows_cpu) {
            lv_table_set_cell_value_fmt(s_table, r, 4, "%u.%u%%", (unsigned)(rows[r].permille / 10),
                                        (unsigned)(rows[r].permille % 10));
        } else {
            lv_table_set_cell_value(s_table, r, 4, "...");
        }
        fmt_bytes(buf, sizeof(buf), t->usStackHighWaterMark);   /* bytes on ESP-IDF */
        lv_table_set_cell_value(s_table, r, 5, buf);
    }
}

/* with_min: the low-water mark and the largest free block (internal RAM). Not for PSRAM:
 * its low-water mark reads 0 from boot on (the core registers it that way), and its
 * largest block walks the whole 28 MB heap with interrupts off (4 ms: the screen blinks). */
static void show_mem_one(uint32_t caps, bool with_min, lv_obj_t *bar, lv_obj_t *val, lv_obj_t *sub)
{
    size_t total = heap_caps_get_total_size(caps);
    size_t free_b = heap_caps_get_free_size(caps);
    size_t min_b = with_min ? heap_caps_get_minimum_free_size(caps) : 0;
    size_t big = with_min ? heap_caps_get_largest_free_block(caps) : 0;   /* (internal: ~0.1 ms) */
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

    wifi_status_t ws;                               /* (a snapshot: wifi_mgr never blocks here) */
    wifi_mgr_get_status(&ws);
    set_text(s_ip, ws.state == WIFI_ST_CONNECTED ? ws.ip
                   : ws.state == WIFI_ST_OFF ? "Wi-Fi off"
                   : ws.state == WIFI_ST_CONNECTING ? "Connecting..." : "Not connected");
    set_text(s_wifi_mac, ws.mac[0] ? ws.mac : (ws.state == WIFI_ST_OFF ? "Wi-Fi off" : "-"));

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
    show_cpu();
    show_mem_one(MALLOC_CAP_INTERNAL, true, s_int_bar, s_int_val, s_int_sub);
    show_mem_one(MALLOC_CAP_SPIRAM, false, s_ps_bar, s_ps_val, s_ps_sub);
    char a[16], b[16], buf[48];
    fmt_bytes(a, sizeof(a), heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    fmt_bytes(b, sizeof(b), heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    snprintf(buf, sizeof(buf), "%s  (block %s)", a, b);
    set_text(s_dma_val, buf);
    show_storage();
    show_lvgl();
    if (s_tab == SM_TASKS) show_tasks();            /* (only on its tab: see the top of the file) */
}

/* ------------------------------------------------------------- history */
/* The last hour, a sample every 10 s, kept from start-up whether the page is open or
 * not: CPU load per core (from the idle tasks' run time), temperature, free internal
 * RAM. Shown as graphs on the System and Memory tabs. */
#define HIST_N     360
#define HIST_MS    10000
#define HIST_NONE  INT16_MIN
static int16_t h_load[2][HIST_N], h_temp[HIST_N], h_ram[HIST_N];   /* %, degrees C x10, KB */
static int16_t h_rssi[HIST_N];                      /* Wi-Fi signal, dBm (HIST_NONE: not connected) */
static int h_count, h_pos;                          /* samples kept; the next slot */
static lv_timer_t *h_timer;
static uint32_t h_idle_prev[2];
static int64_t h_t_prev;
static lv_obj_t *s_ch_cpu, *s_ch_temp, *s_ch_ram, *s_ch_cpu_lbl, *s_ch_temp_lbl, *s_ch_ram_lbl;
static lv_obj_t *s_ch_rssi, *s_ch_rssi_lbl;
static lv_chart_series_t *s_se_c0, *s_se_c1, *s_se_temp, *s_se_ram, *s_se_rssi;

/* how a signal level reads (the usual rule of thumb) */
static const char *rssi_word(int dbm)
{
    return dbm >= -55 ? "excellent" : dbm >= -67 ? "good" : dbm >= -75 ? "fair" : "weak";
}

static int32_t hist_pt(int16_t v) { return v == HIST_NONE ? LV_CHART_POINT_NONE : v; }

/* the labels over the graphs, and the temperature graph's range (it follows the data) */
static void hist_show(void)
{
    if (!s_ch_cpu || h_count == 0) return;
    int last = (h_pos + HIST_N - 1) % HIST_N;
    int peak = 0, tmin = INT16_MAX, tmax = INT16_MIN, rmin = INT16_MAX, smin = INT16_MAX, smax = INT16_MIN;
    for (int i = 0; i < h_count; i++) {
        int k = (h_pos + HIST_N - 1 - i) % HIST_N;
        for (int c = 0; c < 2; c++) if (h_load[c][k] != HIST_NONE && h_load[c][k] > peak) peak = h_load[c][k];
        if (h_temp[k] != HIST_NONE) {
            if (h_temp[k] < tmin) tmin = h_temp[k];
            if (h_temp[k] > tmax) tmax = h_temp[k];
        }
        if (h_ram[k] < rmin) rmin = h_ram[k];
        if (h_rssi[k] != HIST_NONE) {
            if (h_rssi[k] < smin) smin = h_rssi[k];
            if (h_rssi[k] > smax) smax = h_rssi[k];
        }
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "CPU load: now %d%% / %d%%, peak %d%%", h_load[0][last], h_load[1][last], peak);
    set_text(s_ch_cpu_lbl, buf);
    if (tmin <= tmax) {
        snprintf(buf, sizeof(buf), "Temperature: now %d.%d, %d.%d to %d.%d \xC2\xB0" "C", h_temp[last] / 10,
                 abs(h_temp[last] % 10), tmin / 10, abs(tmin % 10), tmax / 10, abs(tmax % 10));
        lv_chart_set_axis_range(s_ch_temp, LV_CHART_AXIS_PRIMARY_Y, (tmin / 10 - 2) * 10, (tmax / 10 + 3) * 10);
    } else {
        snprintf(buf, sizeof(buf), "Temperature: no reading");
    }
    set_text(s_ch_temp_lbl, buf);
    char a[16], b[16];
    fmt_bytes(a, sizeof(a), (uint64_t)h_ram[last] * 1024);
    fmt_bytes(b, sizeof(b), (uint64_t)rmin * 1024);
    snprintf(buf, sizeof(buf), "Free internal RAM: now %s, lowest %s", a, b);
    set_text(s_ch_ram_lbl, buf);
    if (h_rssi[last] != HIST_NONE) {
        snprintf(buf, sizeof(buf), "Wi-Fi signal: now %d dBm (%s), %d to %d", h_rssi[last], rssi_word(h_rssi[last]),
                 smin, smax);
    } else if (smin <= smax) {
        snprintf(buf, sizeof(buf), "Wi-Fi signal: not connected now; was %d to %d dBm", smin, smax);
    } else {
        snprintf(buf, sizeof(buf), "Wi-Fi signal: not connected");
    }
    set_text(s_ch_rssi_lbl, buf);
}

static void hist_sample(lv_timer_t *t)
{
    int64_t now = esp_timer_get_time();
    int load[2];
    for (int c = 0; c < 2; c++) {
        uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(c);   /* microseconds (esp_timer) */
        uint32_t dt = (uint32_t)(now - h_t_prev), di = idle - h_idle_prev[c];
        int l = dt ? 100 - (int)((uint64_t)di * 100 / dt) : 0;
        load[c] = l < 0 ? 0 : (l > 100 ? 100 : l);
        h_idle_prev[c] = idle;
    }
    bool first = h_t_prev == 0;
    h_t_prev = now;
    if (first) return;                              /* (a load needs two readings) */

    int16_t temp = HIST_NONE;
    if (!s_tsens) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK) s_tsens = NULL;
    }
    if (s_tsens) {                                  /* on only for the reading, unless the page has it on */
        if (!s_shown) temperature_sensor_enable(s_tsens);
        float c;
        if (temperature_sensor_get_celsius(s_tsens, &c) == ESP_OK) temp = (int16_t)(c * 10 + (c >= 0 ? 0.5f : -0.5f));
        if (!s_shown) temperature_sensor_disable(s_tsens);
    }
    int16_t ram = (int16_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    wifi_status_t ws;                               /* (wifi_mgr's snapshot: its RSSI is fresh while connected) */
    wifi_mgr_get_status(&ws);
    int16_t rssi = ws.state == WIFI_ST_CONNECTED && ws.rssi < 0 ? (int16_t)ws.rssi : HIST_NONE;

    h_load[0][h_pos] = (int16_t)load[0];
    h_load[1][h_pos] = (int16_t)load[1];
    h_temp[h_pos] = temp;
    h_ram[h_pos] = ram;
    h_rssi[h_pos] = rssi;
    h_pos = (h_pos + 1) % HIST_N;
    if (h_count < HIST_N) h_count++;
    if (s_ch_cpu) {                                 /* (the graphs: one more point each, shifted left) */
        lv_chart_set_next_value(s_ch_cpu, s_se_c0, load[0]);
        lv_chart_set_next_value(s_ch_cpu, s_se_c1, load[1]);
        lv_chart_set_next_value(s_ch_temp, s_se_temp, hist_pt(temp));
        lv_chart_set_next_value(s_ch_ram, s_se_ram, ram);
        lv_chart_set_next_value(s_ch_rssi, s_se_rssi, hist_pt(rssi));
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
    lv_chart_set_point_count(ch, HIST_N);
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
    for (int i = 0; i < h_count; i++) {
        int k = (h_pos + HIST_N - h_count + i) % HIST_N;     /* oldest first */
        lv_chart_set_next_value(s_ch_cpu, s_se_c0, h_load[0][k]);
        lv_chart_set_next_value(s_ch_cpu, s_se_c1, h_load[1][k]);
        lv_chart_set_next_value(s_ch_temp, s_se_temp, hist_pt(h_temp[k]));
        lv_chart_set_next_value(s_ch_ram, s_se_ram, h_ram[k]);
        lv_chart_set_next_value(s_ch_rssi, s_se_rssi, hist_pt(h_rssi[k]));
    }
    hist_show();
}

/* ------------------------------------------------------------- sorting */
static const char *const HDR[] = {"Name", "Core", "Prio", "State", "CPU", "Stack free"};
#define SORTABLE(c) ((c) == COL_NAME || (c) == COL_CORE || (c) == COL_CPU || (c) == COL_STACK)
static lv_obj_t *s_hdr[6];

/* the sorted column in the accent colour, with an arrow for the direction */
static void show_sort_header(void)
{
    for (int i = 0; i < COLS; i++) {
        if (i == s_sort_col) {
            lv_label_set_text_fmt(s_hdr[i], "%s %s", HDR[i], s_sort_desc ? LV_SYMBOL_DOWN : LV_SYMBOL_UP);
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
    if (col == s_sort_col) {
        s_sort_desc = !s_sort_desc;
    } else {
        s_sort_col = col;
        s_sort_desc = col == COL_CPU;               /* busiest first; the others from the top */
    }
    show_sort_header();
    if (s_rows_n > 0) {
        qsort(rows, (size_t)s_rows_n, sizeof(row_t), row_cmp);
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
        s_have_prev = false;                        /* (CPU shares from the next sample on) */
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
    lv_obj_t *bl = label(back, &lv_font_montserrat_18, C_TEXT, LV_SYMBOL_LEFT "  Home");
    lv_obj_center(bl);
    lv_obj_t *h = label(scr, &lv_font_montserrat_28, C_TEXT, "System monitor");
    lv_obj_set_pos(h, 180, 20);

    /* the tabs, under the title (as on Settings); each tab's cards: a scrolling column */
    static const char *const TABS[SM_TABS] = {LV_SYMBOL_SETTINGS "  System", LV_SYMBOL_DRIVE "  Memory",
                                              LV_SYMBOL_SD_CARD "  Storage", LV_SYMBOL_IMAGE "  Graphics",
                                              LV_SYMBOL_LIST "  Tasks"};
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

    esp_chip_info_t ci;
    esp_chip_info(&ci);
    char buf[64];
    lv_obj_t *c = card(s_pane[SM_SYSTEM]);
    lv_obj_t *chip = kv(c, "Chip");
    snprintf(buf, sizeof(buf), "ESP32-P4 v%u.%u, %u cores, %u MHz", (unsigned)(ci.revision / 100),
             (unsigned)(ci.revision % 100), (unsigned)ci.cores, (unsigned)(esp_clk_cpu_freq() / 1000000));
    lv_label_set_text(chip, buf);
    s_uptime = kv(c, "Uptime");
    s_temp = kv(c, "CPU temperature");
    s_load0 = kv(c, "CPU load, core 0");
    s_load1 = kv(c, "CPU load, core 1");
    s_ntasks = kv(c, "Tasks");
    s_reset = kv(c, "Last reset");
    lv_label_set_text(s_reset, reset_text(esp_reset_reason()));
    s_ip = kv(c, "IP address");
    s_wifi_mac = kv(c, "Wi-Fi MAC");
    s_eth_mac = kv(c, "Ethernet MAC");
    uint8_t m[6];
    if (esp_read_mac(m, ESP_MAC_ETH) == ESP_OK) {   /* the P4's own, from its factory MAC (eFuse) */
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
    lv_chart_set_axis_range(s_ch_ram, LV_CHART_AXIS_PRIMARY_Y, 0,
                            (int32_t)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024));
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
    section(s_pane[SM_TASKS], "Tap Name, Core, CPU or Stack free to sort, and again to reverse. CPU is a share of "
                              "one core. Stack free: the least it has had.");

    select_tab(s_tab);                              /* (a rebuild keeps the tab that was open) */
    hist_fill();
    if (!h_timer) {                                 /* the history runs from start-up, page or not */
        h_timer = lv_timer_create(hist_sample, HIST_MS, NULL);
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
        s_have_cpu = false;
        s_ticks = 0;
        s_window_t0 = 0;                            /* frame counts from the next tick */
        tick(NULL);                                 /* fill at once (task CPU from the next tick) */
        select_tab(s_tab);                          /* the tab last open, scrolled to its top */
        lv_timer_resume(s_timer);
    } else {
        lv_timer_pause(s_timer);
        if (s_tsens) temperature_sensor_disable(s_tsens);
    }
}
