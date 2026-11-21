/**
 * @file gameplays_core.h
 * @ingroup gameplays_core
 * @brief Gameplays and Replay, without LVGL: one header for their parts (the screens:
 *        ui_gameplays.c, ui_replay.c, ui_replay_export.c).
 */
#pragma once
#include "gameplays_list.h"
#include "replay_timeline.h"
#include "replay_video.h"

/**
 * @defgroup gameplays_core Gameplays and Replay core
 * @brief What the Gameplays list and the Replay screen show and do, apart from drawing it.
 *
 * | Files                   | Part |
 * |-------------------------|------|
 * | gameplays_list.c / .h   | @ref gameplays_list "Gameplays list": the pages of recorded solves, opening one |
 * | replay_timeline.c / .h  | @ref replay_timeline "Replay timeline": a move log played back, the board at any step |
 * | replay_video.c / .h     | @ref replay_video "Replay video": a replay exported as an MP4 |
 *
 * Include gameplays_core.h for all of them.
 */
