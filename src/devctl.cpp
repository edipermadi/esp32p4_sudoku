/* See devctl.h. */
#include "devctl.h"
#include <Arduino.h>
#include <lvgl.h>
#include "bsp/bsp_board.h"

#define LINE_MAX     96                 /* command line */
#define CHUNK        96                 /* coded bytes per S line (128 base64 characters) */
#define TAP_MS       120
#define SETTLE_MS    150                /* after a release: let the UI react */

static char s_line[LINE_MAX];
static int s_len;

/* lets LVGL run (timers, input, drawing) for ms */
static void pump(uint32_t ms)
{
    uint32_t end = millis() + ms;
    do {
        lv_timer_handler();
        delay(5);
    } while ((int32_t)(end - millis()) > 0);
}

/* ---------------------------------------------------------------- touches */

static void touch(bool pressed, int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= BSP_LCD_H_RES) x = BSP_LCD_H_RES - 1;
    if (y >= BSP_LCD_V_RES) y = BSP_LCD_V_RES - 1;
    bsp_touch_sim(true, pressed, (uint16_t)x, (uint16_t)y);
}

static void touch_done(int x, int y)
{
    touch(false, x, y);
    pump(SETTLE_MS);
    bsp_touch_sim(false, false, 0, 0);              /* the panel again */
}

static void swipe(int x1, int y1, int x2, int y2, uint32_t ms)
{
    const int steps = ms / 20 > 2 ? (int)(ms / 20) : 2;
    touch(true, x1, y1);
    pump(40);
    for (int i = 1; i <= steps; i++) {
        touch(true, x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps);
        pump(ms / steps);
    }
    touch_done(x2, y2);
}

/* ---------------------------------------------------------------- screenshot */

static uint32_t s_crc;
static uint8_t s_chunk[CHUNK];
static int s_chunk_n, s_seq;
static uint32_t s_bytes;

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, int n)   /* as zlib.crc32 */
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void flush_chunk(void)
{
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!s_chunk_n) return;
    char out[16 + CHUNK / 3 * 4 + 8];
    int o = snprintf(out, sizeof(out), "S %d ", s_seq++);
    for (int i = 0; i < s_chunk_n; i += 3) {
        uint32_t v = (uint32_t)s_chunk[i] << 16;
        if (i + 1 < s_chunk_n) v |= (uint32_t)s_chunk[i + 1] << 8;
        if (i + 2 < s_chunk_n) v |= s_chunk[i + 2];
        out[o++] = B64[v >> 18 & 63];
        out[o++] = B64[v >> 12 & 63];
        out[o++] = i + 1 < s_chunk_n ? B64[v >> 6 & 63] : '=';
        out[o++] = i + 2 < s_chunk_n ? B64[v & 63] : '=';
    }
    out[o++] = '\n';
    Serial.write((const uint8_t *)out, o);         /* one write: a whole line */
    s_crc = crc32_update(s_crc, s_chunk, s_chunk_n);
    s_bytes += s_chunk_n;
    s_chunk_n = 0;
}

static void put(uint8_t b)
{
    s_chunk[s_chunk_n++] = b;
    if (s_chunk_n == CHUNK) flush_chunk();
}

static void shot(void)
{
    lv_refr_now(NULL);                              /* everything drawn and flushed */
    const uint16_t *fb = bsp_display_frame();
    if (!fb) {
        Serial.println("ERR shot: no frame buffer");
        return;
    }
    Serial.printf("SHOT BEGIN %d %d\n", BSP_LCD_H_RES, BSP_LCD_V_RES);
    s_crc = 0;
    s_chunk_n = s_seq = 0;
    s_bytes = 0;
    const int total = BSP_LCD_H_RES * BSP_LCD_V_RES;
    for (int i = 0; i < total;) {                   /* runs of up to 256 equal pixels */
        uint16_t px = fb[i];
        int run = 1;
        while (run < 256 && i + run < total && fb[i + run] == px) run++;
        put((uint8_t)(run - 1));
        put((uint8_t)(px & 0xFF));
        put((uint8_t)(px >> 8));
        i += run;
    }
    flush_chunk();
    Serial.printf("SHOT END %u %08x\n", (unsigned)s_bytes, (unsigned)s_crc);
}

/* ---------------------------------------------------------------- commands */

static void run(char *line)
{
    char cmd[16] = "";
    int a[5] = {0, 0, 0, 0, 0};
    int n = sscanf(line, "%15s %d %d %d %d %d", cmd, &a[0], &a[1], &a[2], &a[3], &a[4]) - 1;
    if (n < 0) return;
    if (!strcmp(cmd, "ping")) {
        Serial.println("OK pong");
        return;
    }
    if (!strcmp(cmd, "baud") && n >= 1 && a[0] >= 9600) {
        Serial.printf("OK baud %d\n", a[0]);
        Serial.flush();
        delay(20);
        Serial.updateBaudRate((unsigned long)a[0]);
        return;
    }
    if (!strcmp(cmd, "tap") && n >= 2) {
        touch(true, a[0], a[1]);
        pump(TAP_MS);
        touch_done(a[0], a[1]);
    } else if (!strcmp(cmd, "long") && n >= 2) {
        touch(true, a[0], a[1]);
        pump(n >= 3 && a[2] > 0 ? (uint32_t)a[2] : 1000);
        touch_done(a[0], a[1]);
    } else if (!strcmp(cmd, "swipe") && n >= 4) {
        swipe(a[0], a[1], a[2], a[3], n >= 5 && a[4] > 0 ? (uint32_t)a[4] : 300);
    } else if (!strcmp(cmd, "press") && n >= 2) {
        touch(true, a[0], a[1]);
        pump(40);
    } else if (!strcmp(cmd, "move") && n >= 2) {
        touch(true, a[0], a[1]);
        pump(40);
    } else if (!strcmp(cmd, "release")) {
        touch_done(n >= 2 ? a[0] : 0, n >= 2 ? a[1] : 0);
    } else if (!strcmp(cmd, "wait") && n >= 1) {
        pump(a[0] > 0 ? (uint32_t)a[0] : 0);
    } else if (!strcmp(cmd, "shot")) {
        shot();
    } else {
        Serial.printf("ERR unknown: %s\n", line);
        return;
    }
    Serial.printf("OK %s\n", cmd);
}

void devctl_poll(void)
{
    while (Serial.available()) {
        int c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            s_line[s_len] = 0;
            if (s_len) run(s_line);
            s_len = 0;
        } else if (c >= ' ' && c < 127 && s_len < LINE_MAX - 1) {   /* (line noise from a reset: dropped) */
            s_line[s_len++] = (char)c;
        }
    }
}
