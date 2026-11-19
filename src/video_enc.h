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

typedef struct video_enc video_enc_t;

video_enc_t *video_enc_open(const char *path, int width, int height, int fps, int max_frames);
/* rgb888: width*height pixels as LVGL's RGB888 (bytes B, G, R), 64-byte aligned (the
 * PPA reads it by DMA). changed = false: the same picture as the last frame (the
 * conversion is skipped). */
bool video_enc_frame(video_enc_t *v, const uint8_t *rgb888, bool changed);
bool video_enc_close(video_enc_t *v);       /* finishes the file; frees v */
void video_enc_abort(video_enc_t *v);       /* deletes the file; frees v */
uint32_t video_enc_bytes(const video_enc_t *v);

#ifdef __cplusplus
}
#endif
