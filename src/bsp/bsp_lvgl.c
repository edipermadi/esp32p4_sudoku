/**
 * @file bsp_lvgl.c
 * @ingroup bsp
 * @brief The LVGL port: draw buffers, the flush to the panel, rotation, the touch input.
 *
 * LVGL v9 port: partial rendering into two full-screen PSRAM draw buffers, copied
 * to the DPI frame buffer by DMA2D. Flush completion is signalled from the DPI
 * "color transfer done" callback, as in ESP-IDF's mipi_dsi LVGL example.
 *
 * Rotation (lv_display_set_rotation): LVGL renders in the rotated coordinate
 * system; the flush callback turns each strip the right way round into a
 * spare buffer before it goes to the panel. Touch points are rotated by LVGL.
 *
 */
#include "bsp_board.h"
#include <lvgl.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_cache.h"

static const char *TAG = "bsp_lvgl";    /**< the log's tag */

/* Draw buffers: lines per strip, and where (1 = internal SRAM, 0 = PSRAM). Overridable
 * with -D for measuring (devctl "bench"). The whole screen: LVGL walks the objects and
 * lays out the text once per strip, and on this board that costs more than the pixels.
 * Full-screen redraws measured (bench, PPA on): 80-line strips 30-64 ms per frame,
 * 240 lines 27-54 ms, the whole screen 26-48 ms (13-33 % less than 80 lines); smaller
 * strips in internal SRAM were slower still (20 lines: up to 106 ms) and take the
 * internal RAM MP4 export needs. Costs 2 x 1 MB of PSRAM (and the rotation buffer). */
#ifndef DRAW_BUF_LINES
#define DRAW_BUF_LINES BSP_LCD_V_RES    /**< lines per draw buffer strip (the whole screen) */
#endif
#ifndef DRAW_BUF_INTERNAL
#define DRAW_BUF_INTERNAL 0             /**< 1: the draw buffers in internal SRAM, 0: PSRAM */
#endif

static void *s_rot_buf;                 /**< rotated copy of the strip being flushed */

/**
 * @brief LVGL's tick.
 * @return ms since boot
 */
static uint32_t lvgl_tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief LVGL's flush: the strip into the frame buffer (rotated if the display is).
 * @param disp  the display
 * @param area  where
 * @param px_map  the pixels
 */
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    lv_area_t a = *area;
    lv_display_rotation_t rot = lv_display_get_rotation(disp);
    if (rot != LV_DISPLAY_ROTATION_0 && s_rot_buf) {
        /* one flush at a time (LVGL waits for flush_ready), so one spare buffer will do */
        lv_color_format_t cf = lv_display_get_color_format(disp);
        int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
        bool swap = rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270;
        lv_draw_rotate(px_map, s_rot_buf, w, h, lv_draw_buf_width_to_stride(w, cf),
                       lv_draw_buf_width_to_stride(swap ? h : w, cf), rot, cf);
        lv_display_rotate_area(disp, &a);
        px_map = s_rot_buf;
    }
    /* end coordinates are exclusive for esp_lcd */
    esp_lcd_panel_draw_bitmap(panel, a.x1, a.y1, a.x2 + 1, a.y2 + 1, px_map);
}

/**
 * @brief The DPI's "colour transfer done": the flush is ready.
 * @param panel  the panel
 * @param edata  unused
 * @param ctx  the display
 * @return false (no task to wake)
 */
static bool color_trans_done_cb(esp_lcd_panel_handle_t panel,
                                esp_lcd_dpi_panel_event_data_t *edata, void *ctx)
{
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
}

static volatile bool s_sim_on;          /**< a simulated touch drives the pointer (bsp_touch_sim()) */
static volatile bool s_sim_pressed;     /**< it is pressed */
static volatile uint16_t s_sim_x;       /**< where, px */
static volatile uint16_t s_sim_y;       /**< where, px */

/* (documented in bsp_board.h) */
void bsp_touch_sim(bool on, bool pressed, uint16_t x, uint16_t y)
{
    s_sim_x = x;
    s_sim_y = y;
    s_sim_pressed = pressed;
    s_sim_on = on;
}

/**
 * @brief LVGL's touch read: the panel, or the simulated touch.
 * @param indev  the input device
 * @param data  gets the point and state
 */
static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x, y;
    if (s_sim_on) {                                 /* serial control drives the pointer */
        data->point.x = s_sim_x;
        data->point.y = s_sim_y;
        data->state = s_sim_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    }
    if (bsp_touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* (documented in bsp_board.h) */
const uint16_t *bsp_display_frame(void)
{
    lv_display_t *disp = lv_display_get_default();
    esp_lcd_panel_handle_t panel = disp ? (esp_lcd_panel_handle_t)lv_display_get_user_data(disp) : NULL;
    void *fb = NULL;
    if (!panel || esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK || !fb) {
        return NULL;
    }
    /* DMA2D wrote it: drop whatever the CPU cache still holds of it */
    esp_cache_msync(fb, BSP_LCD_H_RES * BSP_LCD_V_RES * 2, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
    return (const uint16_t *)fb;
}

/* (documented in bsp_board.h) */
esp_err_t bsp_lvgl_start(void)
{
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(bsp_display_new(&panel), TAG, "display init failed");

    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);

    lv_display_t *disp = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    ESP_RETURN_ON_FALSE(disp, ESP_ERR_NO_MEM, TAG, "lv_display_create failed");
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(disp, panel);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    size_t buf_bytes = BSP_LCD_H_RES * DRAW_BUF_LINES * (BSP_LCD_BITS_PER_PIXEL / 8);
    const uint32_t caps = DRAW_BUF_INTERNAL ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)
                          : (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf1 = heap_caps_aligned_alloc(64, buf_bytes, caps);
    void *buf2 = heap_caps_aligned_alloc(64, buf_bytes, caps);
    ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "draw buffer alloc failed (PSRAM enabled?)");
    /* same size as a draw buffer: a strip has the same pixel count either way round */
    s_rot_buf = heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = color_trans_done_cb,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, disp),
                        TAG, "register DPI callbacks failed");

    if (bsp_touch_init() == ESP_OK) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, disp);
        lv_indev_set_read_cb(indev, lvgl_touch_cb);
    } else {
        ESP_LOGW(TAG, "continuing without touch");
    }

    bsp_display_brightness_set(100);
    return ESP_OK;
}
