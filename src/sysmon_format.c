/**
 * @file sysmon_format.c
 * @ingroup sysmon_format
 * @brief The system monitor's numbers as text.
 */
#include "sysmon_format.h"
#include <stdio.h>

/* (documented in sysmon_format.h) */
void fmt_bytes(char *buf, size_t n, uint64_t b)
{
    if (b < 1024) {
        snprintf(buf, n, "%u B", (unsigned)b);
    } else if (b < 1024 * 1024) {
        snprintf(buf, n, "%u.%u KB", (unsigned)(b / 1024), (unsigned)(b % 1024 * 10 / 1024));
    } else if (b < 1024ULL * 1024 * 1024) {
        uint64_t k = b / 1024;
        snprintf(buf, n, "%u.%u MB", (unsigned)(k / 1024), (unsigned)(k % 1024 * 10 / 1024));
    } else {
        uint64_t m = b / (1024 * 1024);
        snprintf(buf, n, "%u.%02u GB", (unsigned)(m / 1024), (unsigned)(m % 1024 * 100 / 1024));
    }
}
