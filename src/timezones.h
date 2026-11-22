/**
 * @file timezones.h
 * @brief The time zones offered in Settings.
 */
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

#define CLOCK_VALID(t) ((t) > 1700000000)      /**< the clock has been set (by NTP) */

/** A time zone offered in Settings. */
typedef struct {
    const char *name;     /**< IANA, e.g. "Asia/Dubai" */
    const char *posix;    /**< e.g. "<+04>-4" */
    const char *label;    /**< for the picker, e.g. "UTC+4  Dubai" */
} tz_entry_t;

/**
 * @brief The time zones offered.
 * @return how many
 */
int tz_count(void);
/**
 * @brief A time zone.
 * @param[in] i  0..tz_count()-1
 * @return it, or NULL out of range
 */
const tz_entry_t *tz_get(int i);
/**
 * @brief
 * @param[in] name  the IANA name
 * @return its index
 */
/** @brief Index of name, or the UTC entry if unknown. */
int tz_find(const char *name);
/**
 * @brief
 * @return the options
 */
/** @brief "label\nlabel\n..." for an lv_dropdown (static, built on first call). */
const char *tz_options(void);
/**
 * @brief
 * @param[in] name  the IANA name
 */
/** @brief setenv("TZ") + tzset() for name; localtime() follows immediately. */
void tz_apply(const char *name);

#ifdef __cplusplus
}
#endif
