/**
 * @file mp4_reader.h
 * @ingroup media
 * @brief An MP4 file's H.264 frames: their places, times and the decoder setup.
 */
#pragma once
/*
 * Minimal MP4 (ISO BMFF) reader for the first H.264 video track: where each
 * frame is in the file, its time and whether it is a key frame, plus SPS/PPS
 * from avcC as Annex-B. Reads the moov box only (wherever it is in the file);
 * the frames stay on disk. Plain stdio, so it also builds on a PC for tests.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief An MP4 file's H.264 track: its frames' places, sizes and times, and the decoder setup. */
typedef struct {
    int width;                  /**< px, from the avc1 sample entry */
    int height;                 /**< px, the same */
    uint32_t timescale;         /**< track ticks per second */
    uint32_t n;                 /**< frames */
    uint64_t *off;              /**< n: where each frame is in the file */
    uint32_t *size;             /**< n: its bytes */
    uint32_t *time;             /**< n: its decode time, in timescale ticks */
    uint8_t *key;               /**< n: 1 = a sync (key) frame */
    uint32_t duration;          /**< ticks, the end of the last frame */
    uint32_t max_size;          /**< the largest frame */
    int nal_len;                /**< bytes in each NAL length prefix (1, 2 or 4) */
    uint8_t *ps;                /**< SPS and PPS, Annex-B (start codes) */
    size_t ps_len;              /**< their bytes */
} mp4r_t;

/**
 * @brief
 * @param f  the file
 * @param m  gets the index
 * @param err  gets why not
 * @param errn  its size
 * @return true if it is one
 */
/** @brief Reads the index of f. false: not an MP4 with an H.264 track (err says why). */
bool mp4r_open(FILE *f, mp4r_t *m, char *err, size_t errn);
/**
 * @brief Frees the index.
 * @param m  it
 */
void mp4r_free(mp4r_t *m);

/**
 * @brief
 * @param m  the index
 * @param i  the frame
 * @return the key frame
 */
/** @brief The key frame at or before frame i. */
uint32_t mp4r_key_before(const mp4r_t *m, uint32_t i);

/**
 * @brief
 * @param f  the file
 * @param m  the index
 * @param i  the frame
 * @param out  gets it
 * @param cap  out's room
 * @return its length, 0 on a read error
 */
/** @brief Reads frame i and rewrites it as Annex-B into out (cap bytes; mp4r_annexb_cap()
 * is enough for any frame). Returns its length, 0 on a read error. */
size_t mp4r_read_annexb(FILE *f, const mp4r_t *m, uint32_t i, uint8_t *out, size_t cap);
/**
 * @brief Room enough for any frame as Annex-B (mp4r_read_annexb()).
 * @param m  the index
 * @return bytes
 */
size_t mp4r_annexb_cap(const mp4r_t *m);

#ifdef __cplusplus
}
#endif
