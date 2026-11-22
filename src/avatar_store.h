/**
 * @file avatar_store.h
 * @brief The avatars' pixels in the flash's "avatars" partition.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 * @return true: they're in flash
 */
/** @brief Finds the avatars in flash (true), else they're blank (false). At start-up, before the UI. */
bool avatars_init(void);
/**
 * @brief The avatars are in flash (avatars_init() found them, or an install finished).
 * @return true if so
 */
bool is_avatars_ready(void);

/**
 * @brief
 * @return true if so
 */
/** @brief The avatars aren't in flash and the card has a matching avatars.bin in its root (mounted). */
bool is_avatars_installable(void);
/**
 * @brief
 * @return true when started
 */
/** @brief Then: writes it into the partition in the background (about a minute), true when started.
 * (Writing the flash makes the panel flicker: the caller darkens the screen first.) */
bool avatars_install(void);

/**
 * @brief
 * @param[out] done  gets the bytes written
 * @param[out] total  gets the bytes to write
 * @return 0, 1, 2 or 3
 */
/** @brief The install: 0 none, 1 running (done of total bytes), 2 just finished (the avatars are there
 * now: redraw), 3 just failed. Call from the LVGL task; 2 and 3 are reported once. */
int avatars_install_poll(uint32_t *done, uint32_t *total);

#ifdef __cplusplus
}
#endif
