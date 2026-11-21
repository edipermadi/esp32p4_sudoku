/**
 * @file sysmon_core.c
 * @ingroup sysmon_core
 * @brief The system monitor's numbers: reading them, the tasks that read the slow ones, the history.
 */
#include "sysmon_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
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
#include "esp_mac.h"
#include "nvs.h"
#include "driver/temperature_sensor.h"
#include "bsp/bsp_board.h"
#include "stats_db.h"
#include "wifi_mgr.h"

#define MAX_TASKS 48                    /**< the task list's size: more tasks and the last list stays */

/* ------------------------------------------------------------- the chip */

const char *sysmon_reset_text(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        return "Power-on";
    case ESP_RST_EXT:
        return "Reset pin";
    case ESP_RST_SW:
        return "Software restart";
    case ESP_RST_PANIC:
        return "Crash (panic)";
    case ESP_RST_INT_WDT:
        return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "Task watchdog";
    case ESP_RST_WDT:
        return "Watchdog";
    case ESP_RST_DEEPSLEEP:
        return "Wake from sleep";
    case ESP_RST_BROWNOUT:
        return "Brownout";
    case ESP_RST_USB:
        return "USB";
    case ESP_RST_JTAG:
        return "JTAG";
    default:
        return "Unknown";
    }
}

uint64_t sysmon_uptime_s(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000000);
}

void sysmon_chip_text(char *buf, size_t n)
{
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    snprintf(buf, n, "ESP32-P4 v%u.%u, %u cores, %u MHz", (unsigned)(ci.revision / 100),
             (unsigned)(ci.revision % 100), (unsigned)ci.cores, (unsigned)(esp_clk_cpu_freq() / 1000000));
}

bool sysmon_eth_mac(uint8_t mac[6])
{
    return esp_read_mac(mac, ESP_MAC_ETH) == ESP_OK;
}

unsigned sysmon_task_count(void)
{
    return (unsigned)uxTaskGetNumberOfTasks();
}

/* ------------------------------------------------------------- CPU load, temperature */

void sysmon_load_reset(sysmon_load_t *s)
{
    s->primed = false;
}

bool sysmon_load_permille(sysmon_load_t *s, int pm[2])
{
    int64_t now = esp_timer_get_time();
    bool ok = s->t && s->primed;
    for (int c = 0; c < 2; c++) {
        uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(c);   /* microseconds (esp_timer) */
        uint32_t dt = (uint32_t)(now - s->t), di = idle - s->idle[c];
        if (s->t && dt && s->primed) {
            int p = 1000 - (int)((uint64_t)di * 1000 / dt);
            pm[c] = p < 0 ? 0 : (p > 1000 ? 1000 : p);
        } else {
            ok = false;
        }
        s->idle[c] = idle;
    }
    s->t = now;
    s->primed = true;
    return ok;
}

static temperature_sensor_handle_t s_tsens;     /**< the chip's temperature sensor (installed on first use) */
static bool s_temp_held;                        /**< kept on (the page is shown) */

/** @brief Installs the temperature sensor if it isn't yet. @return true if there is one */
static bool temp_installed(void)
{
    if (!s_tsens) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK) {
            s_tsens = NULL;
        }
    }
    return s_tsens != NULL;
}

void sysmon_temp_hold(bool on)
{
    if (on == s_temp_held || !temp_installed()) {
        s_temp_held = on && s_tsens;
        return;
    }
    s_temp_held = on;
    if (on) {
        temperature_sensor_enable(s_tsens);
    } else {
        temperature_sensor_disable(s_tsens);
    }
}

bool sysmon_temp_read(float *celsius)
{
    if (!temp_installed()) {
        return false;
    }
    if (!s_temp_held) {                             /* on only for the reading, unless held on */
        temperature_sensor_enable(s_tsens);
    }
    bool ok = temperature_sensor_get_celsius(s_tsens, celsius) == ESP_OK;
    if (!s_temp_held) {
        temperature_sensor_disable(s_tsens);
    }
    return ok;
}

/* ------------------------------------------------------------- memory */

void sysmon_heap(uint32_t caps, bool with_min, sysmon_heap_t *out)
{
    out->total = heap_caps_get_total_size(caps);
    out->free = heap_caps_get_free_size(caps);
    out->min_free = with_min ? heap_caps_get_minimum_free_size(caps) : 0;
    out->largest = with_min ? heap_caps_get_largest_free_block(caps) : 0;   /* (internal: ~0.1 ms) */
}

/* ------------------------------------------------------------- storage */

static sysmon_flash_t s_flash;                  /**< the flash's numbers */
static bool s_flash_read;                       /**< s_flash has been read */

const sysmon_flash_t *sysmon_flash(void)
{
    if (s_flash_read) {
        return &s_flash;
    }
    s_flash_read = true;
    esp_flash_get_size(NULL, &s_flash.flash_size);
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run) {
        s_flash.app_part = run->size;
        s_flash.run_label = run->label;
        s_flash.run_addr = run->address;
        esp_partition_pos_t pos = {.offset = run->address, .size = run->size};
        esp_image_metadata_t md;
        if (esp_image_get_metadata(&pos, &md) == ESP_OK) {
            s_flash.app_len = md.image_len;
        }
    }
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (; it; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p->address + p->size > s_flash.part_end) {
            s_flash.part_end = p->address + p->size;
        }
    }
    esp_partition_iterator_release(it);
    return &s_flash;
}

bool sysmon_nvs(uint32_t *used, uint32_t *total)
{
    nvs_stats_t ns;
    if (nvs_get_stats(NULL, &ns) != ESP_OK || !ns.total_entries) {
        return false;
    }
    *used = (uint32_t)ns.used_entries;
    *total = (uint32_t)ns.total_entries;
    return true;
}

static volatile bool s_sd_busy;                 /**< the counting task runs */
static volatile bool s_sd_valid;                /**< something has been counted */
static volatile esp_err_t s_sd_err;             /**< the last count's result */
static volatile uint64_t s_sd_total;            /**< the volume's size */
static volatile uint64_t s_sd_free;             /**< its free bytes */

/** @brief Counts the card's free space (FatFs may scan the whole FAT: seconds), then ends. @param arg unused */
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

void sysmon_sd_query(void)
{
    if (s_sd_busy) {
        return;
    }
    /* only while sudoku.db has the card mounted (not while it's lent over USB) */
    if (!is_stats_db_ready()) {
        s_sd_err = ESP_ERR_INVALID_STATE;
        s_sd_valid = true;
        return;
    }
    s_sd_busy = true;
    if (xTaskCreatePinnedToCore(sd_task, "sysmon_sd", 4096, NULL, 2, NULL, 0) != pdPASS) {
        s_sd_busy = false;
    }
}

bool sysmon_sd(uint64_t *total, uint64_t *free_b, esp_err_t *err)
{
    if (!s_sd_valid) {
        return false;
    }
    *total = s_sd_total;
    *free_b = s_sd_free;
    *err = s_sd_err;
    return true;
}

uint64_t sysmon_sd_card_bytes(void)
{
    return (uint64_t)bsp_sdcard_sector_count() * bsp_sdcard_sector_size();
}

/* ------------------------------------------------------------- tasks */

static TaskStatus_t *s_ts;                      /**< the list shown (MAX_TASKS, PSRAM) */
static TaskStatus_t *s_ts_w;                    /**< the reader's copy (MAX_TASKS, PSRAM) */
static TaskHandle_t s_reader;                   /**< the reader task (on core 0) */
static volatile bool s_reader_busy;             /**< it's reading */
static volatile bool s_reader_done;             /**< a list is ready in s_ts_w */
static volatile UBaseType_t s_reader_n;         /**< its tasks (0: more than MAX_TASKS) */
static volatile uint32_t s_reader_total;        /**< its total run time */
/** @brief A task's run time in the list before, to turn counters into a share. */
typedef struct {
    UBaseType_t num;                            /**< the task's number */
    uint32_t rt;                                /**< its run time then */
} prev_t;
static prev_t s_prev[MAX_TASKS];                /**< the list before */
static int s_prev_n;                            /**< its tasks */
static uint32_t s_prev_total;                   /**< its total run time */
static bool s_have_prev;                        /**< there's a list before (CPU shares can be worked out) */
/** @brief A row of the list shown: its task, and its share. */
typedef struct {
    int idx;                                    /**< the task, in s_ts */
    uint32_t permille;                          /**< its share of one core */
} row_t;
static row_t rows[MAX_TASKS];                   /**< the list, in the chosen order */
static int s_rows_n;                            /**< its rows */
static bool s_rows_cpu;                         /**< rows[].permille is valid (a list before it) */
static int s_sort_col = SYSMON_COL_CPU;         /**< the order: a column, kept between visits */
static bool s_sort_desc = true;                 /**< ... and its direction */

/** @brief The core a task is pinned to, for sorting (unpinned after the cores). @param t the task @return the key */
static int core_key(const TaskStatus_t *t)
{
    return t->xCoreID == tskNO_AFFINITY ? 99 : (int)t->xCoreID;   /* unpinned after the cores */
}

/** @brief qsort's comparison for the chosen order. @param a a row @param b another @return <0, 0, >0 */
static int row_cmp(const void *a, const void *b)
{
    const row_t *x = a, *y = b;
    const TaskStatus_t *tx = &s_ts[x->idx], *ty = &s_ts[y->idx];
    int c = 0;
    switch (s_sort_col) {
    case SYSMON_COL_CORE:
        c = core_key(tx) - core_key(ty);
        break;
    case SYSMON_COL_CPU:
        c = x->permille < y->permille ? -1 : (x->permille > y->permille);
        break;
    case SYSMON_COL_STACK:
        c = tx->usStackHighWaterMark < ty->usStackHighWaterMark ? -1
            : (tx->usStackHighWaterMark > ty->usStackHighWaterMark);
        break;
    default:
        c = strcasecmp(tx->pcTaskName, ty->pcTaskName);
        break;
    }
    if (s_sort_desc) {
        c = -c;
    }
    if (c == 0) {
        c = strcasecmp(tx->pcTaskName, ty->pcTaskName);    /* ties: by name, A-Z */
    }
    return c;
}

/** @brief The reader: a list each time it's notified, on core 0. @param arg unused */
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

bool sysmon_tasks_poll(void)
{
    if (!s_ts) {
        s_ts = heap_caps_calloc(MAX_TASKS, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_ts_w) {
        s_ts_w = heap_caps_calloc(MAX_TASKS, sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_ts || !s_ts_w) {
        return false;
    }
    if (!s_reader && xTaskCreatePinnedToCore(task_reader, "sysmon_tasks", 3072, NULL, 2, &s_reader, 0) != pdPASS) {
        s_reader = NULL;
        return false;
    }
    bool have = s_reader_done;
    UBaseType_t n = s_reader_n;
    uint32_t total = s_reader_total;
    if (have) {
        memcpy(s_ts, s_ts_w, sizeof(TaskStatus_t) * (n <= MAX_TASKS ? n : MAX_TASKS));
        s_reader_done = false;
    }
    if (!s_reader_busy) {                           /* the next one, for the next call */
        s_reader_busy = true;
        xTaskNotifyGive(s_reader);
    }
    if (!have || n == 0) {
        return false;    /* (none yet, or more than MAX_TASKS: keep the last list) */
    }

    uint32_t dt = total - s_prev_total;
    for (UBaseType_t i = 0; i < n; i++) {
        rows[i].idx = (int)i;
        rows[i].permille = 0;
        if (!s_have_prev || dt == 0) {
            continue;
        }
        for (int j = 0; j < s_prev_n; j++) {
            if (s_prev[j].num == s_ts[i].xTaskNumber) {
                uint64_t d = s_ts[i].ulRunTimeCounter - s_prev[j].rt;
                uint32_t pm = (uint32_t)(d * 1000 / dt);
                rows[i].permille = pm > 1000 ? 1000 : pm;
                break;
            }
        }
    }

    /* remember this list for the next */
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
    return true;
}

void sysmon_tasks_restart(void)
{
    s_have_prev = false;
}

void sysmon_tasks_sort(int col, bool desc)
{
    s_sort_col = col;
    s_sort_desc = desc;
    if (s_rows_n > 0) {
        qsort(rows, (size_t)s_rows_n, sizeof(row_t), row_cmp);
    }
}

int sysmon_tasks_sort_col(void)
{
    return s_sort_col;
}

bool is_sysmon_tasks_sort_desc(void)
{
    return s_sort_desc;
}

int sysmon_tasks_count(void)
{
    return s_rows_n;
}

const TaskStatus_t *sysmon_task(int r, uint32_t *permille)
{
    *permille = rows[r].permille;
    return &s_ts[rows[r].idx];
}

bool is_sysmon_tasks_cpu_valid(void)
{
    return s_rows_cpu;
}

/* ------------------------------------------------------------- history */

static sysmon_sample_t h_samples[SYSMON_HIST_N];   /**< the kept samples, a ring */
static int h_count;                             /**< samples kept */
static int h_pos;                               /**< the next slot */
static uint32_t h_idle_prev[2];                 /**< each core's idle run time at the previous sample */
static int64_t h_t_prev;                        /**< when that was (0: no sample yet) */

bool sysmon_hist_sample(sysmon_sample_t *out)
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
    if (first) {
        return false;    /* (a load needs two readings) */
    }

    sysmon_sample_t s;
    s.load[0] = (int16_t)load[0];
    s.load[1] = (int16_t)load[1];
    float c;
    s.temp = sysmon_temp_read(&c) ? (int16_t)(c * 10 + (c >= 0 ? 0.5f : -0.5f)) : SYSMON_NONE;
    s.ram = (int16_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    wifi_status_t ws;                               /* (wifi_mgr's snapshot: its RSSI is fresh while connected) */
    wifi_mgr_get_status(&ws);
    s.rssi = ws.state == WIFI_ST_CONNECTED && ws.rssi < 0 ? (int16_t)ws.rssi : SYSMON_NONE;

    h_samples[h_pos] = s;
    h_pos = (h_pos + 1) % SYSMON_HIST_N;
    if (h_count < SYSMON_HIST_N) {
        h_count++;
    }
    if (out) {
        *out = s;
    }
    return true;
}

int sysmon_hist_count(void)
{
    return h_count;
}

void sysmon_hist_get(int i, sysmon_sample_t *out)
{
    *out = h_samples[(h_pos + SYSMON_HIST_N - h_count + i) % SYSMON_HIST_N];
}
