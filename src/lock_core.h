/**
 * @file lock_core.h
 * @ingroup lock_core
 * @brief The lock screen's PIN, without LVGL (the screens: ui_lock.c).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup lock_core Lock core
 * @ingroup topic_lock
 * @brief The lock screen's PIN: the digits typed, right or wrong, the tries left and the wait after
 *        too many (LOCK_TRIES wrong ones: LOCK_WAIT_MS); and setting a new PIN (typed twice). The
 *        PIN and the lock's mode are kept in store.h.
 * @{
 */

#define LOCK_TRIES      5                 /**< wrong PINs before a wait */
#define LOCK_WAIT_MS    30000             /**< the wait, ms */

/** @brief What a key on the lock screen did. */
typedef enum {
    LOCK_KEY_IGNORED,                     /**< waiting after too many tries: nothing */
    LOCK_KEY_MORE,                        /**< a digit in (or out): fewer than 4 */
    LOCK_KEY_RIGHT,                       /**< the PIN: unlocked */
    LOCK_KEY_WRONG,                       /**< not the PIN: the digits cleared, lock_tries_left() left */
    LOCK_KEY_WAIT                         /**< not the PIN, and too many tries: the wait starts */
} lock_key_t;

/** @brief What a key on the PIN setup screen did. */
typedef enum {
    PIN_SETUP_MORE,                       /**< a digit in (or out): fewer than 4 */
    PIN_SETUP_AGAIN,                      /**< 4 digits: now once more, to be sure */
    PIN_SETUP_MISMATCH,                   /**< the second didn't match: start again */
    PIN_SETUP_SET                         /**< they matched: the PIN is set, the lock is PIN */
} pin_setup_t;

/**
 * @brief The lock to show.
 * @param[in] force  the lock button on Home: even with the lock Off, as Swipe
 * @return a store_lock_t: LOCK_OFF (none), LOCK_SWIPE or LOCK_PIN
 */
int lock_mode_shown(bool force);
/** @brief The lock screen opens: no digits typed. */
void lock_begin(void);
/**
 * @brief A key on the lock screen's pad.
 * @param[in] k    '0'..'9', or '<' (backspace)
 * @param[in] now  the time, ms (lv_tick_get())
 * @return what it did
 */
lock_key_t lock_key(char k, uint32_t now);
/**
 * @brief The digits typed on the lock screen.
 * @return how many, 0..4
 */
int lock_len(void);
/**
 * @brief The tries left before a wait.
 * @return how many
 */
int lock_tries_left(void);
/**
 * @brief The wait after too many tries.
 * @param[in] now    the time, ms (lv_tick_get())
 * @param[out] ended  gets true once, when the wait has just ended
 * @return ms left (0: not waiting)
 */
int32_t lock_wait_left(uint32_t now, bool *ended);

/** @brief The PIN setup screen opens: nothing typed. */
void pin_setup_begin(void);
/**
 * @brief A key on the PIN setup screen's pad.
 * @param[in] k  '0'..'9', or '<' (backspace)
 * @return what it did
 */
pin_setup_t pin_setup_key(char k);
/**
 * @brief The digits typed on the PIN setup screen.
 * @return how many, 0..4
 */
int pin_setup_len(void);

/** @} */

#ifdef __cplusplus
}
#endif
