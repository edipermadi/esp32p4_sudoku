/**
 * @file ui_saver_pic.c
 * @ingroup ui_sudoku
 * @brief The screen savers' picture (Life, Rain): a full-screen RGB565 image in PSRAM the saver paints
 *        into, flushed from the CPU's cache before it is redrawn (LVGL's PPA draw unit reads it by DMA).
 */
#include "ui_internal.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"

#define PIC_ROW_BYTES   (SCR_W * 2)             /**< a row of the picture, bytes */
#define PIC_BYTES       (SCR_W * SCR_W * 2)     /**< the picture, bytes */

static uint16_t *pc_buf;                  /**< the picture (RGB565, 64-byte aligned, PSRAM) */
static lv_image_dsc_t pc_dsc;             /**< pc_buf as an image */
static lv_obj_t *pc_img;                  /**< the image on the saver's cover */

/* (documented in ui_internal.h) */
uint16_t *saver_pic_start(lv_obj_t *parent)
{
    if (!pc_buf) {
        pc_buf = heap_caps_aligned_alloc(64, PIC_BYTES, MALLOC_CAP_SPIRAM);
    }
    if (!pc_buf) {
        return NULL;
    }
    memset(pc_buf, 0, PIC_BYTES);                   /* black */
    esp_cache_msync(pc_buf, PIC_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    memset(&pc_dsc, 0, sizeof(pc_dsc));
    pc_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    pc_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    pc_dsc.header.w = SCR_W;
    pc_dsc.header.h = SCR_W;
    pc_dsc.header.stride = PIC_ROW_BYTES;
    pc_dsc.data = (const uint8_t *)pc_buf;
    pc_dsc.data_size = PIC_BYTES;
    pc_img = lv_image_create(parent);
    lv_obj_set_pos(pc_img, 0, 0);
    lv_obj_set_clickable(pc_img, false);
    lv_image_set_src(pc_img, &pc_dsc);
    return pc_buf;
}

/* (documented in ui_internal.h) */
void saver_pic_flush(int32_t y0, int32_t y1)
{
    if (!pc_buf || y1 < y0) {
        return;
    }
    size_t from = (size_t)y0 * PIC_ROW_BYTES, to = (size_t)(y1 + 1) * PIC_ROW_BYTES;
    from &= ~(size_t)63;                            /* (the cache works in 64-byte lines) */
    to = LV_MIN((to + 63) & ~(size_t)63, (size_t)PIC_BYTES);
    esp_cache_msync((uint8_t *)pc_buf + from, to - from, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

/* (documented in ui_internal.h) */
void saver_pic_redraw(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (pc_img) {
        lv_area_t a = {x0, y0, x1, y1};
        lv_obj_invalidate_area(pc_img, &a);
    }
}

/* (documented in ui_internal.h) */
void saver_pic_stop(void)
{
    if (pc_img) {
        lv_obj_delete(pc_img);                      /* (before its picture goes) */
        pc_img = NULL;
    }
    heap_caps_free(pc_buf);
    pc_buf = NULL;
}
