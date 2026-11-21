/**
 * @file settings_core.h
 * @ingroup settings_core
 * @brief What Settings does to the board, without the screens: one header for its four parts.
 */
#pragma once
#include "fw_update.h"
#include "sd_card.h"
#include "device_ops.h"
#include "usb_drive.h"

/**
 * @defgroup settings_core Settings core
 * @ingroup topic_settings
 * @brief What the Settings screen does to the board, without LVGL: a .c / .h per part.
 *
 * | Files             | Part |
 * |-------------------|------|
 * | fw_update.c / .h  | @ref fw_update "Firmware update from the card": update.bin into the other app slot |
 * | sd_card.c / .h    | @ref sd_card "The microSD card": probe, format |
 * | device_ops.c / .h | @ref device_ops "Factory reset and power off" |
 * | usb_drive.c / .h  | @ref usb_drive "The USB drive": the card lent to a computer |
 *
 * The screens (ui_settings_data.c, ui_settings.c) draw the dialogs and follow these through their
 * state: fw_update_state(), sd_state(). Include settings_core.h for all four.
 * @{
 */

/** @brief The core the parts' tasks run on: core 0, away from LVGL's (as ui_internal.h's WORKER_CORE). */
#define SETTINGS_TASK_CORE 0

/** @} */
