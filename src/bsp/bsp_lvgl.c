/*
 * LVGL v9 port: partial rendering into two PSRAM draw buffers, copied to the
 * DPI frame buffer by DMA2D. Flush completion is signalled from the DPI
 * "color transfer done" callback, as in ESP-IDF's mipi_dsi LVGL example.
 *
 * Rotation (lv_display_set_rotation): LVGL renders in the rotated coordinate
 * system; the flush callback turns each strip the right way round into a
 * spare buffer before it goes to the panel. Touch points are rotated by LVGL.
 */
#include "bsp_board.h"
#include <lvgl.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_mipi_dsi.h"

static const char *TAG = "bsp_lvgl";

#define DRAW_BUF_LINES 80

static void *s_rot_buf;                 /* rotated copy of the strip being flushed */

static uint32_t lvgl_tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

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

static bool color_trans_done_cb(esp_lcd_panel_handle_t panel,
                                esp_lcd_dpi_panel_event_data_t *edata, void *ctx)
{
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
}

static void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x, y;
    if (bsp_touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

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
    void *buf1 = heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
