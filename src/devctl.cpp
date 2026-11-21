/* See devctl.h. */
#include "devctl.h"
#include <Arduino.h>
#include <lvgl.h>
#include "bsp/bsp_board.h"
#include "sudoku_ui.h"
#include "stats_db.h"
#include "generator.h"
#include <strings.h>
#include <stdio.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#define LINE_MAX     512                /* command line (fill: up to 81 pairs), or an upload's data line */
#define CHUNK        96                 /* coded bytes per S line (128 base64 characters) */
#define TAP_MS       120
#define SETTLE_MS    150                /* after a release: let the UI react */

static char s_line[LINE_MAX];
static int s_len;

/* the answer to a command: to Serial, or (devctl_run_line) into s_cap */
static char *s_cap;
static size_t s_cap_n;

static void reply(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (s_cap) {
        vsnprintf(s_cap, s_cap_n, fmt, ap);
        size_t l = strlen(s_cap);
        while (l && (s_cap[l - 1] == '\n' || s_cap[l - 1] == '\r')) s_cap[--l] = 0;
    } else {
        char buf[200];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        Serial.print(buf);
    }
    va_end(ap);
}

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

static char s_prefix = 'S';             /* S: a screenshot's lines, F: a file's */

static void flush_chunk(void)
{
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!s_chunk_n) return;
    char out[16 + CHUNK / 3 * 4 + 8];
    int o = snprintf(out, sizeof(out), "%c %d ", s_prefix, s_seq++);
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
    s_prefix = 'S';
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

/* ---------------------------------------------------------------- files on the card */
/*
 * Paths are relative to the card's root ("puzzles/a.sdm", "/" or "" = the
 * root), with %XX for a space, '%' or any other byte (the host tool does it).
 * sudoku.db (and its journal) is open while the board runs: it can be read
 * (get), but not replaced, renamed or deleted (Settings > USB drive for that).
 */
#define UP_WINDOW    12                 /* data lines between ACKs (the RX buffer holds them) */
#define UP_TIMEOUT   5000               /* ms without a data line: the upload is abandoned */
#define UP_BUF       (16 * 1024)        /* written to the card in pieces this big */

static int unhex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* "%20"-coded word -> the card's full path; false (and why) if it isn't a usable one */
static bool card_path(const char *word, char *out, size_t n, const char **why)
{
    char rel[200];
    size_t k = 0;
    for (const char *p = word; *p; p++) {
        char c = *p;
        if (c == '%' && unhex(p[1]) >= 0 && unhex(p[2]) >= 0) {
            c = (char)(unhex(p[1]) << 4 | unhex(p[2]));
            p += 2;
        }
        if (k + 1 >= sizeof(rel)) {
            *why = "path too long";
            return false;
        }
        rel[k++] = c;
    }
    rel[k] = 0;
    const char *r = rel;
    while (*r == '/') r++;
    size_t len = strlen(r);
    while (len && r[len - 1] == '/') len--;      /* "puzzles/" = "puzzles" */
    for (const char *s = r; s < r + len;) {      /* no ".." or "." parts */
        const char *e = (const char *)memchr(s, '/', (size_t)(r + len - s));
        size_t pl = e ? (size_t)(e - s) : (size_t)(r + len - s);
        if ((pl == 1 && s[0] == '.') || (pl == 2 && s[0] == '.' && s[1] == '.') || pl == 0) {
            *why = "no '.', '..' or empty parts in a path";
            return false;
        }
        s += pl + 1;
    }
    snprintf(out, n, "%s%s%.*s", BSP_SD_MOUNT_POINT, len ? "/" : "", (int)len, r);
    return true;
}

/* sudoku.db or its journal, while it is open */
static bool db_busy(const char *full)
{
    const char *db = BSP_SD_MOUNT_POINT "/sudoku.db";
    size_t n = strlen(db);
    return stats_db_ready() && !strncasecmp(full, db, n) && (full[n] == 0 || full[n] == '-');
}

static void print_name(const char *s)          /* %XX for spaces, '%' and control bytes */
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c <= ' ' || c == '%' || c == 127) Serial.printf("%%%02X", c);
        else Serial.write(c);
    }
}

static bool card_ls(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e;
    char p[300];
    while ((e = readdir(d)) != NULL) {
        snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
        struct stat st;
        bool isdir = e->d_type == DT_DIR;
        long size = 0;
        if (stat(p, &st) == 0) {
            isdir = S_ISDIR(st.st_mode);
            size = (long)st.st_size;
        }
        Serial.printf("L %c %ld ", isdir ? 'd' : 'f', isdir ? 0L : size);
        print_name(e->d_name);
        Serial.write('\n');
    }
    closedir(d);
    return true;
}

static bool card_get(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    struct stat st;
    long size = fstat(fileno(f), &st) == 0 ? (long)st.st_size : -1;
    Serial.printf("FILE BEGIN %ld\n", size);
    s_prefix = 'F';
    s_crc = 0;
    s_chunk_n = s_seq = 0;
    s_bytes = 0;
    uint8_t buf[CHUNK * 8];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) put(buf[i]);
    }
    flush_chunk();
    fclose(f);
    Serial.printf("FILE END %u %08x\n", (unsigned)s_bytes, (unsigned)s_crc);
    return true;
}

static int b64val(char c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
           : c == '+' ? 62 : c == '/' ? 63 : -1;
}

/* base64 -> bytes; -1 if it isn't base64 */
static int b64_decode(const char *s, uint8_t *out, int max)
{
    int n = 0, bits = 0;
    uint32_t acc = 0;
    for (; *s && *s != '='; s++) {
        int v = b64val(*s);
        if (v < 0) return -1;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= max) return -1;
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return n;
}

/* one line from the host, waiting up to ms; false on a timeout */
static bool read_line(char *buf, int max, uint32_t ms)
{
    int len = 0;
    uint32_t start = millis();
    while (millis() - start < ms) {
        int c = Serial.read();
        if (c < 0) {
            delay(1);
            continue;
        }
        if (c == '\r') continue;
        if (c == '\n') {
            buf[len] = 0;
            if (len) return true;
            continue;
        }
        if (len < max - 1) buf[len++] = (char)c;
    }
    return false;
}

/* put: "READY", then lines "D <base64>" (an ACK after each UP_WINDOW of them) until size bytes;
 * written to <path>.part, which becomes <path> once the CRC matches */
static void card_put(const char *path, long size, uint32_t crc)
{
    char part[300];
    snprintf(part, sizeof(part), "%s.part", path);
    FILE *f = fopen(part, "wb");
    if (!f) {
        Serial.printf("ERR put: can't write %s (%s)\n", part, strerror(errno));
        return;
    }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(UP_BUF, MALLOC_CAP_SPIRAM);
    if (!buf) {
        fclose(f);
        remove(part);
        Serial.println("ERR put: no memory");
        return;
    }
    Serial.println("READY");
    long got = 0;
    int fill = 0, lines = 0;
    uint32_t c = 0;
    const char *err = NULL;
    while (got < size && !err) {
        if (!read_line(s_line, LINE_MAX, UP_TIMEOUT)) {
            err = "timed out waiting for data";
            break;
        }
        if (s_line[0] != 'D' || s_line[1] != ' ') {
            err = "expected a data line";
            break;
        }
        if (fill + CHUNK > UP_BUF) {
            if (fwrite(buf, 1, fill, f) != (size_t)fill) err = "write failed (card full?)";
            fill = 0;
        }
        int n = b64_decode(s_line + 2, buf + fill, CHUNK);
        if (n < 0 || got + n > size) {
            err = "bad data line";
            break;
        }
        c = crc32_update(c, buf + fill, n);
        fill += n;
        got += n;
        if (++lines % UP_WINDOW == 0 && got < size) Serial.printf("ACK %ld\n", got);
    }
    if (!err && fill && fwrite(buf, 1, fill, f) != (size_t)fill) err = "write failed (card full?)";
    heap_caps_free(buf);
    if (fclose(f) != 0 && !err) err = "write failed";
    if (!err && c != crc) err = "CRC mismatch";
    if (!err) {
        remove(path);                              /* (FAT's rename doesn't replace) */
        if (rename(part, path) != 0) err = "couldn't rename the .part file";
    }
    if (err) {
        remove(part);
        while (read_line(s_line, LINE_MAX, 300)) {}   /* the rest of what the host sent */
        Serial.printf("ERR put: %s\n", err);
        return;
    }
    Serial.printf("OK put %ld\n", got);
}

/* ls / get / put / mv / rm / mkdir; true if line was one of them (it has answered) */
static bool run_file(char *line)
{
    char cmd[8], a[200] = "", b[200] = "";
    long size = -1;
    unsigned long crc = 0;
    int n = sscanf(line, "%7s %199s %199s", cmd, a, b);
    if (n < 1) return false;
    bool is_ls = !strcmp(cmd, "ls"), is_get = !strcmp(cmd, "get"), is_put = !strcmp(cmd, "put"),
         is_mv = !strcmp(cmd, "mv"), is_rm = !strcmp(cmd, "rm"), is_mkdir = !strcmp(cmd, "mkdir");
    if (!(is_ls || is_get || is_put || is_mv || is_rm || is_mkdir)) return false;

    char p1[260], p2[260];
    const char *why = "";
    if (!is_ls && n < 2) {
        Serial.printf("ERR %s: needs a path\n", cmd);
        return true;
    }
    if (!card_path(n >= 2 ? a : "", p1, sizeof(p1), &why)) {
        Serial.printf("ERR %s: %s\n", cmd, why);
        return true;
    }
    if (is_ls) {
        if (!card_ls(p1)) Serial.printf("ERR ls: can't open %s (%s; is the card in?)\n", p1, strerror(errno));
        else Serial.println("OK ls");
        return true;
    }
    if (is_get) {
        if (!card_get(p1)) Serial.printf("ERR get: can't read %s (%s)\n", p1, strerror(errno));
        else Serial.println("OK get");
        return true;
    }
    if (is_rm || is_mkdir) {
        if (db_busy(p1)) {
            Serial.printf("ERR %s: sudoku.db is open (use Settings > USB drive)\n", cmd);
            return true;
        }
        struct stat st;
        int r = is_mkdir ? mkdir(p1, 0775) : stat(p1, &st) == 0 && S_ISDIR(st.st_mode) ? rmdir(p1) : remove(p1);
        if (r != 0) Serial.printf("ERR %s: %s: %s\n", cmd, p1, strerror(errno));
        else Serial.printf("OK %s\n", cmd);
        return true;
    }
    if (is_mv) {
        if (n < 3 || !card_path(b, p2, sizeof(p2), &why)) {
            Serial.printf("ERR mv: %s\n", n < 3 ? "needs two paths" : why);
            return true;
        }
        if (db_busy(p1) || db_busy(p2)) {
            Serial.println("ERR mv: sudoku.db is open (use Settings > USB drive)");
            return true;
        }
        struct stat st;
        if (stat(p2, &st) == 0) {
            Serial.printf("ERR mv: %s exists (rm it first)\n", p2);
            return true;
        }
        if (rename(p1, p2) != 0) Serial.printf("ERR mv: %s\n", strerror(errno));
        else Serial.println("OK mv");
        return true;
    }
    /* put <path> <size> <crc32 hex> */
    if (n < 2 || sscanf(line, "%*s %*s %ld %lx", &size, &crc) != 2 || size < 0) {
        Serial.println("ERR put: put <path> <size> <crc32 hex>");
        return true;
    }
    if (db_busy(p1)) {
        Serial.println("ERR put: sudoku.db is open (use Settings > USB drive)");
        return true;
    }
    card_put(p1, size, (uint32_t)crc);
    return true;
}

/* ---------------------------------------------------------------- commands */

/* (fill and state are functions of their own, not inline in run(): their buffers would
 * sit on the stack under every command, a tap among them, and a tap that opens a hard
 * puzzle runs the solver's deep recursion on top) */
/* the game on screen, as text */
static void __attribute__((noinline)) cmd_state(void)
{
    sudoku_ui_state_t st;
    char board[82];
    if (!sudoku_ui_state(&st, board)) {
        reply("ERR state: no game on screen\n");
        return;
    }
    if (s_cap) {                                /* over sound: what fits a message (140 bytes) */
        reply("OK state s%d %s\n", st.solved, board);
        return;
    }
    Serial.printf("STATE name=\"%s\" variant=%s size=%d solved=%d paused=%d filled=%d/%d time=%u "
                  "mistakes=%u hints=%u%s%s\n", st.name, st.variant, st.size, st.solved, st.paused, st.filled,
                  st.cells, (unsigned)st.seconds, st.mistakes, st.hints, st.practice ? " practice=1" : "",
                  st.attack ? " attack=1" : "");
    Serial.printf("BOARD %s\n", board);
    reply("OK state\n");
}

/* fill <cell> <digit> ...: any separators, e.g. (12,5),(13,7) */
static void __attribute__((noinline)) cmd_fill(const char *line)
{
    uint8_t cells[81], digits[81];
    int nums = 0, np = 0, v = -1;
    bool bad = false;
    for (const char *p = line + 4;; p++) {      /* the numbers after "fill", in pairs */
        if (*p >= '0' && *p <= '9') {
            v = (v < 0 ? 0 : v * 10) + (*p - '0');
            if (v > 255) bad = true;
            continue;
        }
        if (v >= 0) {
            if (nums % 2 == 0) {
                if (np >= 81) bad = true;
                else cells[np] = (uint8_t)v;
            } else if (np < 81) {
                digits[np++] = (uint8_t)v;
            }
            nums++;
            v = -1;
        }
        if (!*p) break;
    }
    if (bad || nums == 0 || nums % 2) {
        reply("ERR fill: pairs of <cell 0-80> <digit>, e.g. fill 12:5 13:7 (at most 81)\n");
        return;
    }
    char why[64];
    int placed = sudoku_ui_fill(cells, digits, np, why, sizeof(why));
    if (placed < 0) {
        reply("ERR fill: %s\n", why);
        return;
    }
    pump(SETTLE_MS);                            /* the board redraws (and the solve begins, if done) */
    reply("OK fill %d\n", placed);
}

/* ---- genbench: the puzzle generator's time on the board */

/* a variant or level by number, name or the start of a name (any case); -1: none, -2: ambiguous */
static int gb_pick(const char *s, const char *(*name)(int), int n)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s && !*end) return v >= 0 && v < n ? (int)v : -1;
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(s, name(i))) return i;
    }
    int hit = -1, hits = 0;
    for (int i = 0; i < n; i++) {
        if (!strncasecmp(s, name(i), strlen(s))) { hit = i; hits++; }
    }
    return hits == 1 ? hit : hits ? -2 : -1;
}

static struct {
    int variant, level;
    uint32_t seed;
    uint32_t us;
    int got, clues;
    volatile bool done;
} s_gb;
static sudoku_geom_t *s_gb_board;                   /* PSRAM */

/* one puzzle, on the worker core like New puzzle's (with its yields: the task watchdog) */
static void gb_task(void *arg)
{
    uint8_t givens[81];
    gen_level_t got = (gen_level_t)s_gb.level;
    int64_t t0 = esp_timer_get_time();
    sudoku_generate(s_gb.variant, (gen_level_t)s_gb.level, s_gb.seed, givens, s_gb_board, &got);
    s_gb.us = (uint32_t)(esp_timer_get_time() - t0);
    s_gb.got = got;
    s_gb.clues = 0;
    for (int i = 0; i < 81; i++) s_gb.clues += givens[i] != 0;
    s_gb.done = true;
    vTaskDelete(NULL);
}

/* genbench <variant|all> [level|all] [n]: n puzzles (default 5) of each, the same seeds every
 * time (so builds compare), with the CPU at full speed even while the screen is dark */
static void cmd_genbench(const char *line)
{
    char vs[24] = "", ls[16] = "all";
    int n = 5;
    if (sscanf(line, "%*s %23s %15s %d", vs, ls, &n) < 1) {
        reply("ERR genbench: genbench <variant|all> [level|all] [n], e.g. genbench greater expert 10\n");
        return;
    }
    if (n < 1) n = 1;
    if (n > 200) n = 200;
    bool all_v = !strcasecmp(vs, "all"), all_l = !strcasecmp(ls, "all");
    int v0 = all_v ? 0 : gb_pick(vs, variant_name, VAR_COUNT), l0 = all_l ? 0 : gb_pick(ls, gen_level_name, GEN_LEVELS);
    if (v0 < 0 || l0 < 0) {
        reply("ERR genbench: %s %s: %s\n", v0 < 0 ? "variant" : "level", v0 < 0 ? vs : ls,
              (v0 < 0 ? v0 : l0) == -2 ? "more than one match" : "no such one");
        return;
    }
    if (!s_gb_board && !(s_gb_board = (sudoku_geom_t *)heap_caps_malloc(sizeof(*s_gb_board), MALLOC_CAP_SPIRAM))) {
        reply("ERR genbench: no memory\n");
        return;
    }
    {                                               /* not over a puzzle being solved */
        sudoku_ui_state_t st;
        char board[82];
        if (sudoku_ui_state(&st, board) && !st.solved) {
            reply("ERR genbench: a puzzle is being solved (the game screen is open): leave it first\n");
            return;
        }
    }
    const char *io = sudoku_ui_io_busy();          /* nothing on the card under way meanwhile */
    if (io) {
        reply("ERR genbench: %s: try again when it's done\n", io);
        return;
    }
    if (!gen_bench_begin()) {
        reply("ERR genbench: a puzzle is being generated (New puzzle or Learn): try again when it's done\n");
        return;
    }
    bsp_power_cpu_full(true);                       /* (screen dark: 180 MHz otherwise) */
    Serial.printf("GENBENCH CPU %u MHz, %d puzzle%s each\n", (unsigned)getCpuFrequencyMhz(), n, n == 1 ? "" : "s");
    for (int v = v0; v < (all_v ? VAR_COUNT : v0 + 1); v++) {
        for (int l = l0; l < (all_l ? GEN_LEVELS : l0 + 1); l++) {
            if (all_l && v == VAR_KIDS && l >= GEN_HARD) continue;   /* (Kids 4x4: Easy and Medium only) */
            uint32_t sum = 0, mn = UINT32_MAX, mx = 0;
            int on = 0, clues = 0;
            for (int i = 0; i < n; i++) {
                char what[96];                      /* the screen says what's running */
                snprintf(what, sizeof(what), "devctl genbench: puzzle generation\n%s %s, %d of %d", variant_name(v),
                         gen_level_name(l), i + 1, n);
                sudoku_ui_busy(what);
                s_gb.variant = v;
                s_gb.level = l;
                s_gb.seed = 0x5EED0000u + (uint32_t)v * 1000u + (uint32_t)l * 100u + (uint32_t)i;
                s_gb.done = false;
                if (xTaskCreatePinnedToCore(gb_task, "genbench", 16384, NULL, 2, NULL, 0) != pdPASS) {
                    reply("ERR genbench: no task\n");
                    goto out;
                }
                while (!s_gb.done) {
                    lv_display_trigger_activity(NULL);   /* (no screen timeout meanwhile) */
                    pump(5);
                }
                sum += s_gb.us;
                if (s_gb.us < mn) mn = s_gb.us;
                if (s_gb.us > mx) mx = s_gb.us;
                on += s_gb.got == l;
                clues += s_gb.clues;
            }
            Serial.printf("GENBENCH %s %s: avg %u ms, min %u, max %u; on level %d/%d; %u.%u clues\n", variant_name(v),
                          gen_level_name(l), (unsigned)(sum / n / 1000), (unsigned)(mn / 1000), (unsigned)(mx / 1000), on,
                          n, (unsigned)(clues / n), (unsigned)(clues * 10 / n % 10));
        }
    }
    reply("OK genbench\n");
out:
    bsp_power_cpu_full(false);                      /* (back to 180 MHz if still dark) */
    sudoku_ui_busy(NULL);
    gen_bench_end();
}

static void run(char *line)
{
    if (!s_cap && run_file(line)) return;           /* (files: serial only) */
    char cmd[16] = "";
    int a[5] = {0, 0, 0, 0, 0};
    int n = sscanf(line, "%15s %d %d %d %d %d", cmd, &a[0], &a[1], &a[2], &a[3], &a[4]) - 1;
    if (n < 0) return;
    if (!strcmp(cmd, "ping")) {
        reply("OK pong\n");
        return;
    }
    if (s_cap) {                                    /* over sound: touches, wait, heap, screenoff only */
        static const char *const ok[] = {"tap", "long", "swipe", "press", "move", "release", "wait", "heap", "screenoff",
                                         "fill", "state", "pz"};
        bool allowed = false;
        for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) allowed |= !strcmp(cmd, ok[i]);
        if (!allowed) {
            reply("ERR not over sound: %s\n", cmd);
            return;
        }
        if (!strcmp(cmd, "wait") && a[0] > 10000) a[0] = 10000;
        if (!strcmp(cmd, "heap")) {                 /* short: a reply is ~16 bytes a second */
            reply("OK heap i=%u d=%u p=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            return;
        }
    }
    if (!strcmp(cmd, "screen")) {                  /* where the board is: home1, game+dialog, ... */
        char name[48];
        sudoku_ui_screen_name(name, sizeof(name));
        reply("OK screen %s\n", name);
        return;
    }
    if (!strcmp(cmd, "state")) {                   /* the game on screen, as text */
        cmd_state();
        return;
    }
    if (!strcmp(cmd, "pz")) {                      /* pz <id> <i>/<n> <part>: a puzzle sent by sound (sudoku_ui.c) */
        char o[96];
        sudoku_ui_puzzle_part(line + 2, o, sizeof(o));
        reply("%s\n", o);
        return;
    }
    if (!strcmp(cmd, "genbench")) {                /* genbench <variant|all> [level|all] [n] */
        cmd_genbench(line);
        return;
    }
    if (!strcmp(cmd, "fill")) {                    /* fill <cell> <digit> ...: any separators, e.g. (12,5),(13,7) */
        cmd_fill(line);
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
    } else if (!strcmp(cmd, "heap")) {           /* free memory: internal (and its DMA part), PSRAM */
        Serial.printf("HEAP internal %u free, largest %u, min ever %u; dma %u, largest %u; psram %u free, largest %u\n",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        if (n >= 1 && a[0]) heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);   /* "heap 1": per region */
    } else if (!strcmp(cmd, "bench")) {          /* N full-screen redraws of the screen shown */
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
            if (d > mx) mx = d;
            if (d < mn) mn = d;
        }
        uint32_t avg = (uint32_t)((esp_timer_get_time() - t0) / frames);
        Serial.printf("BENCH %d frames: avg %u us, min %u us, max %u us (%u.%u fps)\n", frames, (unsigned)avg,
                      (unsigned)mn, (unsigned)mx, (unsigned)(10000000 / avg / 10), (unsigned)(10000000 / avg % 10));
    } else if (!strcmp(cmd, "screenoff")) {
        sudoku_ui_screen_off();                     /* as the timeout: black, then the lock screen if on */
        pump(200);
    } else {
        reply("ERR unknown: %s\n", line);
        return;
    }
    reply("OK %s\n", cmd);
}

bool devctl_run_line(const char *line, char *out, size_t n)
{
    char buf[LINE_MAX];
    strlcpy(buf, line, sizeof(buf));
    out[0] = 0;
    s_cap = out;
    s_cap_n = n;
    run(buf);
    s_cap = NULL;
    if (!out[0]) strlcpy(out, "ERR empty", n);
    return !strncmp(out, "OK", 2);
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
