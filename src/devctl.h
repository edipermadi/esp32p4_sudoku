#pragma once
/*
 * Serial control for documentation and testing: commands on the USB-UART
 * port (Serial, a line each) take screenshots and play touches, so screens
 * can be captured by a script (tools/devctl.py). Runs in the LVGL task.
 * The full protocol: docs/devctl-protocol.md (keep the two in step).
 *
 *   ping                      -> "OK pong"
 *   baud <n>                  -> "OK baud <n>", then the port runs at n baud
 *   tap <x> <y>               a short tap (pressed 120 ms)
 *   long <x> <y> [ms]         a long press (default 1000 ms)
 *   swipe <x1> <y1> <x2> <y2> [ms]   a drag (default 300 ms)
 *   press <x> <y> / move <x> <y> / release    by hand
 *   wait <ms>                 let the UI run (animations, the database)
 *   screenoff                 turn the screen off as its timeout does (the next tap wakes it)
 *   heap [1]                  -> "HEAP internal <free>, largest <n>, ...; dma ...; psram ..." (1: per region too)
 *   shot                      the screen, as lines (below)
 *
 * Files on the microSD card (paths from its root, %XX for spaces):
 *   ls [dir]                  -> "L d 0 <name>" / "L f <size> <name>" lines
 *   get <path>                -> "FILE BEGIN <size>", "F <n> <base64>" lines, "FILE END <bytes> <crc32>"
 *   put <path> <size> <crc32> -> "READY"; then "D <base64 of up to 96 bytes>" lines, with
 *                                "ACK <bytes>" after every 12 (send the next 12 then); written
 *                                to <path>.part, renamed to <path> once the CRC matches
 *   mv <from> <to>, rm <path> (a file or an empty folder), mkdir <path>
 * sudoku.db is open: it can be read, not replaced, renamed or deleted.
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
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call from loop() (the LVGL task): reads and runs any commands waiting. */
void devctl_poll(void);

/* Runs one command line from elsewhere (gibberlink.h: commands as sound), in the
 * LVGL task: touches, wait (at most 10 s), ping, heap (short), screenoff; not
 * shot, files, baud or bench. The answer ("OK ..." / "ERR ...", one line) goes
 * into out, not to Serial. true: OK. */
bool devctl_run_line(const char *line, char *out, size_t n);

#ifdef __cplusplus
}
#endif
