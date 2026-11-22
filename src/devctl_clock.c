/**
 * @file devctl_clock.c
 * @ingroup devctl
 * @brief clock: whether Wi-Fi is connected and the time is set; settime: the time (and time zone) from the computer.
 */
#include "devctl_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "store.h"
#include "wifi_mgr.h"
#include "timezones.h"

/* (documented in devctl_internal.h) */
void devctl_clock(void)
{
    static const char *const STATE[] = {"off", "starting", "idle", "connecting", "connected", "failed", "no-radio"};
    wifi_status_t ws;
    wifi_mgr_get_status(&ws);
    const char *wifi = (unsigned)ws.state < sizeof(STATE) / sizeof(STATE[0]) ? STATE[ws.state] : "?";
    time_t now = time(NULL);
    if (!CLOCK_VALID(now)) {
        devctl_reply("OK clock wifi=%s time=unset\n", wifi);
        return;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    char t[24];
    strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S", &tm);
    devctl_reply("OK clock wifi=%s time=set %s\n", wifi, t);
}

/* (documented in devctl_internal.h) */
void devctl_settime(const char *line)
{
    char ms_txt[24] = "", zone[64] = "";
    int n = sscanf(line, "%*s %23s %63s", ms_txt, zone);
    char *end;
    long long ms = n >= 1 ? strtoll(ms_txt, &end, 10) : 0;
    if (n < 1 || *end || !CLOCK_VALID((time_t)(ms / 1000))) {
        devctl_reply("ERR settime: settime <Unix time, ms> [IANA time zone]\n");
        return;
    }
    int z = -1;
    if (n == 2) {                                   /* only a zone Settings offers (tz_find() falls back to UTC) */
        z = tz_find(zone);
        if (strcmp(tz_get(z)->name, zone)) {
            devctl_reply("ERR settime: %s isn't one of Settings' time zones (nothing changed)\n", zone);
            return;
        }
    }
    struct timeval tv = {.tv_sec = (time_t)(ms / 1000), .tv_usec = (suseconds_t)(ms % 1000) * 1000};
    if (settimeofday(&tv, NULL) != 0) {
        devctl_reply("ERR settime: the clock couldn't be set\n");
        return;
    }
    if (z >= 0) {                                   /* as Settings > Network's picker does */
        store_set_timezone(tz_get(z)->name);
        tz_apply(tz_get(z)->name);
    }
    time_t now = time(NULL);                        /* (NTP, once Wi-Fi connects, keeps it right from here) */
    struct tm tm;
    localtime_r(&now, &tm);
    char t[24];
    strftime(t, sizeof(t), "%Y-%m-%d %H:%M:%S", &tm);
    devctl_reply("OK settime %s\n", t);
}
