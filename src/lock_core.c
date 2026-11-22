/**
 * @file lock_core.c
 * @ingroup lock_core
 * @brief The lock screen's PIN, without LVGL: right or wrong, tries, the wait; setting a new one.
 */
#include "lock_core.h"
#include "store.h"
#include <stdio.h>
#include <string.h>

static char lk_entry[5];                  /**< the digits typed on the lock screen */
static int lk_tries;                      /**< wrong PINs in a row */
static uint32_t lk_wait_until;            /**< the wait ends then (0: not waiting) */
static char ps_first[5];                  /**< PIN setup: the first entry ("" until it's typed) */
static char ps_entry[5];                  /**< PIN setup: the digits typed */

/**
 * @brief A key into a 4-digit entry: a digit added, or '<' takes the last one away.
 * @param[in,out] buf  the entry
 * @param[in] k    the key
 * @return the entry's length
 */
static int entry_key(char *buf, char k)
{
    size_t n = strlen(buf);
    if (k == '<') {
        if (n) {
            buf[n - 1] = 0;
        }
    } else if (n < 4) {
        buf[n] = k;
        buf[n + 1] = 0;
    }
    return (int)strlen(buf);
}

/* (documented in lock_core.h) */
int lock_mode_shown(bool force)
{
    uint8_t mode = store_lock_mode();
    if (mode == LOCK_OFF && force) {
        mode = LOCK_SWIPE;
    }
    return mode;
}

/* (documented in lock_core.h) */
void lock_begin(void)
{
    lk_entry[0] = 0;
}

/* (documented in lock_core.h) */
lock_key_t lock_key(char k, uint32_t now)
{
    if (lk_wait_until) {
        return LOCK_KEY_IGNORED;
    }
    if (entry_key(lk_entry, k) < 4) {
        return LOCK_KEY_MORE;
    }
    if (!strcmp(lk_entry, store_lock_pin())) {      /* right: back to where it was */
        lk_tries = 0;
        return LOCK_KEY_RIGHT;
    }
    lk_entry[0] = 0;                                /* wrong */
    if (++lk_tries >= LOCK_TRIES) {
        lk_tries = 0;
        lk_wait_until = now + LOCK_WAIT_MS;
        if (!lk_wait_until) {
            lk_wait_until = 1;
        }
        return LOCK_KEY_WAIT;
    }
    return LOCK_KEY_WRONG;
}

/* (documented in lock_core.h) */
int lock_len(void)
{
    return (int)strlen(lk_entry);
}

/* (documented in lock_core.h) */
int lock_tries_left(void)
{
    return LOCK_TRIES - lk_tries;
}

/* (documented in lock_core.h) */
int32_t lock_wait_left(uint32_t now, bool *ended)
{
    *ended = false;
    int32_t left = (int32_t)(lk_wait_until - now);
    if (lk_wait_until && left > 0) {
        return left;
    }
    if (lk_wait_until) {
        lk_wait_until = 0;
        *ended = true;
    }
    return 0;
}

/* (documented in lock_core.h) */
void pin_setup_begin(void)
{
    ps_first[0] = ps_entry[0] = 0;
}

/* (documented in lock_core.h) */
pin_setup_t pin_setup_key(char k)
{
    if (entry_key(ps_entry, k) < 4) {
        return PIN_SETUP_MORE;
    }
    if (!ps_first[0]) {                             /* once more, to be sure */
        snprintf(ps_first, sizeof(ps_first), "%s", ps_entry);
        ps_entry[0] = 0;
        return PIN_SETUP_AGAIN;
    }
    if (strcmp(ps_first, ps_entry) != 0) {
        ps_first[0] = ps_entry[0] = 0;
        return PIN_SETUP_MISMATCH;
    }
    store_set_lock_pin(ps_entry);
    store_set_lock_mode(LOCK_PIN);
    return PIN_SETUP_SET;
}

/* (documented in lock_core.h) */
int pin_setup_len(void)
{
    return (int)strlen(ps_entry);
}
