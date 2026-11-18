#include "timezones.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const tz_entry_t ZONES[] = {
    { "Pacific/Honolulu",      "HST10",                               "UTC-10  Honolulu" },
    { "America/Anchorage",     "AKST9AKDT,M3.2.0,M11.1.0",            "UTC-9  Anchorage" },
    { "America/Los_Angeles",   "PST8PDT,M3.2.0,M11.1.0",              "UTC-8  Los Angeles" },
    { "America/Vancouver",     "PST8PDT,M3.2.0,M11.1.0",              "UTC-8  Vancouver" },
    { "America/Denver",        "MST7MDT,M3.2.0,M11.1.0",              "UTC-7  Denver" },
    { "America/Phoenix",       "MST7",                                "UTC-7  Phoenix" },
    { "America/Chicago",       "CST6CDT,M3.2.0,M11.1.0",              "UTC-6  Chicago" },
    { "America/Mexico_City",   "CST6",                                "UTC-6  Mexico City" },
    { "America/New_York",      "EST5EDT,M3.2.0,M11.1.0",              "UTC-5  New York" },
    { "America/Toronto",       "EST5EDT,M3.2.0,M11.1.0",              "UTC-5  Toronto" },
    { "America/Bogota",        "<-05>5",                              "UTC-5  Bogota" },
    { "America/Lima",          "<-05>5",                              "UTC-5  Lima" },
    { "America/Caracas",       "<-04>4",                              "UTC-4  Caracas" },
    { "America/Santiago",      "<-04>4<-03>,M9.1.6/24,M4.1.6/24",     "UTC-4  Santiago" },
    { "America/Halifax",       "AST4ADT,M3.2.0,M11.1.0",              "UTC-4  Halifax" },
    { "America/Sao_Paulo",     "<-03>3",                              "UTC-3  Sao Paulo" },
    { "America/Argentina/Buenos_Aires", "<-03>3",                     "UTC-3  Buenos Aires" },
    { "Atlantic/Azores",       "<-01>1<+00>,M3.5.0/0,M10.5.0/1",      "UTC-1  Azores" },
    { "UTC",                   "UTC0",                                "UTC" },
    { "Atlantic/Reykjavik",    "GMT0",                                "UTC+0  Reykjavik" },
    { "Europe/London",         "GMT0BST,M3.5.0/1,M10.5.0",            "UTC+0  London" },
    { "Europe/Dublin",         "IST-1GMT0,M10.5.0,M3.5.0/1",          "UTC+0  Dublin" },
    { "Europe/Lisbon",         "WET0WEST,M3.5.0/1,M10.5.0",           "UTC+0  Lisbon" },
    { "Africa/Lagos",          "WAT-1",                               "UTC+1  Lagos" },
    { "Europe/Amsterdam",      "CET-1CEST,M3.5.0,M10.5.0/3",          "UTC+1  Amsterdam" },
    { "Europe/Berlin",         "CET-1CEST,M3.5.0,M10.5.0/3",          "UTC+1  Berlin" },
    { "Europe/Madrid",         "CET-1CEST,M3.5.0,M10.5.0/3",          "UTC+1  Madrid" },
    { "Europe/Paris",          "CET-1CEST,M3.5.0,M10.5.0/3",          "UTC+1  Paris" },
    { "Europe/Rome",           "CET-1CEST,M3.5.0,M10.5.0/3",          "UTC+1  Rome" },
    { "Europe/Stockholm",      "CET-1CEST,M3.5.0,M10.5.0/3",          "UTC+1  Stockholm" },
    { "Europe/Warsaw",         "CET-1CEST,M3.5.0,M10.5.0/3",          "UTC+1  Warsaw" },
    { "Africa/Cairo",          "EET-2EEST,M4.5.5/0,M10.5.4/24",       "UTC+2  Cairo" },
    { "Africa/Johannesburg",   "SAST-2",                              "UTC+2  Johannesburg" },
    { "Asia/Jerusalem",        "IST-2IDT,M3.4.4/26,M10.5.0",          "UTC+2  Jerusalem" },
    { "Europe/Athens",         "EET-2EEST,M3.5.0/3,M10.5.0/4",        "UTC+2  Athens" },
    { "Europe/Helsinki",       "EET-2EEST,M3.5.0/3,M10.5.0/4",        "UTC+2  Helsinki" },
    { "Europe/Kyiv",           "EET-2EEST,M3.5.0/3,M10.5.0/4",        "UTC+2  Kyiv" },
    { "Africa/Nairobi",        "EAT-3",                               "UTC+3  Nairobi" },
    { "Asia/Baghdad",          "<+03>-3",                             "UTC+3  Baghdad" },
    { "Asia/Qatar",            "<+03>-3",                             "UTC+3  Doha" },
    { "Asia/Riyadh",           "<+03>-3",                             "UTC+3  Riyadh" },
    { "Europe/Istanbul",       "<+03>-3",                             "UTC+3  Istanbul" },
    { "Europe/Moscow",         "MSK-3",                               "UTC+3  Moscow" },
    { "Asia/Tehran",           "<+0330>-3:30",                        "UTC+3:30  Tehran" },
    { "Asia/Baku",             "<+04>-4",                             "UTC+4  Baku" },
    { "Asia/Dubai",            "<+04>-4",                             "UTC+4  Dubai" },
    { "Asia/Muscat",           "<+04>-4",                             "UTC+4  Muscat" },
    { "Asia/Kabul",            "<+0430>-4:30",                        "UTC+4:30  Kabul" },
    { "Asia/Almaty",           "<+05>-5",                             "UTC+5  Almaty" },
    { "Asia/Karachi",          "PKT-5",                               "UTC+5  Karachi" },
    { "Asia/Tashkent",         "<+05>-5",                             "UTC+5  Tashkent" },
    { "Asia/Kolkata",          "IST-5:30",                            "UTC+5:30  India" },
    { "Asia/Colombo",          "<+0530>-5:30",                        "UTC+5:30  Colombo" },
    { "Asia/Kathmandu",        "<+0545>-5:45",                        "UTC+5:45  Kathmandu" },
    { "Asia/Dhaka",            "<+06>-6",                             "UTC+6  Dhaka" },
    { "Asia/Yangon",           "<+0630>-6:30",                        "UTC+6:30  Yangon" },
    { "Asia/Bangkok",          "<+07>-7",                             "UTC+7  Bangkok" },
    { "Asia/Ho_Chi_Minh",      "<+07>-7",                             "UTC+7  Ho Chi Minh" },
    { "Asia/Jakarta",          "WIB-7",                               "UTC+7  Jakarta" },
    { "Asia/Hong_Kong",        "HKT-8",                               "UTC+8  Hong Kong" },
    { "Asia/Kuala_Lumpur",     "<+08>-8",                             "UTC+8  Kuala Lumpur" },
    { "Asia/Makassar",         "WITA-8",                              "UTC+8  Makassar" },
    { "Asia/Manila",           "PST-8",                               "UTC+8  Manila" },
    { "Asia/Shanghai",         "CST-8",                               "UTC+8  Shanghai" },
    { "Asia/Singapore",        "<+08>-8",                             "UTC+8  Singapore" },
    { "Asia/Taipei",           "CST-8",                               "UTC+8  Taipei" },
    { "Australia/Perth",       "AWST-8",                              "UTC+8  Perth" },
    { "Asia/Jayapura",         "WIT-9",                               "UTC+9  Jayapura" },
    { "Asia/Seoul",            "KST-9",                               "UTC+9  Seoul" },
    { "Asia/Tokyo",            "JST-9",                               "UTC+9  Tokyo" },
    { "Australia/Darwin",      "ACST-9:30",                           "UTC+9:30  Darwin" },
    { "Australia/Adelaide",    "ACST-9:30ACDT,M10.1.0,M4.1.0/3",      "UTC+9:30  Adelaide" },
    { "Australia/Brisbane",    "AEST-10",                             "UTC+10  Brisbane" },
    { "Australia/Melbourne",   "AEST-10AEDT,M10.1.0,M4.1.0/3",        "UTC+10  Melbourne" },
    { "Australia/Sydney",      "AEST-10AEDT,M10.1.0,M4.1.0/3",        "UTC+10  Sydney" },
    { "Pacific/Guam",          "ChST-10",                             "UTC+10  Guam" },
    { "Pacific/Noumea",        "<+11>-11",                            "UTC+11  Noumea" },
    { "Pacific/Auckland",      "NZST-12NZDT,M9.5.0,M4.1.0/3",         "UTC+12  Auckland" },
    { "Pacific/Fiji",          "<+12>-12",                            "UTC+12  Fiji" },
    { "Pacific/Tongatapu",     "<+13>-13",                            "UTC+13  Tonga" },
};
#define N_ZONES ((int)(sizeof(ZONES) / sizeof(ZONES[0])))

int tz_count(void) { return N_ZONES; }

const tz_entry_t *tz_get(int i)
{
    return (i >= 0 && i < N_ZONES) ? &ZONES[i] : NULL;
}

int tz_find(const char *name)
{
    int utc = 0;
    for (int i = 0; i < N_ZONES; i++) {
        if (name && strcmp(ZONES[i].name, name) == 0) {
            return i;
        }
        if (strcmp(ZONES[i].name, "UTC") == 0) {
            utc = i;
        }
    }
    return utc;
}

const char *tz_options(void)
{
    static char *opts;
    if (!opts) {
        size_t n = 0;
        for (int i = 0; i < N_ZONES; i++) {
            n += strlen(ZONES[i].label) + 1;
        }
        opts = malloc(n);
        if (!opts) {
            return "UTC";
        }
        char *p = opts;
        for (int i = 0; i < N_ZONES; i++) {
            size_t l = strlen(ZONES[i].label);
            memcpy(p, ZONES[i].label, l);
            p += l;
            *p++ = (i < N_ZONES - 1) ? '\n' : '\0';
        }
    }
    return opts;
}

void tz_apply(const char *name)
{
    setenv("TZ", ZONES[tz_find(name)].posix, 1);
    tzset();
}
