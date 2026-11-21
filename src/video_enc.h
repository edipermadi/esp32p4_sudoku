/**
 * @file video_enc.h
 * @ingroup media
 * @brief H.264 video into an MP4 file, frame by frame.
 */
#pragma once
/*
 * RGB888 frames -> ESP32-P4 hardware H.264 encoder -> MP4 file (mp4_writer.h).
 * Width and height must be multiples of 16. Runs in the caller's task; each
 * frame takes a colour conversion (only when the picture changed) and one
 * hardware encode.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** An encoder writing one MP4 file. */
typedef struct video_enc video_enc_t;

/**
 * @brief Starts a video: the encoder and the file.
 * @param path  the file
 * @param width  px, a multiple of 16
 * @param height  px, a multiple of 16
 * @param fps  frames a second
 * @param max_frames  the most frames it will get
 * @return it, or NULL
 */
video_enc_t *video_enc_open(const char *path, int width, int height, int fps, int max_frames);
/**
 * @brief
 * @param v  the encoder
 * @param rgb888  the picture
 * @param changed  false: the same picture as the last frame
 * @return false: it failed
 */
/** @brief rgb888: width*height pixels as LVGL's RGB888 (bytes B, G, R), 64-byte aligned (the
 * PPA reads it by DMA). changed = false: the same picture as the last frame (the
 * conversion is skipped). */
bool video_enc_frame(video_enc_t *v, const uint8_t *rgb888, bool changed);
/**
 * @brief Finishes the file; frees v.
 * @param v  the encoder
 * @return true if the file is good
 */
bool video_enc_close(video_enc_t *v);
/**
 * @brief Deletes the file; frees v.
 * @param v  the encoder
 */
void video_enc_abort(video_enc_t *v);
/**
 * @brief The file so far.
 * @param v  the encoder
 * @return its size, bytes
 */
uint32_t video_enc_bytes(const video_enc_t *v);

#ifdef __cplusplus
}
#endif
