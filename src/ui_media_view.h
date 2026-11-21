#pragma once
/*
 * Full-screen viewer for pictures and videos on the microSD card, opened from
 * the Files page: JPEG (the game's screenshots and other baseline JPEGs, by
 * the P4's hardware decoder; not progressive ones) and MP4 with H.264
 * Baseline video (the exported replays, by the tinyh264 software decoder).
 * Bigger pictures are scaled down to fit the screen. Swipe or the arrows go to
 * the other pictures and videos in the same folder; a tap shows or hides the
 * bars. Decoding runs in a worker task on core 0.
 */
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MEDIA_NONE = 0, MEDIA_IMAGE, MEDIA_VIDEO };

/* What a file is, by its name's extension. */
int media_view_kind(const char *name);

/* Opens the viewer over parent (a screen) on names[index]; names are the
 * viewable files of folder dir, in the order Prev / Next go through them
 * (copied). */
void media_view_open(lv_obj_t *parent, const char *dir, const char *const *names, int count, int index);

/* Closes it (if open) and stops any decoding. */
void media_view_close(void);

bool media_view_is_open(void);

#ifdef __cplusplus
}
#endif
