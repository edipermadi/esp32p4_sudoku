/**
 * @file device_ops.h
 * @ingroup device_ops
 * @brief Factory reset and power off.
 */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup device_ops Factory reset and power off
 * @ingroup settings_core
 * @brief The board back to how it came, or off until RESET.
 * @{
 */

/**
 * @brief Factory reset: settings back to the defaults, the Wi-Fi network forgotten and Wi-Fi off, the old
 *        NVS keys erased, and sudoku.db deleted and made again.
 * @return true: sudoku.db is being rebuilt (its settings arrive like a reload: stats_db_take_prefs());
 *         false: there's no sudoku.db, and it's all done now
 */
bool device_factory_reset(void);
/** @brief Power off: sudoku.db closed and the card unmounted, then deep sleep until RESET. Never returns. */
void device_power_off(void) __attribute__((noreturn));

/** @} */

#ifdef __cplusplus
}
#endif
