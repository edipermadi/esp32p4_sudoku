/**
 * @file sysmon_history.c
 * @ingroup sysmon_history
 * @brief The last hour, a sample every 10 s: CPU load, temperature, free RAM, Wi-Fi signal.
 */
#include "sysmon_history.h"
#include "sysmon_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "wifi_mgr.h"

static sysmon_sample_t h_samples[SYSMON_HIST_N];   /**< the kept samples, a ring */
static int h_count;                             /**< samples kept */
static int h_pos;                               /**< the next slot */
static uint32_t h_idle_prev[2];                 /**< each core's idle run time at the previous sample */
static int64_t h_t_prev;                        /**< when that was (0: no sample yet) */

/* (documented in sysmon_history.h) */
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

/* (documented in sysmon_history.h) */
int sysmon_hist_count(void)
{
    return h_count;
}

/* (documented in sysmon_history.h) */
void sysmon_hist_get(int i, sysmon_sample_t *out)
{
    *out = h_samples[(h_pos + SYSMON_HIST_N - h_count + i) % SYSMON_HIST_N];
}
