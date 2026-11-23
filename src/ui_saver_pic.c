/**
 * @file ui_saver_pic.c
 * @ingroup ui_sudoku
 * @brief The animated screen savers' picture: a full-screen RGB565 image in PSRAM the saver paints into,
 *        flushed from the CPU's cache before it is redrawn (LVGL's PPA draw unit reads it by DMA); and a
 *        pen for drawing on it (pixels, squares, lines, round dots) that redraws only what it touched.
 *
 * The pen cuts the screen into PEN_TILES_X x PEN_TILES_Y tiles (30, within LVGL's 32 areas); a frame
 * (saver_pen_begin() .. saver_pen_end()) redraws in each tile just the box its changed pixels fill, so
 * drawing bunched in one place redraws little.
 */
#include "ui_internal.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"

#define PIC_ROW_BYTES   (SCR_W * 2)             /**< a row of the picture, bytes */
#define PIC_BYTES       (SCR_W * SCR_W * 2)     /**< the picture, bytes */
#define PEN_TILES_X     6                       /**< the pen's tiles across */
#define PEN_TILES_Y     5                       /**< down */
#define PEN_TILES       (PEN_TILES_X * PEN_TILES_Y) /**< tiles */
#define PEN_TILE_W      (SCR_W / PEN_TILES_X)   /**< a tile's width, px */
#define PEN_TILE_H      (SCR_W / PEN_TILES_Y)   /**< a tile's height, px */

static uint16_t *pc_buf;                  /**< the picture (RGB565, 64-byte aligned, PSRAM) */
static lv_image_dsc_t pc_dsc;             /**< pc_buf as an image */
static lv_obj_t *pc_img;                  /**< the image on the saver's cover */
static void widen(int t, int x0, int y0, int x1, int y1);

static int pc_box[PEN_TILES][4];          /**< each tile's changed box this frame: x0, y0, x1, y1 (x1 < 0: none) */

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
uint16_t saver_pic_mix(uint32_t a, uint32_t b, int u)
{
    lv_color_t p = lv_color_hex(a), q = lv_color_hex(b);
    u = u < 0 ? 0 : u > 255 ? 255 : u;
    return lv_color_to_u16(lv_color_make((uint8_t)(p.red + (q.red - p.red) * u / 255),
                                         (uint8_t)(p.green + (q.green - p.green) * u / 255),
                                         (uint8_t)(p.blue + (q.blue - p.blue) * u / 255)));
}

/* (documented in ui_internal.h) */
void saver_pen_begin(void)
{
    for (int k = 0; k < PEN_TILES; k++) {
        pc_box[k][2] = -1;
    }
}

/* (documented in ui_internal.h) */
void saver_pen_put(int x, int y, uint16_t c)
{
    if (!pc_buf || (unsigned)x >= SCR_W || (unsigned)y >= SCR_W) {
        return;
    }
    pc_buf[y * SCR_W + x] = c;
    widen((y / PEN_TILE_H) * PEN_TILES_X + x / PEN_TILE_W, x, y, x, y);
}

/**
 * @brief Widens a tile's changed box to take in a rectangle (inside the tile).
 * @param[in] t  the tile
 * @param[in] x0  left, px
 * @param[in] y0  top, px
 * @param[in] x1  right, px (included)
 * @param[in] y1  bottom, px (included)
 */
static void widen(int t, int x0, int y0, int x1, int y1)
{
    int *b = pc_box[t];
    if (b[2] < 0) {
        b[0] = x0;
        b[1] = y0;
        b[2] = x1;
        b[3] = y1;
        return;
    }
    b[0] = LV_MIN(b[0], x0);
    b[1] = LV_MIN(b[1], y0);
    b[2] = LV_MAX(b[2], x1);
    b[3] = LV_MAX(b[3], y1);
}

/* (documented in ui_internal.h) */
void saver_pen_square(int x, int y, int size, uint16_t c)
{
    int x0 = LV_MAX(x, 0), y0 = LV_MAX(y, 0);
    int x1 = LV_MIN(x + size - 1, SCR_W - 1), y1 = LV_MIN(y + size - 1, SCR_W - 1);
    if (!pc_buf || x1 < x0 || y1 < y0) {
        return;
    }
    for (int yy = y0; yy <= y1; yy++) {             /* (row by row, not pixel by pixel: it is drawn a lot) */
        uint16_t *p = pc_buf + yy * SCR_W;
        for (int xx = x0; xx <= x1; xx++) {
            p[xx] = c;
        }
    }
    for (int ty = y0 / PEN_TILE_H; ty <= y1 / PEN_TILE_H; ty++) {   /* (the tiles it lies on: 1 to 4) */
        for (int tx = x0 / PEN_TILE_W; tx <= x1 / PEN_TILE_W; tx++) {
            widen(ty * PEN_TILES_X + tx, LV_MAX(x0, tx * PEN_TILE_W), LV_MAX(y0, ty * PEN_TILE_H),
                  LV_MIN(x1, (tx + 1) * PEN_TILE_W - 1), LV_MIN(y1, (ty + 1) * PEN_TILE_H - 1));
        }
    }
}

/* (documented in ui_internal.h) */
void saver_pen_dot(int col, int row, int px, uint16_t c)
{
    int x0 = col * px, y0 = row * px, n = px - 1;   /* (the last column and row: the black gap) */
    for (int r = 0; r < n; r++) {
        int edge = r == 0 || r == n - 1;            /* (the top and bottom rows: corners off, round) */
        for (int k = edge; k < n - edge; k++) {
            saver_pen_put(x0 + k, y0 + r, c);
        }
    }
}

/* (documented in ui_internal.h) */
void saver_pen_line(int x0, int y0, int x1, int y1, int w, uint16_t c)
{
    const int o = (w - 1) / 2;                      /* (the square centred on the line) */
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {                                      /* (Bresenham, a w x w square a step) */
        saver_pen_square(x0 - o, y0 - o, w, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* (documented in ui_internal.h) */
void saver_pen_end(void)
{
    int y0 = SCR_W, y1 = -1;
    for (int k = 0; k < PEN_TILES; k++) {
        if (pc_box[k][2] >= 0) {
            y0 = LV_MIN(y0, pc_box[k][1]);
            y1 = LV_MAX(y1, pc_box[k][3]);
        }
    }
    if (y1 < 0) {
        return;
    }
    saver_pic_flush(y0, y1);
    for (int k = 0; k < PEN_TILES; k++) {
        if (pc_box[k][2] >= 0) {
            saver_pic_redraw(pc_box[k][0], pc_box[k][1], pc_box[k][2], pc_box[k][3]);
        }
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
