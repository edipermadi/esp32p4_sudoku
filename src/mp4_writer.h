/**
 * @file mp4_writer.h
 * @ingroup media
 * @brief An MP4 file written from H.264 frames.
 */
#pragma once
/*
 * Minimal MP4 (ISO BMFF) writer for one H.264 video track at a constant frame
 * rate. Frames go in as Annex-B access units (start codes, as the ESP32-P4
 * H.264 encoder produces them); SPS and PPS are taken from the stream for the
 * avcC box and the rest is stored length-prefixed. Layout: ftyp, mdat (one
 * chunk, all frames), moov at the end. Uses stdio, so it works on the FAT
 * volume and on a PC (for testing).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** An MP4 file being written. */
typedef struct mp4w mp4w_t;

/**
 * @brief Starts an MP4 file. max_frames bounds the sample table (frames past it are refused).
 * @param path  the file
 * @param width  px
 * @param height  px
 * @param fps  frames a second
 * @param max_frames  the most frames
 * @return the writer, or NULL
 */
/** @brief max_frames bounds the sample table (frames past it are refused). */
mp4w_t *mp4w_open(const char *path, int width, int height, int fps, int max_frames);
/**
 * @brief
 * @param w  the writer
 * @param annexb  the frame
 * @param len  its bytes
 * @param key  an IDR frame
 * @return false: it failed
 */
/** @brief One encoded frame (Annex-B). key: an IDR frame. */
bool mp4w_add_frame(mp4w_t *w, const uint8_t *annexb, size_t len, bool key);
/**
 * @brief
 * @param w  the writer
 * @return true if the file is good
 */
/** @brief Writes the index (moov) and closes the file; false if anything failed.
 * The writer is freed either way. */
bool mp4w_close(mp4w_t *w);
/**
 * @brief
 * @param w  the writer
 */
/** @brief Closes without finishing and deletes the file. */
void mp4w_abort(mp4w_t *w);
/**
 * @brief
 * @param w  the writer
 * @return bytes
 */
/** @brief Bytes written so far. */
uint32_t mp4w_size(const mp4w_t *w);

#ifdef __cplusplus
}
#endif
