/**
 * @file sysmon_tasks.h
 * @ingroup sysmon_tasks
 * @brief The FreeRTOS task list, each task's share of a core, sorted.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sysmon_tasks Tasks
 * @ingroup sysmon_core
 * @brief The FreeRTOS task list, each task's share of a core, sorted.
 *
 * The FreeRTOS task list with each task's share of a core since the previous list. It's read by
 *  a task on core 0 (uxTaskGetSystemState holds the kernel lock with interrupts off for ~1.5 ms),
 *  so a list is a call old: sysmon_tasks_poll() takes the last one and asks for the next.
 * @{
 */

/** @brief The task table's columns, and the orders it can be sorted in. */
enum {
    SYSMON_COL_NAME = 0,                /**< the task's name, A-Z */
    SYSMON_COL_CORE,                    /**< the core it's pinned to (unpinned after the cores) */
    SYSMON_COL_PRIO,                    /**< its priority */
    SYSMON_COL_STATE,                   /**< running, ready, waiting ... */
    SYSMON_COL_CPU,                     /**< its share of a core */
    SYSMON_COL_STACK,                   /**< the least stack it has had free */
    SYSMON_COLS,                        /**< how many columns */
};
/**
 * @brief Takes the list read last (if there's a new one: its CPU shares worked out, sorted) and asks
 *        for the next.
 * @return true if there's a new list to show
 */
bool sysmon_tasks_poll(void);
/** @brief CPU shares from the next list on (the Tasks tab was just opened: the last list is old). */
void sysmon_tasks_restart(void);
/**
 * @brief Sorts the list (now, and the ones after it).
 * @param[in] col   a SYSMON_COL_ column (NAME, CORE, CPU and STACK sort)
 * @param[in] desc  highest first
 */
void sysmon_tasks_sort(int col, bool desc);
/**
 * @brief The column the list is sorted by.
 * @return a SYSMON_COL_ value (SYSMON_COL_CPU at first)
 */
int sysmon_tasks_sort_col(void);
/**
 * @brief The direction the list is sorted in.
 * @return true: highest first
 */
bool is_sysmon_tasks_sort_desc(void);
/**
 * @brief How many tasks the list has.
 * @return the number of rows
 */
int sysmon_tasks_count(void);
/**
 * @brief A row of the list, in its order.
 * @param[in] r              the row, 0 .. sysmon_tasks_count() - 1
 * @param[out] permille  the task's share of a core, 0..1000 (if is_sysmon_tasks_cpu_valid())
 * @return the task (its name, core, priority, state, stack)
 */
const TaskStatus_t *sysmon_task(int r, uint32_t *permille);
/**
 * @brief The CPU shares are there: the list had one before it to compare with.
 * @return true if the permille values mean something
 */
bool is_sysmon_tasks_cpu_valid(void);

/** @} */

#ifdef __cplusplus
}
#endif
