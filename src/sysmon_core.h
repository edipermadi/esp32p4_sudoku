/**
 * @file sysmon_core.h
 * @ingroup sysmon_core
 * @brief The system monitor's numbers, without the screen: one header for its parts.
 */
#pragma once
#include "sysmon_chip.h"
#include "sysmon_cpu.h"
#include "sysmon_memory.h"
#include "sysmon_storage.h"
#include "sysmon_tasks.h"
#include "sysmon_history.h"

/**
 * @defgroup sysmon_core System monitor core
 * @brief What the system monitor (ui_sysmon.c) shows, gathered without LVGL: a .c / .h per part.
 *
 * | Files                 | Part |
 * |-----------------------|------|
 * | sysmon_chip.c / .h    | @ref sysmon_chip "The chip": why it started, uptime, model, Ethernet MAC |
 * | sysmon_cpu.c / .h     | @ref sysmon_cpu "CPU load and temperature" |
 * | sysmon_memory.c / .h  | @ref sysmon_memory "Memory": the heaps |
 * | sysmon_storage.c / .h | @ref sysmon_storage "Storage": flash, NVS, microSD |
 * | sysmon_tasks.c / .h   | @ref sysmon_tasks "Tasks": the FreeRTOS task list |
 * | sysmon_history.c / .h | @ref sysmon_history "History": the last hour |
 *
 * Include sysmon_core.h for all of them.
 *
 * Everything here only reads: heap counters, the partition table, NVS's in-RAM entry counts,
 * FreeRTOS run-time stats (esp_timer based, enabled in the core's sdkconfig). No flash writes (they
 * make the panel blink).
 *
 * Nothing here may switch interrupts off on the LVGL core (core 1, which also serves the display)
 * for long: the whole screen blinks cyan. Measured: the largest free PSRAM block
 * (heap_caps_get_largest_free_block walks the 28 MB heap under its lock) 4 ms, uxTaskGetSystemState
 * 1.5 ms; once a second, either made the screen blink (2026-11-20). So: no PSRAM largest block at
 * all, CPU load from the idle tasks' counters, and the full task list read by a task on core 0,
 * whose interrupts the display doesn't use. The microSD free space is counted by a short-lived task
 * too: FatFs may scan the whole FAT the first time, seconds on a big card.
 */
