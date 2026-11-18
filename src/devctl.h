#pragma once
/*
 * Serial control for documentation and testing: commands on the USB-UART
 * port (Serial, a line each) take screenshots and play touches, so screens
 * can be captured by a script (tools/devctl.py). Runs in the LVGL task.
 *
 *   ping                      -> "OK pong"
 *   baud <n>                  -> "OK baud <n>", then the port runs at n baud
 *   tap <x> <y>               a short tap (pressed 120 ms)
 *   long <x> <y> [ms]         a long press (default 1000 ms)
 *   swipe <x1> <y1> <x2> <y2> [ms]   a drag (default 300 ms)
 *   press <x> <y> / move <x> <y> / release    by hand
 *   wait <ms>                 let the UI run (animations, the database)
 *   shot                      the screen, as lines (below)
 *
 * Every command answers "OK <command>" (or "ERR <why>") once done.
 *
 * A screenshot is the panel's frame buffer (what the screen shows),
 * 720 x 720 RGB565 (little-endian), run-length coded: runs of 1..256 equal
 * pixels as [count - 1][pixel low][pixel high]. It comes as text lines, so
 * log lines from other tasks can be told apart:
 *   SHOT BEGIN <width> <height>
 *   S <n> <base64 of up to 96 bytes>      n = 0, 1, 2, ...
 *   SHOT END <bytes> <crc32 of all bytes, hex>
 */
#ifdef __cplusplus
extern "C" {
#endif

/* Call from loop() (the LVGL task): reads and runs any commands waiting. */
void devctl_poll(void);

#ifdef __cplusplus
}
#endif
