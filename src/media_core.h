/**
 * @file media_core.h
 * @ingroup media
 * @brief The picture and video viewer's decoding, without LVGL: a worker task (core 0) decodes a JPEG or an MP4
 *        into two RGB565 buffers in turn; the page (ui_media_view.c) shows each one it hands over.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_SCR 720                   /**< the screen's size, px: the buffers are MEDIA_SCR x MEDIA_SCR */

/** @brief What a file is (media_view_kind()). */
enum {
    MEDIA_NONE = 0,                     /**< neither */
    MEDIA_IMAGE,                        /**< a picture (.jpg, .jpeg) */
    MEDIA_VIDEO                         /**< a video (.mp4, .m4v) */
};

/**
 * @brief
 * @param[in] name  the file's name
 * @return MEDIA_NONE, MEDIA_IMAGE or MEDIA_VIDEO
 */
/** @brief What a file is, by its name's extension. */
int media_view_kind(const char *name);

/** @brief A video's state, for the page's bar. */
typedef struct {
    bool is_info;                       /**< its frames and length are known (the rest is valid) */
    bool is_playing;                    /**< playing */
    bool is_ended;                      /**< it played to the end */
    bool is_seeking;                    /**< a seek asked for is still under way */
    uint32_t frames;                    /**< its frames */
    uint32_t pos;                       /**< the frame shown */
    uint32_t pos_ms;                    /**< its time, ms */
    uint32_t dur_ms;                    /**< the video's length, ms */
} media_video_t;

/**
 * @brief Makes the two picture buffers (once; 64-byte aligned, PSRAM).
 * @return false: no memory
 */
bool media_core_buffers(void);
/**
 * @brief A picture buffer, for the page's image (RGB565, MEDIA_SCR x MEDIA_SCR at most).
 * @param[in] k  0 or 1
 * @return it (NULL before media_core_buffers())
 */
const uint16_t *media_core_buffer(int k);
/**
 * @brief Starts the worker on a file (the last one must have stopped: is_media_core_running()).
 * @param[in] path  the file
 * @param[in] kind  MEDIA_IMAGE or MEDIA_VIDEO
 * @return false: no memory for the task
 */
bool media_core_start(const char *path, int kind);
/** @brief Asks the worker to stop (it ends on its own, soon). */
void media_core_stop(void);
/**
 * @brief Is the worker still running a file?
 * @return true if so
 */
bool is_media_core_running(void);
/**
 * @brief A picture handed over, to show.
 * @param[out] w  gets its width, px
 * @param[out] h  gets its height
 * @return its buffer (0 or 1), -1: none (or stopping); show it, then media_core_frame_shown()
 */
int media_core_take_frame(int *w, int *h);
/**
 * @brief The picture taken is on screen: the worker may fill the other buffer.
 * @param[in] k  its buffer
 */
void media_core_frame_shown(int k);
/**
 * @brief The worker's message's number: changes with each new message.
 * @return it
 */
uint32_t media_core_msg_seq(void);
/**
 * @brief The worker's last message for the page ("Can't read this video.").
 * @return it
 */
const char *media_core_msg(void);
/**
 * @brief The video's state.
 * @param[out] v  gets it
 */
void media_core_video(media_video_t *v);
/**
 * @brief Seeks the video.
 * @param[in] frame  the frame to show
 */
void media_core_seek(int32_t frame);
/** @brief Play / pause; at the end, plays from the start. */
void media_core_play_toggle(void);

#ifdef __cplusplus
}
#endif
