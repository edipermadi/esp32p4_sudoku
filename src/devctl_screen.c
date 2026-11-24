/**
 * @file devctl_screen.c
 * @ingroup devctl
 * @brief shot, bench, screenoff: what the screen shows, how fast it redraws, turning it off.
 */
#include "devctl_internal.h"
#include <stdio.h>
#include <stdint.h>
#include <lvgl.h>
#include "esp_timer.h"
#include "bsp/bsp_board.h"
#include "ui_sudoku.h"

/* (documented in devctl_internal.h) */
void devctl_shot(bool z)
{
    lv_refr_now(NULL);                              /* everything drawn and flushed */
    const uint16_t *fb = bsp_display_frame();
    if (!fb) {
        serial_println("ERR shot: no frame buffer");
        return;
    }
    if (!devctl_z_begin('S', z)) {
        serial_println("ERR shot: no memory");
        return;
    }
    serial_printf("SHOT BEGIN %d %d%s\n", BSP_LCD_H_RES, BSP_LCD_V_RES, z ? " z" : "");
    const int total = BSP_LCD_H_RES * BSP_LCD_V_RES;
    uint8_t run3[3 * 256];                          /* (written a few hundred bytes at a time) */
    int k = 0;
    for (int i = 0; i < total;) {                   /* runs of up to 256 equal pixels */
        uint16_t px = fb[i];
        int run = 1;
        while (run < 256 && i + run < total && fb[i + run] == px) {
            run++;
        }
        run3[k++] = (uint8_t)(run - 1);
        run3[k++] = (uint8_t)(px & 0xFF);
        run3[k++] = (uint8_t)(px >> 8);
        if (k == sizeof(run3)) {
            devctl_z_write(run3, k);
            k = 0;
        }
        i += run;
    }
    devctl_z_write(run3, k);
    devctl_z_end();
    serial_printf("SHOT END %u %08x\n", (unsigned)devctl_z_bytes(), (unsigned)devctl_z_crc());
}

/* (documented in devctl_internal.h) */
void devctl_bench(int n, const int *a)
{
    int frames = n >= 1 && a[0] > 0 ? a[0] : 30;
    lv_obj_t *scr = lv_screen_active();
    lv_refr_now(NULL);                          /* nothing pending before the clock starts */
    uint32_t mx = 0, mn = UINT32_MAX;
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < frames; i++) {
        int64_t s = esp_timer_get_time();
        lv_obj_invalidate(scr);
        lv_refr_now(NULL);
        uint32_t d = (uint32_t)(esp_timer_get_time() - s);
        if (d > mx) {
            mx = d;
        }
        if (d < mn) {
            mn = d;
        }
    }
    uint32_t avg = (uint32_t)((esp_timer_get_time() - t0) / frames);
    serial_printf("BENCH %d frames: avg %u us, min %u us, max %u us (%u.%u fps)\n", frames, (unsigned)avg,
                  (unsigned)mn, (unsigned)mx, (unsigned)(10000000 / avg / 10), (unsigned)(10000000 / avg % 10));
}

/* (documented in devctl_internal.h) */
void devctl_screenoff(void)
{
    sudoku_ui_screen_off();                     /* as the timeout: black, then the lock screen if on */
    devctl_pump(200);
}

/**
 * @brief An average, us.
 * @param[in] sum  the sum, us
 * @param[in] n  how many
 * @return sum / n (0 for none)
 */
static unsigned avg_us(uint64_t sum, uint32_t n)
{
    return n ? (unsigned)(sum / n) : 0;
}

/* (documented in devctl_internal.h) */
void devctl_perf(int n, const int *a)
{
    uint32_t ms = n >= 1 && a[0] > 0 ? (uint32_t)(a[0] > 60000 ? 60000 : a[0]) : 5000;
    sudoku_ui_perf_t p;
    sudoku_ui_perf(ms, &p);
    uint32_t t = p.ms ? p.ms : 1;
    char who[16] = "none";
    if (p.saver >= 0) {
        snprintf(who, sizeof(who), "%d", p.saver);
    }
    unsigned busy = (unsigned)((p.tick_us + p.refresh_us) / 10 / t);     /* % of the time */
    serial_printf("PERF saver=%s %u ms: %u frames, %u.%u fps; tick avg %u us (calc %u, draw %u, flush %u), "
                  "max %u us; refresh %u x, avg %u us, max %u us; busy %u%%\n",
                  who, (unsigned)p.ms, (unsigned)p.frames, (unsigned)(p.frames * 1000u / t),
                  (unsigned)(p.frames * 10000u / t % 10), avg_us(p.tick_us, p.frames), avg_us(p.calc_us, p.frames),
                  avg_us(p.draw_us, p.frames), avg_us(p.flush_us, p.frames), (unsigned)p.tick_max_us,
                  (unsigned)p.refreshes, avg_us(p.refresh_us, p.refreshes), (unsigned)p.refresh_max_us, busy);
}

/* (documented in devctl_internal.h) */
bool devctl_saver(int mode)
{
    if (!sudoku_ui_screen_saver(mode)) {
        return false;
    }
    devctl_pump(200);
    return true;
}
