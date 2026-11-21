/**
 * @file ui_sysmon_tab.h
 * @ingroup ui_sysmon
 * @brief The system monitor screen's files: what they share. Only they include it.
 *
 * The rest of the UI reaches the page through ui_sysmon.h; the numbers come from sysmon_core.h.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lvgl.h>
#include "sysmon_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ui_sysmon System monitor screen
 * @ingroup topic_sysmon
 * @brief The system monitor (Home > System): a tab bar over five tabs, a file per tab.
 *
 * | File                 | What's in it |
 * |----------------------|--------------|
 * | ui_sysmon.c          | the screen around the tabs (title, tab bar, a pane per tab), the refresh every second and the history sample every 10 s (each tab gets its turn), and the cards' look (card(), kv(), bar_row(), graph_card()) |
 * | ui_sysmon_system.c   | chip, uptime, temperature, CPU load, tasks, last reset, network; the CPU load, temperature and Wi-Fi graphs |
 * | ui_sysmon_memory.c   | internal RAM, PSRAM, DMA-capable memory; the free RAM graph |
 * | ui_sysmon_storage.c  | firmware, flash, NVS, microSD (counted every 10 s) |
 * | ui_sysmon_graphics.c | LVGL: frames drawn per second, how busy it is, draw times, objects, timers |
 * | ui_sysmon_tasks.c    | the FreeRTOS task table, sortable by a tap on its column names |
 *
 * A tab's file builds its cards into the pane it's given and keeps its widgets to itself.
 * @{
 */

/** @name The tabs' interface
 *  build: its cards into @p pane, when the page is (re)built. tick: every second while the page is
 *  shown (the Tasks tab: only while it's the tab shown). shown: the page is shown or left.
 *  hist_add / hist_fill: a new history sample for its graphs / its graphs from all the samples kept.
 *  @{ */
/** @brief System: chip, uptime, temperature, CPU load, network, and three graphs. @param pane its pane */
void sysmon_tab_system_build(lv_obj_t *pane);
/** @brief System: uptime, temperature, network and CPU load again. */
void sysmon_tab_system_tick(void);
/** @brief System: the page shown (the temperature sensor kept on, CPU load from the next tick) or left.
 *  @param shown  true: shown */
void sysmon_tab_system_shown(bool shown);
/** @brief System: one more point on each of its graphs. @param k  the new sample */
void sysmon_tab_system_hist_add(const sysmon_sample_t *k);
/** @brief System: its graphs from all the samples kept. */
void sysmon_tab_system_hist_fill(void);

/** @brief Memory: internal RAM, PSRAM, DMA, and the free RAM graph. @param pane its pane */
void sysmon_tab_memory_build(lv_obj_t *pane);
/** @brief Memory: the heaps again. */
void sysmon_tab_memory_tick(void);
/** @brief Memory: one more point on its graph. @param k  the new sample */
void sysmon_tab_memory_hist_add(const sysmon_sample_t *k);
/** @brief Memory: its graph from all the samples kept. */
void sysmon_tab_memory_hist_fill(void);

/** @brief Storage: firmware, flash, NVS, microSD. @param pane its pane */
void sysmon_tab_storage_build(lv_obj_t *pane);
/** @brief Storage: the numbers again; every 10th tick, the microSD's free space counted again.
 *  @param n  ticks since the page was shown (0: just shown) */
void sysmon_tab_storage_tick(int n);

/** @brief Graphics: LVGL's numbers (and its hooks, to count frames and draw times). @param pane its pane */
void sysmon_tab_graphics_build(lv_obj_t *pane);
/** @brief Graphics: the last second's frames, draw times and busy %, the objects and timers. */
void sysmon_tab_graphics_tick(void);
/** @brief Graphics: the page shown: frames counted from the next tick on. */
void sysmon_tab_graphics_shown(void);

/** @brief Tasks: the task table and its column names. @param pane its pane */
void sysmon_tab_tasks_build(lv_obj_t *pane);
/** @brief Tasks: the list read last, and the next asked for (only while the tab is shown). */
void sysmon_tab_tasks_tick(void);
/** @brief Tasks: the tab was opened: CPU shares from the next list on, and a list asked for at once. */
void sysmon_tab_tasks_opened(void);
/** @} */

/** @name The cards' look (ui_sysmon.c)
 *  @{ */
/**
 * @brief A size in B, KB, MB or GB (1024s), one or two decimals.
 * @param[out] buf  the text
 * @param n         buf's size
 * @param b         the size in bytes
 */
void fmt_bytes(char *buf, size_t n, uint64_t b);
/**
 * @brief A label.
 * @param parent  where
 * @param font    its font
 * @param color   its colour
 * @param txt     its text
 * @return the label
 */
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt);
/**
 * @brief A muted note under a card.
 * @param parent  the tab's pane
 * @param title   the note
 */
void section(lv_obj_t *parent, const char *title);
/**
 * @brief A card: a rounded column of rows.
 * @param parent  the tab's pane
 * @return the card
 */
lv_obj_t *card(lv_obj_t *parent);
/**
 * @brief A row "Title ........ value".
 * @param c      the card
 * @param title  the row's name
 * @return the value's label ("-" until set)
 */
lv_obj_t *kv(lv_obj_t *c, const char *title);
/**
 * @brief A row "Title ........ value", a bar under it, and a muted line under that.
 * @param c          the card
 * @param title      the row's name
 * @param[out] bar   the bar
 * @param[out] val   the value's label
 * @param[out] sub   the line under the bar (NULL: none)
 */
void bar_row(lv_obj_t *c, const char *title, lv_obj_t **bar, lv_obj_t **val, lv_obj_t **sub);
/**
 * @brief Fills a bar: its share, in the accent colour (orange from 75 %, red from 90 %).
 * @param bar    the bar
 * @param used   how much is used
 * @param total  of how much
 */
void set_bar(lv_obj_t *bar, uint64_t used, uint64_t total);
/**
 * @brief Sets a label's text, only if it changed (no redraw otherwise).
 * @param l    the label
 * @param txt  the text
 */
void set_text(lv_obj_t *l, const char *txt);
/**
 * @brief A graph card: a title line, the graph (an hour of samples), "1 hour ago ... now" under it.
 * @param pane        the tab's pane
 * @param[out] title  the title's label
 * @return the chart
 */
lv_obj_t *graph_card(lv_obj_t *pane, lv_obj_t **title);
/**
 * @brief A sample's value as a chart point.
 * @param v  the value
 * @return v, or LV_CHART_POINT_NONE for SYSMON_NONE (a gap in the line)
 */
int32_t hist_pt(int16_t v);
/** @} */

/** @} */

#ifdef __cplusplus
}
#endif
