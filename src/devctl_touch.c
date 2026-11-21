/**
 * @file devctl_touch.c
 * @ingroup devctl
 * @brief tap, long, swipe, press, move, release: simulated touches on the screen.
 */
#include "devctl_internal.h"
#include <string.h>
#include "bsp/bsp_board.h"

#define TAP_MS       120                            /**< a tap: how long it's pressed */

/**
 * @brief The simulated touch: down (or up) at a point, kept on the screen.
 * @param pressed  down
 * @param x  pixels from the left
 * @param y  pixels from the top
 */
static void touch(bool pressed, int x, int y)
{
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x >= BSP_LCD_H_RES) {
        x = BSP_LCD_H_RES - 1;
    }
    if (y >= BSP_LCD_V_RES) {
        y = BSP_LCD_V_RES - 1;
    }
    bsp_touch_sim(true, pressed, (uint16_t)x, (uint16_t)y);
}

/**
 * @brief Lets go at a point, lets the UI react, then gives the panel back its touches.
 * @param x  pixels from the left
 * @param y  pixels from the top
 */
static void touch_done(int x, int y)
{
    touch(false, x, y);
    devctl_pump(SETTLE_MS);
    bsp_touch_sim(false, false, 0, 0);              /* the panel again */
}

/**
 * @brief A drag from one point to another, in steps of about 20 ms.
 * @param x1  from, left
 * @param y1  from, top
 * @param x2  to, left
 * @param y2  to, top
 * @param ms  how long it takes
 */
static void swipe(int x1, int y1, int x2, int y2, uint32_t ms)
{
    const int steps = ms / 20 > 2 ? (int)(ms / 20) : 2;
    touch(true, x1, y1);
    devctl_pump(40);
    for (int i = 1; i <= steps; i++) {
        touch(true, x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps);
        devctl_pump(ms / steps);
    }
    touch_done(x2, y2);
}

/* (documented in devctl_internal.h) */
bool devctl_touch_cmd(const char *cmd, const int *a, int n)
{
    if (!strcmp(cmd, "tap") && n >= 2) {
        touch(true, a[0], a[1]);
        devctl_pump(TAP_MS);
        touch_done(a[0], a[1]);
    } else if (!strcmp(cmd, "long") && n >= 2) {
        touch(true, a[0], a[1]);
        devctl_pump(n >= 3 && a[2] > 0 ? (uint32_t)a[2] : 1000);
        touch_done(a[0], a[1]);
    } else if (!strcmp(cmd, "swipe") && n >= 4) {
        swipe(a[0], a[1], a[2], a[3], n >= 5 && a[4] > 0 ? (uint32_t)a[4] : 300);
    } else if (!strcmp(cmd, "press") && n >= 2) {
        touch(true, a[0], a[1]);
        devctl_pump(40);
    } else if (!strcmp(cmd, "move") && n >= 2) {
        touch(true, a[0], a[1]);
        devctl_pump(40);
    } else if (!strcmp(cmd, "release")) {
        touch_done(n >= 2 ? a[0] : 0, n >= 2 ? a[1] : 0);
    } else {
        return false;
    }
    return true;
}
