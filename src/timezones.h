#pragma once
/*
 * Built-in time zone table: IANA names (what is stored in settings) mapped to
 * POSIX TZ rules (what newlib's localtime() understands), ordered west to east.
 * The rules include daylight saving where the zone has it; each was checked
 * against zoneinfo (Jan/Mar/Jul/Oct 2026). Zones whose rules a POSIX string
 * can't express (e.g. Casablanca's Ramadan switch) are left out.
 */
#ifdef __cplusplus
extern "C" {
#endif

#define CLOCK_VALID(t) ((t) > 1700000000)      /* the clock has been set (by NTP) */

typedef struct {
    const char *name;     /* IANA, e.g. "Asia/Dubai" */
    const char *posix;    /* e.g. "<+04>-4" */
    const char *label;    /* for the picker, e.g. "UTC+4  Dubai" */
} tz_entry_t;

int tz_count(void);
const tz_entry_t *tz_get(int i);
/* Index of name, or the UTC entry if unknown. */
int tz_find(const char *name);
/* "label\nlabel\n..." for an lv_dropdown (static, built on first call). */
const char *tz_options(void);
/* setenv("TZ") + tzset() for name; localtime() follows immediately. */
void tz_apply(const char *name);

#ifdef __cplusplus
}
#endif
