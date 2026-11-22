/**
 * @file ui_sysmon_system.c
 * @ingroup ui_sysmon
 * @brief System monitor > System: the chip, uptime, temperature, CPU load, network, and the CPU load,
 *        temperature and Wi-Fi graphs.
 */
#include "ui_sysmon_tab.h"
#include "ui_palette.h"
#include <stdio.h>
#include <stdlib.h>
#include "wifi_mgr.h"

static lv_obj_t *s_uptime;                          /**< the uptime's value */
static lv_obj_t *s_temp;                            /**< the temperature's value */
static lv_obj_t *s_load0;                           /**< core 0's load */
static lv_obj_t *s_load1;                           /**< core 1's load */
static lv_obj_t *s_ntasks;                          /**< the number of tasks */
static lv_obj_t *s_reset;                           /**< why the chip last started */
static lv_obj_t *s_ip;                              /**< the IP address */
static lv_obj_t *s_wifi_mac;                        /**< the Wi-Fi MAC */
static lv_obj_t *s_eth_mac;                         /**< the Ethernet MAC */
static sysmon_load_t s_load;                        /**< the CPU load (from the previous tick) */
static lv_obj_t *s_ch_cpu;                          /**< the CPU load graph */
static lv_obj_t *s_ch_temp;                         /**< the temperature graph */
static lv_obj_t *s_ch_rssi;                         /**< the Wi-Fi signal graph */
static lv_obj_t *s_ch_cpu_lbl;                      /**< its title */
static lv_obj_t *s_ch_temp_lbl;                     /**< its title */
static lv_obj_t *s_ch_rssi_lbl;                     /**< its title */
static lv_chart_series_t *s_se_c0;                  /**< core 0's line */
static lv_chart_series_t *s_se_c1;                  /**< core 1's line */
static lv_chart_series_t *s_se_temp;                /**< the temperature's line */
static lv_chart_series_t *s_se_rssi;                /**< the signal's line */

/* ------------------------------------------------------------- views */
/**
 * @brief CPU load per core (from the idle tasks' run time, no locks) and the number of tasks.
 */
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

/**
 * @brief Uptime, IP address and MACs (wifi_mgr's snapshot), and the temperature (orange from 60, red
 *        from 75 degrees C).
 */
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

/**
 * @brief How a Wi-Fi signal level reads (the usual rule of thumb).
 * @param[in] dbm  the signal, dBm
 * @return "excellent", "good", "fair" or "weak"
 */
static const char *rssi_word(int dbm)
{
    return dbm >= -55 ? "excellent" : dbm >= -67 ? "good" : dbm >= -75 ? "fair" : "weak";
}

/**
 * @brief The titles over the graphs (now, peak, range), and the temperature graph's range (it follows the data).
 */
static void hist_show(void)
{
    int n = sysmon_hist_count();
    if (!s_ch_cpu || n == 0) {
        return;
    }
    sysmon_sample_t last, k;
    sysmon_hist_get(n - 1, &last);
    int peak = 0, tmin = INT16_MAX, tmax = INT16_MIN, smin = INT16_MAX, smax = INT16_MIN;
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

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_system_build(lv_obj_t *pane)
{
    char buf[64];
    lv_obj_t *c = card(pane);
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

    s_ch_cpu = graph_card(pane, &s_ch_cpu_lbl);
    lv_chart_set_axis_range(s_ch_cpu, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    s_se_c0 = lv_chart_add_series(s_ch_cpu, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    s_se_c1 = lv_chart_add_series(s_ch_cpu, C_ORANGE, LV_CHART_AXIS_PRIMARY_Y);
    section(pane, "CPU load, last hour: core 0 in the accent colour, core 1 (the screen) in orange.");
    s_ch_temp = graph_card(pane, &s_ch_temp_lbl);
    s_se_temp = lv_chart_add_series(s_ch_temp, C_RED, LV_CHART_AXIS_PRIMARY_Y);
    s_ch_rssi = graph_card(pane, &s_ch_rssi_lbl);
    lv_chart_set_axis_range(s_ch_rssi, LV_CHART_AXIS_PRIMARY_Y, -95, -30);   /* dBm: weak at the bottom */
    s_se_rssi = lv_chart_add_series(s_ch_rssi, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    section(pane, "Wi-Fi signal, last hour, from -95 dBm (bottom) to -30 dBm (top); a gap while "
            "not connected. Above -67 dBm is good.");
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_system_tick(void)
{
    show_overview();
    show_cpu();
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_system_shown(bool shown)
{
    sysmon_temp_hold(shown);                        /* (on while shown: a reading every tick) */
    if (shown) {
        sysmon_load_reset(&s_load);
    }
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_system_hist_add(const sysmon_sample_t *k)
{
    lv_chart_set_next_value(s_ch_cpu, s_se_c0, k->load[0]);
    lv_chart_set_next_value(s_ch_cpu, s_se_c1, k->load[1]);
    lv_chart_set_next_value(s_ch_temp, s_se_temp, hist_pt(k->temp));
    lv_chart_set_next_value(s_ch_rssi, s_se_rssi, hist_pt(k->rssi));
    hist_show();
}

/* (documented in ui_sysmon_tab.h) */
void sysmon_tab_system_hist_fill(void)
{
    lv_chart_set_all_values(s_ch_cpu, s_se_c0, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(s_ch_cpu, s_se_c1, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(s_ch_temp, s_se_temp, LV_CHART_POINT_NONE);
    lv_chart_set_all_values(s_ch_rssi, s_se_rssi, LV_CHART_POINT_NONE);
    sysmon_sample_t k;
    for (int i = 0; i < sysmon_hist_count(); i++) {           /* oldest first */
        sysmon_hist_get(i, &k);
        lv_chart_set_next_value(s_ch_cpu, s_se_c0, k.load[0]);
        lv_chart_set_next_value(s_ch_cpu, s_se_c1, k.load[1]);
        lv_chart_set_next_value(s_ch_temp, s_se_temp, hist_pt(k.temp));
        lv_chart_set_next_value(s_ch_rssi, s_se_rssi, hist_pt(k.rssi));
    }
    hist_show();
}
