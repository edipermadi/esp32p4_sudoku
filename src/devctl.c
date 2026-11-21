/**
 * @file devctl.c
 * @ingroup devctl
 * @brief Serial control: reading command lines from the port, answering, and dispatching them (the
 *        commands themselves: devctl_internal.h; the protocol: devctl.h, docs/devctl-protocol.md).
 */
#include "devctl_internal.h"
#include "esp32-hal.h"                 /* delay() (the Arduino core's C API) */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <lvgl.h>
#include "esp_heap_caps.h"
#include "ui_sudoku.h"

/** The line being read (CMD_MAX bytes, PSRAM: internal RAM is tight) */
static char *s_line;
static int s_len;                                   /**< its length so far */
static char *s_cap;                                 /**< devctl_run_line()'s answer goes here (NULL: to the port) */
static size_t s_cap_n;                              /**< its size */

/* (documented in devctl_internal.h) */
void devctl_reply(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (s_cap) {
        vsnprintf(s_cap, s_cap_n, fmt, ap);
        size_t l = strlen(s_cap);
        while (l && (s_cap[l - 1] == '\n' || s_cap[l - 1] == '\r')) {
            s_cap[--l] = 0;
        }
    } else {
        char buf[200];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        serial_print(buf);
    }
    va_end(ap);
}

/* (documented in devctl_internal.h) */
void devctl_pump(uint32_t ms)
{
    uint32_t end = millis() + ms;
    do {
        lv_timer_handler();
        delay(5);
    } while ((int32_t)(end - millis()) > 0);
}

/* (documented in devctl_internal.h) */
bool devctl_is_sound(void)
{
    return s_cap != NULL;
}

/* (documented in devctl_internal.h) */
char *devctl_line_buf(void)
{
    return s_line;
}

/**
 * @brief Runs a command line (from the port, or from devctl_run_line()).
 * @param line  the command line
 */
static void run(char *line)
{
    if (!s_cap && devctl_files_cmd(line)) {
        return;    /* (files: serial only) */
    }
    char cmd[16] = "";
    int a[5] = {0, 0, 0, 0, 0};
    int n = sscanf(line, "%15s %d %d %d %d %d", cmd, &a[0], &a[1], &a[2], &a[3], &a[4]) - 1;
    if (n < 0) {
        return;
    }
    if (!strcmp(cmd, "ping")) {
        devctl_reply("OK pong\n");
        return;
    }
    if (s_cap) {                                    /* over sound: touches, wait, heap, screenoff only */
        static const char *const ok[] = {"tap", "long", "swipe", "press", "move", "release", "wait", "heap", "screenoff",
                                         "fill", "state", "pz"
                                        };
        bool allowed = false;
        for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
            allowed |= !strcmp(cmd, ok[i]);
        }
        if (!allowed) {
            devctl_reply("ERR not over sound: %s\n", cmd);
            return;
        }
        if (!strcmp(cmd, "wait") && a[0] > 10000) {
            a[0] = 10000;
        }
        if (!strcmp(cmd, "heap")) {                 /* short: a reply is ~16 bytes a second */
            devctl_reply("OK heap i=%u d=%u p=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            return;
        }
    }
    if (!strcmp(cmd, "screen")) {                  /* where the board is: home1, game+dialog, ... */
        char name[48];
        sudoku_ui_screen_name(name, sizeof(name));
        devctl_reply("OK screen %s\n", name);
        return;
    }
    if (!strcmp(cmd, "clock")) {                   /* Wi-Fi connected? the time set? */
        devctl_clock();
        return;
    }
    if (!strcmp(cmd, "state")) {                   /* the game on screen, as text */
        devctl_state();
        return;
    }
    if (!strcmp(cmd, "pz")) {                      /* pz <id> <i>/<n> <part>: a puzzle sent by sound (ui_sudoku.c) */
        char o[96];
        sudoku_ui_puzzle_part(line + 2, o, sizeof(o));
        devctl_reply("%s\n", o);
        return;
    }
    if (!strcmp(cmd, "sql")) {                     /* sql <base64>: SQL on sudoku.db (serial only: not in ok[] above) */
        devctl_sql(line);
        return;
    }
    if (!strcmp(cmd, "genbench")) {                /* genbench <variant|all> [level|all] [n] */
        devctl_genbench(line);
        return;
    }
    if (!strcmp(cmd, "fill")) {                    /* fill <cell> <digit> ...: any separators, e.g. (12,5),(13,7) */
        devctl_fill(line);
        return;
    }
    if (!strcmp(cmd, "baud") && n >= 1 && a[0] >= 9600) {
        serial_printf("OK baud %d\n", a[0]);
        serial_flush();
        delay(20);
        serial_set_baud((uint32_t)a[0]);
        return;
    }
    if (devctl_touch_cmd(cmd, a, n)) {
        /* (tap, long, swipe, press, move, release: devctl_touch.c) */
    } else if (!strcmp(cmd, "wait") && n >= 1) {
        devctl_pump(a[0] > 0 ? (uint32_t)a[0] : 0);
    } else if (!strcmp(cmd, "shot")) {
        char zw[4] = "";
        devctl_shot(sscanf(line, "%*s %3s", zw) == 1 && !strcmp(zw, "z"));
    } else if (!strcmp(cmd, "heap")) {           /* free memory: internal (and its DMA part), PSRAM */
        serial_printf("HEAP internal %u free, largest %u, min ever %u; dma %u, largest %u; psram %u free, largest %u\n",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        if (n >= 1 && a[0]) {
            heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);    /* "heap 1": per region */
        }
    } else if (!strcmp(cmd, "bench")) {          /* N full-screen redraws of the screen shown */
        devctl_bench(n, a);
    } else if (!strcmp(cmd, "screenoff")) {
        devctl_screenoff();
    } else {
        devctl_reply("ERR unknown: %s\n", line);
        return;
    }
    devctl_reply("OK %s\n", cmd);
}

/* (documented in devctl.h) */
bool devctl_run_line(const char *line, char *out, size_t n)
{
    char buf[DEVCTL_LINE_MAX];
    strlcpy(buf, line, sizeof(buf));
    out[0] = 0;
    s_cap = out;
    s_cap_n = n;
    run(buf);
    s_cap = NULL;
    if (!out[0]) {
        strlcpy(out, "ERR empty", n);
    }
    return !strncmp(out, "OK", 2);
}

/* (documented in devctl.h) */
void devctl_poll(void)
{
    if (!s_line && !(s_line = (char *)heap_caps_malloc(CMD_MAX, MALLOC_CAP_SPIRAM))) {
        return;
    }
    while (serial_available()) {
        int c = serial_read();
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            s_line[s_len] = 0;
            if (s_len) {
                run(s_line);
            }
            s_len = 0;
        } else if (c >= ' ' && c < 127 && s_len < CMD_MAX - 1) {    /* (line noise from a reset: dropped) */
            s_line[s_len++] = (char)c;
        }
    }
}
