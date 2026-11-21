/* The avatars' pixels in the flash's "avatars" partition (avatar_store.c); avatar_image() is
 * declared in avatars.h. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Finds the avatars in flash (true), else they're blank (false). At start-up, before the UI. */
bool avatars_init(void);
bool avatars_ready(void);

/* If the avatars aren't in flash and the card has a matching avatars.bin in its root: writes it
 * into the partition in the background (about a minute), true when started. The card must be
 * mounted. */
bool avatars_install(void);

/* The install: 0 none, 1 running (done of total bytes), 2 just finished (the avatars are there
 * now: redraw), 3 just failed. Call from the LVGL task; 2 and 3 are reported once. */
int avatars_install_poll(uint32_t *done, uint32_t *total);

#ifdef __cplusplus
}
#endif
