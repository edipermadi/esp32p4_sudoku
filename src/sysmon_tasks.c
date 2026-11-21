/**
 * @file sysmon_tasks.c
 * @ingroup sysmon_tasks
 * @brief The FreeRTOS task list, each task's share of a core, sorted.
 */
#include "sysmon_tasks.h"
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include "esp_heap_caps.h"

#define MAX_TASKS 48                    /**< the task list's size: more tasks and the last list stays */

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

/* (documented in sysmon_tasks.h) */
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

/* (documented in sysmon_tasks.h) */
void sysmon_tasks_restart(void)
{
    s_have_prev = false;
}

/* (documented in sysmon_tasks.h) */
void sysmon_tasks_sort(int col, bool desc)
{
    s_sort_col = col;
    s_sort_desc = desc;
    if (s_rows_n > 0) {
        qsort(rows, (size_t)s_rows_n, sizeof(row_t), row_cmp);
    }
}

/* (documented in sysmon_tasks.h) */
int sysmon_tasks_sort_col(void)
{
    return s_sort_col;
}

/* (documented in sysmon_tasks.h) */
bool is_sysmon_tasks_sort_desc(void)
{
    return s_sort_desc;
}

/* (documented in sysmon_tasks.h) */
int sysmon_tasks_count(void)
{
    return s_rows_n;
}

/* (documented in sysmon_tasks.h) */
const TaskStatus_t *sysmon_task(int r, uint32_t *permille)
{
    *permille = rows[r].permille;
    return &s_ts[rows[r].idx];
}

/* (documented in sysmon_tasks.h) */
bool is_sysmon_tasks_cpu_valid(void)
{
    return s_rows_cpu;
}
