/**
 * @file media_jpeg.c
 * @ingroup media
 * @brief The viewer's pictures: the file read whole, decoded by the JPEG engine to RGB565 at full size, then
 *        scaled down (nearest pixel) to fit the screen.
 */
#include "media_core.h"
#include "media_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include "driver/jpeg_decode.h"

#define SCR        MEDIA_SCR            /**< the screen's size, px */
#define JPEG_MAX_FILE  (16u * 1024 * 1024)  /**< the largest JPEG file read */
#define JPEG_MAX_OUT   (24u * 1024 * 1024)       /**< decoded RGB565 at full size */

/* (documented in media_internal.h) */
void media_jpeg_run(const char *path)
{
    uint8_t *in = NULL;
    uint16_t *out = NULL;
    jpeg_decoder_handle_t dec = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) {
        media_set_msg("Couldn't open the file.");
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || (unsigned long)size > JPEG_MAX_FILE) {
        media_set_msg(size <= 0 ? "The file is empty." : "The picture is too big to show (over 16 MB).");
        goto done;
    }
    jpeg_decode_memory_alloc_cfg_t icfg = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    size_t in_cap = 0;
    in = jpeg_alloc_decoder_mem((size_t)size, &icfg, &in_cap);
    if (!in) {
        media_set_msg("Out of memory.");
        goto done;
    }
    if (fread(in, 1, (size_t)size, f) != (size_t)size) {
        media_set_msg("Couldn't read the file.");
        goto done;
    }
    fclose(f);
    f = NULL;
    if (is_media_stopping()) {
        goto done;
    }

    jpeg_decode_picture_info_t info;
    if (jpeg_decoder_get_info(in, (uint32_t)size, &info) != ESP_OK || !info.width || !info.height) {
        media_set_msg("This JPEG can't be read.");
        goto done;
    }
    /* the engine writes whole blocks: 8 or 16 pixels, by the chroma sampling */
    int wb = (info.sample_method == JPEG_DOWN_SAMPLING_YUV444 || info.sample_method == JPEG_DOWN_SAMPLING_GRAY) ? 8 : 16;
    int hb = info.sample_method == JPEG_DOWN_SAMPLING_YUV420 ? 16 : 8;
    uint32_t aw = (info.width + wb - 1) / wb * wb, ah = (info.height + hb - 1) / hb * hb;
    if ((uint64_t)aw * ah * 2 > JPEG_MAX_OUT) {
        media_set_msg("The picture is too big to show (%ux%u).", (unsigned)info.width, (unsigned)info.height);
        goto done;
    }
    jpeg_decode_memory_alloc_cfg_t ocfg = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    size_t out_cap = 0;
    out = jpeg_alloc_decoder_mem((size_t)aw * ah * 2, &ocfg, &out_cap);
    jpeg_decode_engine_cfg_t ecfg = {.timeout_ms = 2000};
    if (!out || jpeg_new_decoder_engine(&ecfg, &dec) != ESP_OK) {
        dec = NULL;
        media_set_msg("Out of memory.");
        goto done;
    }
    jpeg_decode_cfg_t cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,   /* little-endian RGB565, as LVGL's */
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t got = 0;
    esp_err_t err = jpeg_decoder_process(dec, &cfg, in, (uint32_t)size, (uint8_t *)out, (uint32_t)out_cap, &got);
    if (err != ESP_OK) {
        media_set_msg("This JPEG can't be decoded here (a progressive JPEG?).");
        goto done;
    }
    if (got >= 2 * ah) {                           /* the row length the engine used */
        uint32_t stride = got / 2 / ah;
        if (stride >= info.width) {
            aw = stride;
        }
    }
    int dw, dh;
    media_fit((int)info.width, (int)info.height, &dw, &dh);
    if (!media_wait_free()) {
        goto done;
    }
    uint16_t *dst = media_back_buf();
    media_make_xmap((int)info.width, dw);
    for (int y = 0; y < dh; y++) {
        const uint16_t *row = out + (size_t)media_src_row(y, (int)info.height, dh) * aw;
        uint16_t *o = dst + (size_t)y * dw;
        if (dw == (int)info.width) {
            memcpy(o, row, (size_t)dw * 2);
        } else {
            for (int x = 0; x < dw; x++) {
                o[x] = row[media_xmap[x]];
            }
        }
    }
    media_publish(dw, dh);
done:
    if (f) {
        fclose(f);
    }
    if (dec) {
        jpeg_del_decoder_engine(dec);
    }
    if (out) {
        heap_caps_free(out);
    }
    if (in) {
        heap_caps_free(in);
    }
}
