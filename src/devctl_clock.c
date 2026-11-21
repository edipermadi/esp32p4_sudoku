/**
 * @file devctl_clock.c
 * @ingroup devctl
 * @brief clock: whether Wi-Fi is connected and the time is set.
 */
#include "devctl_internal.h"
#include <time.h>
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
