/**
 * @file ui_sysmon_memory.c
 * @ingroup ui_sysmon
 * @brief System monitor > Memory: internal RAM, PSRAM, DMA-capable memory, and the free RAM graph.
 */
#include "ui_sysmon_tab.h"
#include "ui_palette.h"
#include <stdio.h>
#include "esp_heap_caps.h"

static lv_obj_t *s_int_bar;                         /**< internal RAM: its bar */
static lv_obj_t *s_int_val;                         /**< ... its value */
static lv_obj_t *s_int_sub;                         /**< ... the line under it */
static lv_obj_t *s_ps_bar;                          /**< PSRAM: its bar */
static lv_obj_t *s_ps_val;                          /**< ... its value */
static lv_obj_t *s_ps_sub;                          /**< ... the line under it */
static lv_obj_t *s_dma_val;                         /**< DMA-capable memory free, and its largest block */
static lv_obj_t *s_ch_ram;                          /**< the free RAM graph */
static lv_obj_t *s_ch_ram_lbl;                      /**< its title */
static lv_chart_series_t *s_se_ram;                 /**< its line */

/**
 * @brief One heap's row: free of total, its bar, and the line under it.
 * @param caps      which heap (MALLOC_CAP_INTERNAL, MALLOC_CAP_SPIRAM)
 * @param with_min  the low-water mark and the largest free block too (internal RAM only: see sysmon_heap())
 * @param bar       its bar
 * @param val       its value
 * @param sub       the line under the bar
 */
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

/**
 * @brief The title over the graph: free internal RAM now, and the lowest in the hour.
 */
static void hist_show(void)
{
    int n = sysmon_hist_count();
    if (!s_ch_ram || n == 0) {
        return;
    }
    sysmon_sample_t last, k;
    sysmon_hist_get(n - 1, &last);
    int rmin = INT16_MAX;
    for (int i = 0; i < n; i++) {
        sysmon_hist_get(i, &k);
        if (k.ram < rmin) {
            rmin = k.ram;
        }
    }
    char a[16], b[16], buf[96];
    fmt_bytes(a, sizeof(a), (uint64_t)last.ram * 1024);
    fmt_bytes(b, sizeof(b), (uint64_t)rmin * 1024);
    snprintf(buf, sizeof(buf), "Free internal RAM: now %s, lowest %s", a, b);
    set_text(s_ch_ram_lbl, buf);
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_memory_build(lv_obj_t *pane)
{
    lv_obj_t *c;
    c = card(pane);
    bar_row(c, "Internal RAM", &s_int_bar, &s_int_val, &s_int_sub);
    bar_row(c, "PSRAM", &s_ps_bar, &s_ps_val, &s_ps_sub);
    s_dma_val = kv(c, "DMA-capable free");

    s_ch_ram = graph_card(pane, &s_ch_ram_lbl);
    sysmon_heap_t ram;
    sysmon_heap(MALLOC_CAP_INTERNAL, false, &ram);
    lv_chart_set_axis_range(s_ch_ram, LV_CHART_AXIS_PRIMARY_Y, 0, (int32_t)(ram.total / 1024));
    s_se_ram = lv_chart_add_series(s_ch_ram, C_GREEN, LV_CHART_AXIS_PRIMARY_Y);
    section(pane, "Free internal RAM, last hour (the graph's height is all of it).");
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_memory_tick(void)
{
    show_mem_one(MALLOC_CAP_INTERNAL, true, s_int_bar, s_int_val, s_int_sub);
    show_mem_one(MALLOC_CAP_SPIRAM, false, s_ps_bar, s_ps_val, s_ps_sub);
    char a[16], b[16], buf[48];
    sysmon_heap_t dma;
    sysmon_heap(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL, true, &dma);
    fmt_bytes(a, sizeof(a), dma.free);
    fmt_bytes(b, sizeof(b), dma.largest);
    snprintf(buf, sizeof(buf), "%s  (block %s)", a, b);
    set_text(s_dma_val, buf);
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_memory_hist_add(const sysmon_sample_t *k)
{
    lv_chart_set_next_value(s_ch_ram, s_se_ram, k->ram);
    hist_show();
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_memory_hist_fill(void)
{
    lv_chart_set_all_values(s_ch_ram, s_se_ram, LV_CHART_POINT_NONE);
    sysmon_sample_t k;
    for (int i = 0; i < sysmon_hist_count(); i++) {           /* oldest first */
        sysmon_hist_get(i, &k);
        lv_chart_set_next_value(s_ch_ram, s_se_ram, k.ram);
    }
    hist_show();
}
