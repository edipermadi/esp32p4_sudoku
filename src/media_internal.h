/**
 * @file media_internal.h
 * @ingroup media
 * @brief What the viewer's decoding files share (media_core.c, media_jpeg.c, media_video.c); the page uses
 *        media_core.h.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "media_core.h"

extern uint16_t media_xmap[MEDIA_SCR];  /**< each output column's source column (media_make_xmap()) */

/**
 * @brief The worker's message for the page (and the log).
 * @param[in] fmt  printf format
 * @param[in] ...  its arguments
 */
void media_set_msg(const char *fmt, ...);
/**
 * @brief Waits until the page has taken the last picture handed over.
 * @return false: stop
 */
bool media_wait_free(void);
/**
 * @brief The buffer the worker fills (not the one shown).
 * @return it
 */
uint16_t *media_back_buf(void);
/**
 * @brief Which buffer that is.
 * @return 0 or 1
 */
int media_back_index(void);
/**
 * @brief Hands the back buffer over to the page (flushed from the cache for the PPA's DMA).
 * @param[in] w  the picture's width, px
 * @param[in] h  its height
 */
void media_publish(int w, int h);
/**
 * @brief The size to show a picture at: as it is, or scaled down to fit the screen.
 * @param[in] w  its width, px
 * @param[in] h  its height
 * @param[out] dw  gets the width shown
 * @param[out] dh  gets the height shown
 */
void media_fit(int w, int h, int *dw, int *dh);
/**
 * @brief Fills media_xmap for a scale (nearest pixel).
 * @param[in] sw  the source's width
 * @param[in] dw  the width shown
 */
void media_make_xmap(int sw, int dw);
/**
 * @brief Has the page asked the worker to stop?
 * @return true if so
 */
bool is_media_stopping(void);
/**
 * @brief Shows a JPEG (the worker, media_jpeg.c).
 * @param[in] path  the file
 */
void media_jpeg_run(const char *path);
/**
 * @brief Plays a video until it is stopped (the worker, media_video.c).
 * @param[in] path  the file
 */
void media_video_run(const char *path);
/** @brief The video's state back to the start, playing (a new file; media_video.c). */
void media_video_reset(void);

/**
 * @brief An output row's source row (centres).
 * @param[in] y  the output row
 * @param[in] sh  the source's height
 * @param[in] dh  the output's height
 * @return the source row
 */
static inline int media_src_row(int y, int sh, int dh)
{
    return (int)(((int64_t)y * 2 + 1) * sh / (2 * dh));
}
