/**
 * @file device_ops.c
 * @ingroup device_ops
 * @brief Factory reset and power off.
 */
#include "device_ops.h"
#include <stdio.h>
#include "bsp/bsp_board.h"
#include "stats_db.h"
#include "store.h"
#include "wifi_mgr.h"

bool device_factory_reset(void)
{
    printf("factory reset\n");
    wifi_mgr_forget();                             /* network gone (Wi-Fi's NVS keys) */
    wifi_mgr_set_enabled(false);
    store_factory_reset();                         /* defaults, no stats, old NVS keys erased */
    if (!stats_db_ready()) {
        return false;
    }
    stats_db_factory_reset();                      /* sudoku.db deleted and made again */
    return true;
}

void device_power_off(void)
{
    printf("power off\n");
    stats_db_suspend();                            /* queued writes done, sudoku.db closed, card unmounted */
    bsp_power_off();                               /* deep sleep until RESET */
}
