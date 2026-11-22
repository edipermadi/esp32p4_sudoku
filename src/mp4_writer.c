/**
 * @file mp4_writer.c
 * @ingroup media
 * @brief Writes an MP4 file from H.264 frames (box layout: ISO/IEC 14496-12 and -15, avcC).
 */
#include "mp4_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define BIG_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM)   /**< the frame tables' memory: PSRAM */
#else
#define BIG_ALLOC(n) malloc(n)          /**< the frame tables' memory (PSRAM on the board) */
#endif

#define TIMESCALE_PER_FRAME 100          /**< track timescale = fps * 100 */

/** @brief An MP4 file being written. */
struct mp4w {
    FILE *f;                             /**< the file */
    char path[96];                       /**< its path (to delete it) */
    int width;                           /**< px */
    int height;                          /**< px */
    int fps;                             /**< frames a second */
    int max_frames;                      /**< the sample table's room */
    uint32_t mdat_pos;                   /**< offset of the mdat box */
    uint32_t pos;                        /**< bytes written */
    uint32_t *sizes;                     /**< per frame */
    uint32_t *keys;                      /**< 1-based frame numbers of IDR frames */
    int n;                               /**< frames written */
    int nkeys;                           /**< IDR frames among them */
    uint8_t sps[64];                     /**< the SPS (from the first key frame) */
    uint8_t pps[64];                     /**< the PPS */
    int sps_len;                         /**< its bytes */
    int pps_len;                         /**< its bytes */
    bool failed;                         /**< a write failed (the file is bad) */
};

/* ---------------------------------------------------------------- bytes */

/**
 * @brief Stores a big-endian 32-bit number.
 * @param[out] p  gets its bytes
 * @param[in] v  it
 */
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

/**
 * @brief Writes to the file (nothing once a write has failed).
 * @param[in] w  the writer
 * @param[in] data  the bytes
 * @param[in] n  how many
 */
static void wr(mp4w_t *w, const void *data, size_t n)
{
    if (w->failed) {
        return;
    }
    if (fwrite(data, 1, n, w->f) != n) {
        w->failed = true;
        printf("mp4w: write of %u bytes at %u failed\n", (unsigned)n, (unsigned)w->pos);
    }
    w->pos += n;
}

/** @brief a growing buffer for moov */
typedef struct {
    uint8_t *p;                          /**< the bytes */
    size_t len;                          /**< used */
    size_t cap;                          /**< room */
    bool oom;                            /**< it ran out of room (the box is bad) */
} buf_t;

/**
 * @brief Adds bytes (nothing once out of room).
 * @param[in,out] b  the buffer
 * @param[in] d  the bytes
 * @param[in] n  how many
 */
static void b_put(buf_t *b, const void *d, size_t n)
{
    if (b->len + n > b->cap) {
        b->oom = true;
        return;
    }
    memcpy(b->p + b->len, d, n);
    b->len += n;
}
/**
 * @brief Adds a byte.
 * @param[in,out] b  the buffer
 * @param[in] v  it
 */
static void b_u8(buf_t *b, uint8_t v) { b_put(b, &v, 1); }
/**
 * @brief Adds a big-endian 16-bit number.
 * @param[in,out] b  the buffer
 * @param[in] v  it
 */
static void b_u16(buf_t *b, uint16_t v) { uint8_t t[2] = {v >> 8, v}; b_put(b, t, 2); }
/**
 * @brief Adds a big-endian 32-bit number.
 * @param[in,out] b  the buffer
 * @param[in] v  it
 */
static void b_u32(buf_t *b, uint32_t v) { uint8_t t[4]; put32(t, v); b_put(b, t, 4); }
/**
 * @brief Adds zeros.
 * @param[in,out] b  the buffer
 * @param[in] n  how many
 */
static void b_zero(buf_t *b, size_t n) { while (n--) b_u8(b, 0); }

/**
 * @brief Starts a box (b_end() puts its size in).
 * @param[in,out] b  the buffer
 * @param[in] type  its 4-letter type
 * @return where its size goes
 */
static size_t b_box(buf_t *b, const char *type)
{
    size_t at = b->len;
    b_u32(b, 0);
    b_put(b, type, 4);
    return at;
}
/**
 * @brief Starts a full box (with a version and flags).
 * @param[in,out] b  the buffer
 * @param[in] type  its 4-letter type
 * @param[in] version  its version
 * @param[in] flags  its flags
 * @return where its size goes
 */
static size_t b_fullbox(buf_t *b, const char *type, uint8_t version, uint32_t flags)
{
    size_t at = b_box(b, type);
    b_u32(b, ((uint32_t)version << 24) | flags);
    return at;
}
/**
 * @brief Ends a box: its size in.
 * @param[in,out] b  the buffer
 * @param[in] at  b_box()'s return
 */
static void b_end(buf_t *b, size_t at)
{
    if (!b->oom) {
        put32(b->p + at, (uint32_t)(b->len - at));
    }
}

/**
 * @brief Adds the identity transformation matrix (tkhd, mvhd).
 * @param[in,out] b  the buffer
 */
static void b_matrix(buf_t *b)
{
    static const uint32_t m[9] = {0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    for (int i = 0; i < 9; i++) {
        b_u32(b, m[i]);
    }
}

/* ---------------------------------------------------------------- API */

/* (documented in mp4_writer.h) */
mp4w_t *mp4w_open(const char *path, int width, int height, int fps, int max_frames)
{
    mp4w_t *w = calloc(1, sizeof(*w));
    if (!w) {
        return NULL;
    }
    snprintf(w->path, sizeof(w->path), "%s", path);
    w->width = width;
    w->height = height;
    w->fps = fps > 0 ? fps : 15;
    w->max_frames = max_frames;
    w->sizes = BIG_ALLOC(sizeof(uint32_t) * max_frames);
    w->keys = BIG_ALLOC(sizeof(uint32_t) * max_frames);
    w->f = fopen(path, "wb");
    if (!w->sizes || !w->keys || !w->f) {
        if (w->f) {
            fclose(w->f);
        }
        free(w->sizes);
        free(w->keys);
        free(w);
        return NULL;
    }
    static const uint8_t ftyp[] = {
        0, 0, 0, 32, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 2, 0,
        'i', 's', 'o', 'm', 'i', 's', 'o', '2', 'a', 'v', 'c', '1', 'm', 'p', '4', '1',
    };
    wr(w, ftyp, sizeof(ftyp));
    w->mdat_pos = w->pos;
    static const uint8_t mdat[8] = {0, 0, 0, 0, 'm', 'd', 'a', 't'};   /* size patched at the end */
    wr(w, mdat, sizeof(mdat));
    return w;
}

/* (documented in mp4_writer.h) */
uint32_t mp4w_size(const mp4w_t *w)
{
    return w ? w->pos : 0;
}

/**
 * @brief next NAL unit in an Annex-B buffer: returns its start (after the start
 * code) and length, or NULL at the end
 * @param[in] p  where to look from
 * @param[in] end  the buffer's end
 * @param[out] len  gets its length
 * @return its start, or NULL
 */
static const uint8_t *next_nal(const uint8_t *p, const uint8_t *end, size_t *len)
{
    while (p + 3 <= end && !(p[0] == 0 && p[1] == 0 && p[2] == 1)) {
        p++;
    }
    if (p + 3 > end) {
        return NULL;
    }
    const uint8_t *s = p + 3, *q = s;
    while (q + 3 <= end && !(q[0] == 0 && q[1] == 0 && (q[2] == 1 || (q[2] == 0 && q + 4 <= end && q[3] == 1)))) {
        q++;
    }
    if (q + 3 > end) {
        q = end;
    }
    *len = (size_t)(q - s);
    return s;
}

/* (documented in mp4_writer.h) */
bool mp4w_add_frame(mp4w_t *w, const uint8_t *annexb, size_t len, bool key)
{
    if (!w || w->failed || w->n >= w->max_frames) {
        printf("mp4w: frame %d refused (%s)\n", w ? w->n : -1, !w ? "no writer" : w->failed ? "write failed earlier" : "too many frames");
        return false;
    }
    const uint8_t *end = annexb + len, *p = annexb, *nal;
    size_t nlen;
    uint32_t sample = 0;
    while ((nal = next_nal(p, end, &nlen)) != NULL) {
        p = nal + nlen;
        if (!nlen) {
            continue;
        }
        int type = nal[0] & 0x1F;
        if (type == 7) {                                 /* SPS: into avcC (first one) */
            if (!w->sps_len && nlen <= sizeof(w->sps)) {
                memcpy(w->sps, nal, w->sps_len = (int)nlen);
            }
            continue;
        }
        if (type == 8) {                                 /* PPS */
            if (!w->pps_len && nlen <= sizeof(w->pps)) {
                memcpy(w->pps, nal, w->pps_len = (int)nlen);
            }
            continue;
        }
        if (type == 9) {
            continue;    /* access unit delimiter: not in MP4 */
        }
        uint8_t l[4];
        put32(l, (uint32_t)nlen);
        wr(w, l, 4);
        wr(w, nal, nlen);
        sample += 4 + (uint32_t)nlen;
    }
    if (!sample) {                                       /* nothing to show in it */
        printf("mp4w: frame %d: no picture data in %u bytes (first %02x %02x %02x %02x %02x)\n", w->n, (unsigned)len,
               len > 0 ? annexb[0] : 0, len > 1 ? annexb[1] : 0, len > 2 ? annexb[2] : 0, len > 3 ? annexb[3] : 0,
               len > 4 ? annexb[4] : 0);
        return false;
    }
    w->sizes[w->n++] = sample;
    if (key) {
        w->keys[w->nkeys++] = (uint32_t)w->n;
    }
    return !w->failed;
}

/**
 * @brief Frees the writer (not the file).
 * @param[in] w  it
 */
static void free_writer(mp4w_t *w)
{
    free(w->sizes);
    free(w->keys);
    free(w);
}

/* (documented in mp4_writer.h) */
void mp4w_abort(mp4w_t *w)
{
    if (!w) {
        return;
    }
    fclose(w->f);
    remove(w->path);
    free_writer(w);
}

/* (documented in mp4_writer.h) */
bool mp4w_close(mp4w_t *w)
{
    if (!w) {
        return false;
    }
    bool ok = !w->failed && w->n > 0 && w->sps_len > 3 && w->pps_len > 0;
    if (!ok) {
        printf("mp4w: can't finish: failed=%d frames=%d sps=%d pps=%d\n", w->failed, w->n, w->sps_len, w->pps_len);
    }
    if (ok) {
        /* mdat size */
        uint8_t sz[4];
        put32(sz, w->pos - w->mdat_pos);
        ok = fseek(w->f, (long)w->mdat_pos, SEEK_SET) == 0 && fwrite(sz, 1, 4, w->f) == 4 &&
             fseek(w->f, 0, SEEK_END) == 0;
    }
    buf_t b = {0};
    if (ok) {
        b.cap = 1024 + (size_t)w->n * 4 + (size_t)w->nkeys * 4 + w->sps_len + w->pps_len;
        b.p = BIG_ALLOC(b.cap);
        ok = b.p != NULL;
    }
    if (ok) {
        uint32_t ts = (uint32_t)w->fps * TIMESCALE_PER_FRAME;
        uint32_t dur = (uint32_t)w->n * TIMESCALE_PER_FRAME;          /* in track timescale */
        uint32_t dur_ms = (uint32_t)((uint64_t)w->n * 1000 / w->fps);

        size_t moov = b_box(&b, "moov");
        size_t mvhd = b_fullbox(&b, "mvhd", 0, 0);
        b_u32(&b, 0);
        b_u32(&b, 0);                     /* creation, modification */
        b_u32(&b, 1000);
        b_u32(&b, dur_ms);             /* timescale, duration */
        b_u32(&b, 0x00010000);
        b_u16(&b, 0x0100);       /* rate 1.0, volume 1.0 */
        b_zero(&b, 10);
        b_matrix(&b);
        b_zero(&b, 24);                                 /* pre_defined */
        b_u32(&b, 2);                                   /* next track ID */
        b_end(&b, mvhd);

        size_t trak = b_box(&b, "trak");
        size_t tkhd = b_fullbox(&b, "tkhd", 0, 3);      /* enabled, in movie */
        b_u32(&b, 0);
        b_u32(&b, 0);
        b_u32(&b, 1);
        b_u32(&b, 0);                     /* track ID, reserved */
        b_u32(&b, dur_ms);
        b_zero(&b, 8);
        b_u16(&b, 0);
        b_u16(&b, 0);
        b_u16(&b, 0);
        b_u16(&b, 0);   /* layer, group, volume, reserved */
        b_matrix(&b);
        b_u32(&b, (uint32_t)w->width << 16);
        b_u32(&b, (uint32_t)w->height << 16);
        b_end(&b, tkhd);

        size_t mdia = b_box(&b, "mdia");
        size_t mdhd = b_fullbox(&b, "mdhd", 0, 0);
        b_u32(&b, 0);
        b_u32(&b, 0);
        b_u32(&b, ts);
        b_u32(&b, dur);
        b_u16(&b, 0x55C4);
        b_u16(&b, 0);                /* language "und" */
        b_end(&b, mdhd);
        size_t hdlr = b_fullbox(&b, "hdlr", 0, 0);
        b_u32(&b, 0);
        b_put(&b, "vide", 4);
        b_zero(&b, 12);
        b_put(&b, "VideoHandler", 13);
        b_end(&b, hdlr);

        size_t minf = b_box(&b, "minf");
        size_t vmhd = b_fullbox(&b, "vmhd", 0, 1);
        b_zero(&b, 8);
        b_end(&b, vmhd);
        size_t dinf = b_box(&b, "dinf");
        size_t dref = b_fullbox(&b, "dref", 0, 0);
        b_u32(&b, 1);
        size_t url = b_fullbox(&b, "url ", 0, 1);       /* data in this file */
        b_end(&b, url);
        b_end(&b, dref);
        b_end(&b, dinf);

        size_t stbl = b_box(&b, "stbl");
        size_t stsd = b_fullbox(&b, "stsd", 0, 0);
        b_u32(&b, 1);
        size_t avc1 = b_box(&b, "avc1");
        b_zero(&b, 6);
        b_u16(&b, 1);                    /* data reference index */
        b_zero(&b, 16);
        b_u16(&b, (uint16_t)w->width);
        b_u16(&b, (uint16_t)w->height);
        b_u32(&b, 0x00480000);
        b_u32(&b, 0x00480000);   /* 72 dpi */
        b_u32(&b, 0);
        b_u16(&b, 1);                     /* frame count */
        b_zero(&b, 32);                                 /* compressor name */
        b_u16(&b, 0x0018);
        b_u16(&b, 0xFFFF);           /* depth, pre_defined */
        size_t avcc = b_box(&b, "avcC");
        b_u8(&b, 1);
        b_u8(&b, w->sps[1]);
        b_u8(&b, w->sps[2]);
        b_u8(&b, w->sps[3]);   /* profile, compat, level */
        b_u8(&b, 0xFF);                                 /* 4-byte NAL lengths */
        b_u8(&b, 0xE1);
        b_u16(&b, (uint16_t)w->sps_len);
        b_put(&b, w->sps, w->sps_len);
        b_u8(&b, 1);
        b_u16(&b, (uint16_t)w->pps_len);
        b_put(&b, w->pps, w->pps_len);
        b_end(&b, avcc);
        b_end(&b, avc1);
        b_end(&b, stsd);

        size_t stts = b_fullbox(&b, "stts", 0, 0);
        b_u32(&b, 1);
        b_u32(&b, (uint32_t)w->n);
        b_u32(&b, TIMESCALE_PER_FRAME);
        b_end(&b, stts);
        size_t stss = b_fullbox(&b, "stss", 0, 0);
        b_u32(&b, (uint32_t)w->nkeys);
        for (int i = 0; i < w->nkeys; i++) {
            b_u32(&b, w->keys[i]);
        }
        b_end(&b, stss);
        size_t stsc = b_fullbox(&b, "stsc", 0, 0);
        b_u32(&b, 1);
        b_u32(&b, 1);
        b_u32(&b, (uint32_t)w->n);
        b_u32(&b, 1);   /* one chunk */
        b_end(&b, stsc);
        size_t stsz = b_fullbox(&b, "stsz", 0, 0);
        b_u32(&b, 0);
        b_u32(&b, (uint32_t)w->n);
        for (int i = 0; i < w->n; i++) {
            b_u32(&b, w->sizes[i]);
        }
        b_end(&b, stsz);
        size_t stco = b_fullbox(&b, "stco", 0, 0);
        b_u32(&b, 1);
        b_u32(&b, w->mdat_pos + 8);       /* the chunk starts right after mdat's header */
        b_end(&b, stco);
        b_end(&b, stbl);
        b_end(&b, minf);
        b_end(&b, mdia);
        b_end(&b, trak);
        b_end(&b, moov);
        ok = !b.oom;
        if (ok) {
            wr(w, b.p, b.len);
        }
        ok = ok && !w->failed;
    }
    free(b.p);
    if (fclose(w->f) != 0) {
        ok = false;
    }
    if (!ok) {
        remove(w->path);
    }
    free_writer(w);
    return ok;
}
