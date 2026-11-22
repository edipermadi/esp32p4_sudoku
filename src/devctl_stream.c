/**
 * @file devctl_stream.c
 * @ingroup devctl
 * @brief Data to and from the host: the numbered base64 lines (with a CRC), compression (the ROM's
 *        deflate), base64 in, and data lines read from the host.
 */
#include "devctl_internal.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp32-hal.h"
#include <miniz.h>                      /* (the ESP32-P4 ROM's: deflate and inflate) */


static uint32_t s_crc;                              /**< the data's CRC-32 so far */
static uint8_t s_chunk[CHUNK];                      /**< bytes per data line (128 base64 characters) */
static int s_chunk_n;                               /**< its bytes */
static int s_seq;                                   /**< the next line's number */
static uint32_t s_bytes;                            /**< the data's size so far */

/* (documented in devctl_internal.h) */
uint32_t devctl_crc32(uint32_t crc, const uint8_t *p, int n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static char s_prefix = 'S';                         /**< the lines' letter: S a screenshot, F a file, Q SQL's result */

/**
 * @brief Sends the bytes gathered as one numbered base64 line ("S \<n\> ...", "F \<n\> ...").
 */
static void flush_chunk(void)
{
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!s_chunk_n) {
        return;
    }
    char out[16 + CHUNK / 3 * 4 + 8];
    int o = snprintf(out, sizeof(out), "%c %d ", s_prefix, s_seq++);
    for (int i = 0; i < s_chunk_n; i += 3) {
        uint32_t v = (uint32_t)s_chunk[i] << 16;
        if (i + 1 < s_chunk_n) {
            v |= (uint32_t)s_chunk[i + 1] << 8;
        }
        if (i + 2 < s_chunk_n) {
            v |= s_chunk[i + 2];
        }
        out[o++] = B64[v >> 18 & 63];
        out[o++] = B64[v >> 12 & 63];
        out[o++] = i + 1 < s_chunk_n ? B64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < s_chunk_n ? B64[v & 63] : '=';
    }
    out[o++] = '\n';
    serial_write((const uint8_t *)out, (size_t)o);         /* one write: a whole line */
    s_chunk_n = 0;
}

/**
 * @brief One byte onto the line being gathered (sent when it's full).
 * @param[in] b  the byte
 */
static void put(uint8_t b)
{
    s_chunk[s_chunk_n++] = b;
    if (s_chunk_n == CHUNK) {
        flush_chunk();
    }
}

/* The data a command sends (a screenshot, a file, SQL's result): z_begin(), z_write() the
 * bytes, z_end(). With z, they're compressed first (zlib format: the ROM's deflate), then
 * the base64 lines; s_bytes / s_crc are always the data's own (uncompressed) size and CRC. */
static tdefl_compressor *s_def;                     /**< the compressor (PSRAM, made on first use: ~150 KB) */
static bool s_zon;                                  /**< the data being sent is compressed */

/**
 * @brief The compressor's output, onto the base64 lines.
 * @param[in] p  the compressed bytes
 * @param[in] n  how many
 * @param[in] user  unused
 * @return MZ_TRUE (go on)
 */
static mz_bool z_sink(const void *p, int n, void *user)
{
    const uint8_t *b = (const uint8_t *)p;
    for (int i = 0; i < n; i++) {
        put(b[i]);
    }
    return MZ_TRUE;
}

/* (documented in devctl_internal.h) */
bool devctl_z_begin(char prefix, bool z)
{
    s_prefix = prefix;
    s_crc = 0;
    s_chunk_n = s_seq = 0;
    s_bytes = 0;
    s_zon = false;
    if (!z) {
        return true;
    }
    if (!s_def && !(s_def = (tdefl_compressor *)heap_caps_malloc(sizeof(tdefl_compressor), MALLOC_CAP_SPIRAM))) {
        return false;
    }
    if (tdefl_init(s_def, z_sink, NULL, TDEFL_WRITE_ZLIB_HEADER | 32) != TDEFL_STATUS_OKAY) {   /* 32 probes */
        return false;
    }
    s_zon = true;
    return true;
}

/* (documented in devctl_internal.h) */
void devctl_z_write(const void *p, size_t n)
{
    s_crc = devctl_crc32(s_crc, (const uint8_t *)p, (int)n);
    s_bytes += n;
    if (s_zon) {
        tdefl_compress_buffer(s_def, p, n, TDEFL_NO_FLUSH);
    } else {
        for (size_t i = 0; i < n; i++) {
            put(((const uint8_t *)p)[i]);
        }
    }
}

/* (documented in devctl_internal.h) */
void devctl_z_end(void)
{
    if (s_zon) {
        tdefl_compress_buffer(s_def, NULL, 0, TDEFL_FINISH);
    }
    flush_chunk();
}

/**
 * @brief A base64 character's value.
 * @param[in] c  the character
 * @return 0..63, or -1 if it isn't one
 */
static int b64val(char c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
           : c == '+' ? 62 : c == '/' ? 63 : -1;
}

/* (documented in devctl_internal.h) */
int devctl_b64_decode(const char *s, uint8_t *out, int max)
{
    int n = 0, bits = 0;
    uint32_t acc = 0;
    for (; *s && *s != '='; s++) {
        int v = b64val(*s);
        if (v < 0) {
            return -1;
        }
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= max) {
                return -1;
            }
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return n;
}

/* (documented in devctl_internal.h) */
bool devctl_read_line(char *buf, int max, uint32_t ms)
{
    int len = 0;
    uint32_t start = millis();
    while (millis() - start < ms) {
        int c = serial_read();
        if (c < 0) {
            delay(1);
            continue;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            buf[len] = 0;
            if (len) {
                return true;
            }
            continue;
        }
        if (len < max - 1) {
            buf[len++] = (char)c;
        }
    }
    return false;
}

/* (documented in devctl_internal.h) */
uint32_t devctl_z_bytes(void)
{
    return s_bytes;
}

/* (documented in devctl_internal.h) */
uint32_t devctl_z_crc(void)
{
    return s_crc;
}
