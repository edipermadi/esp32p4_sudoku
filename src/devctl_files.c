/**
 * @file devctl_files.c
 * @ingroup devctl
 * @brief ls, get, put, mv, rm, mkdir: files on the microSD card (serial only).
 */
#include "devctl_internal.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "bsp/bsp_board.h"
#include "stats_db.h"
#include "ui_sudoku.h"
#include "esp_timer.h"
#include <lvgl.h>                      /* (LV_SYMBOL_*: the toast's icons) */
#include <miniz.h>

/* ---------------------------------------------------------------- files on the card */
/*
 * Paths are relative to the card's root ("puzzles/a.sdm", "/" or "" = the
 * root), with %XX for a space, '%' or any other byte (the host tool does it).
 * sudoku.db (and its journal) is open while the board runs: it can be read
 * (get), but not replaced, renamed or deleted (Settings > USB drive for that).
 */
#define UP_WINDOW    12                             /**< data lines between ACKs (the RX buffer holds them) */
#define UP_TIMEOUT   5000                           /**< ms without a data line: the upload is abandoned */
#define UP_BUF       (16 * 1024)                    /**< written to the card in pieces this big */
#define IMPORT_TIMEOUT 300000                       /**< import: the most it waits for sudoku.db, ms */

/**
 * @brief A hex digit's value.
 * @param[in] c  the character
 * @return 0..15, or -1 if it isn't one
 */
static int unhex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/**
 * @brief A "%20"-coded path from the card's root -> the full path (no ".", ".." or empty parts).
 * @param[in] word  the coded path
 * @param[out] out  the full path
 * @param[in] n  out's size
 * @param[out] why  what's wrong, if false
 * @return false if it isn't a usable path
 */
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
    while (*r == '/') {
        r++;
    }
    size_t len = strlen(r);
    while (len && r[len - 1] == '/') {
        len--;    /* "puzzles/" = "puzzles" */
    }
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

/**
 * @brief The path is sudoku.db (or its journal), and it's open.
 * @param[in] full  a full path
 * @return true if it mustn't be replaced, renamed or deleted
 */
static bool is_db_busy(const char *full)
{
    const char *db = BSP_SD_MOUNT_POINT "/sudoku.db";
    size_t n = strlen(db);
    return is_stats_db_ready() && !strncasecmp(full, db, n) && (full[n] == 0 || full[n] == '-');
}

/**
 * @brief Sends a file name with %XX for spaces, '%' and control bytes.
 * @param[in] s  the name
 */
static void print_name(const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c <= ' ' || c == '%' || c == 127) {
            serial_printf("%%%02X", c);
        } else {
            serial_write_byte((uint8_t)c);
        }
    }
}

/**
 * @brief ls: a line "L \<d|f\> \<size\> \<name\>" per entry of a folder.
 * @param[in] dir  the folder (full path)
 * @return false if it can't be opened
 */
static bool card_ls(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        return false;
    }
    struct dirent *e;
    char p[300];
    while ((e = readdir(d)) != NULL) {
        bool whole = snprintf(p, sizeof(p), "%s/%s", dir, e->d_name) < (int)sizeof(p);   /* (too long: no size) */
        struct stat st;
        bool isdir = e->d_type == DT_DIR;
        long size = 0;
        if (whole && stat(p, &st) == 0) {
            isdir = S_ISDIR(st.st_mode);
            size = (long)st.st_size;
        }
        serial_printf("L %c %ld ", isdir ? 'd' : 'f', isdir ? 0L : size);
        print_name(e->d_name);
        serial_write_byte('\n');
    }
    closedir(d);
    return true;
}

/**
 * @brief get \<path\> [z]: the file (with z, compressed).
 * @param[in] path  the file (full path)
 * @param[in] z  compressed
 * @return false if it can't be opened (errno: why)
 */
static bool card_get(const char *path, bool z)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    if (!devctl_z_begin('F', z)) {
        fclose(f);
        errno = ENOMEM;
        return false;
    }
    struct stat st;
    long size = fstat(fileno(f), &st) == 0 ? (long)st.st_size : -1;
    serial_printf("FILE BEGIN %ld%s\n", size, z ? " z" : "");
    uint8_t buf[CHUNK * 8];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        devctl_z_write(buf, n);
    }
    devctl_z_end();
    fclose(f);
    serial_printf("FILE END %u %08x\n", (unsigned)devctl_z_bytes(), (unsigned)devctl_z_crc());
    return true;
}

/** @brief put z: the inflate state for an upload that comes compressed (PSRAM). */
typedef struct {
    tinfl_decompressor inf;                         /**< the ROM's inflate */
    uint8_t dict[TINFL_LZ_DICT_SIZE];               /**< the window: inflate writes round it */
    size_t at;                                      /**< where it writes next in the window */
} put_inflate_t;

/**
 * @brief A size as text: "512 B", "3.4 KB", "7.1 MB".
 * @param[out] buf  gets it
 * @param[in] n    its size
 * @param[in] b    bytes
 */
static void put_size_text(char *buf, size_t n, long b)
{
    if (b < 1024) {
        snprintf(buf, n, "%ld B", b);
    } else if (b < 1024 * 1024) {
        snprintf(buf, n, "%ld.%ld KB", b / 1024, b % 1024 * 10 / 1024);
    } else {
        snprintf(buf, n, "%ld.%ld MB", b / 1048576, b % 1048576 * 10 / 1048576);
    }
}

/**
 * @brief put: "READY", then lines "D \<base64\>" (an ACK after each UP_WINDOW of them) until all the bytes
 *        are in; written to \<path\>.part, which becomes \<path\> once the CRC matches.
 * @param[in] path  the file (full path)
 * @param[in] size  its size
 * @param[in] crc  its CRC-32
 * @param[in] zsize  -1, or the size of the file compressed (zlib format, inflated here)
 */
static void card_put(const char *path, long size, uint32_t crc, long zsize)
{
    /* the toast on the board: the file coming (shown during the transfer), then how it went */
    const char *name = strncmp(path, BSP_SD_MOUNT_POINT "/", strlen(BSP_SD_MOUNT_POINT "/")) == 0
                       ? path + strlen(BSP_SD_MOUNT_POINT "/") : path;
    char sz[24], note[400];                         /* (room for the longest path) */
    put_size_text(sz, sizeof(sz), size);
    snprintf(note, sizeof(note), LV_SYMBOL_DOWNLOAD "  Receiving %s from the computer (%s)...", name, sz);
    sudoku_ui_progress(note);
    char part[300];
    snprintf(part, sizeof(part), "%s.part", path);
    FILE *f = fopen(part, "wb");
    if (!f) {
        serial_printf("ERR put: can't write %s (%s)\n", part, strerror(errno));
        snprintf(note, sizeof(note), "Couldn't receive %s: it can't be written on the card.", name);
        sudoku_ui_progress_done(note, 4000);
        return;
    }
    uint8_t *buf = (uint8_t *)heap_caps_malloc(UP_BUF, MALLOC_CAP_SPIRAM);
    if (!buf) {
        fclose(f);
        remove(part);
        serial_println("ERR put: no memory");
        snprintf(note, sizeof(note), "Couldn't receive %s: out of memory.", name);
        sudoku_ui_progress_done(note, 4000);
        return;
    }
    put_inflate_t *z = NULL;
    if (zsize >= 0 && !(z = (put_inflate_t *)heap_caps_malloc(sizeof(put_inflate_t), MALLOC_CAP_SPIRAM))) {
        heap_caps_free(buf);
        fclose(f);
        remove(part);
        serial_println("ERR put: no memory");
        snprintf(note, sizeof(note), "Couldn't receive %s: out of memory.", name);
        sudoku_ui_progress_done(note, 4000);
        return;
    }
    if (z) {
        tinfl_init(&z->inf);
        z->at = 0;
    }
    serial_println("READY");
    long got = 0, zgot = 0;                         /* the file's bytes; with z, the compressed ones read */
    int fill = 0, lines = 0;
    uint32_t c = 0;
    const char *err = NULL;
    bool zdone = false;
    uint8_t in[CHUNK];
    while ((z ? zgot < zsize : got < size) && !err) {
        if (!devctl_read_line(devctl_line_buf(), CMD_MAX, UP_TIMEOUT)) {
            err = "timed out waiting for data";
            break;
        }
        if (devctl_line_buf()[0] != 'D' || devctl_line_buf()[1] != ' ') {
            err = "expected a data line";
            break;
        }
        int n = devctl_b64_decode(devctl_line_buf() + 2, in, CHUNK);
        if (n < 0 || (z ? zgot + n > zsize : got + n > size)) {
            err = "bad data line";
            break;
        }
        const uint8_t *ip = in;
        size_t left = (size_t)n;
        zgot += n;
        while (!err) {                              /* the file's bytes this line makes: into buf */
            const uint8_t *out;
            size_t out_n;
            if (z) {
                size_t in_n = left, room = TINFL_LZ_DICT_SIZE - z->at;
                tinfl_status st = zdone ? TINFL_STATUS_DONE
                                  : tinfl_decompress(&z->inf, ip, &in_n, z->dict, z->dict + z->at, &room,
                                                     TINFL_FLAG_PARSE_ZLIB_HEADER |
                                                     (zgot < zsize ? TINFL_FLAG_HAS_MORE_INPUT : 0));
                ip += in_n;
                left -= in_n;
                out = z->dict + z->at;
                out_n = zdone ? 0 : room;
                z->at = (z->at + room) & (TINFL_LZ_DICT_SIZE - 1);
                if (st < 0) {
                    err = "bad compressed data";
                }
                zdone |= st == TINFL_STATUS_DONE;
                if (zdone && left) {
                    err = "data after the end";
                }
                if (!out_n && (st != TINFL_STATUS_HAS_MORE_OUTPUT || zdone)) {
                    left = 0;                       /* (this line's input is used up) */
                }
            } else {
                out = ip;
                out_n = left;
                left = 0;
            }
            if (got + (long)out_n > size) {
                err = "more data than the size said";
                break;
            }
            for (size_t done = 0; done < out_n && !err;) {
                if (fill == UP_BUF) {
                    if (fwrite(buf, 1, fill, f) != (size_t)fill) {
                        err = "write failed (card full?)";
                    }
                    fill = 0;
                }
                size_t k = out_n - done < (size_t)(UP_BUF - fill) ? out_n - done : (size_t)(UP_BUF - fill);
                memcpy(buf + fill, out + done, k);
                c = devctl_crc32(c, buf + fill, (int)k);
                fill += (int)k;
                got += (long)k;
                done += k;
            }
            if (!left && (!z || !out_n)) {
                break;
            }
        }
        if (++lines % UP_WINDOW == 0 && (z ? zgot < zsize : got < size)) {
            serial_printf("ACK %ld\n", z ? zgot : got);
        }
    }
    if (!err && z && !zdone) {
        err = "the compressed data ended early";
    }
    if (!err && got != size) {
        err = "not the size it said";
    }
    heap_caps_free(z);
    if (!err && fill && fwrite(buf, 1, fill, f) != (size_t)fill) {
        err = "write failed (card full?)";
    }
    heap_caps_free(buf);
    if (fclose(f) != 0 && !err) {
        err = "write failed";
    }
    if (!err && c != crc) {
        err = "CRC mismatch";
    }
    if (!err) {
        remove(path);                              /* (FAT's rename doesn't replace) */
        if (rename(part, path) != 0) {
            err = "couldn't rename the .part file";
        }
    }
    if (err) {
        remove(part);
        while (devctl_read_line(devctl_line_buf(), DEVCTL_LINE_MAX, 300)) {}   /* the rest of what the host sent */
        serial_printf("ERR put: %s\n", err);
        snprintf(note, sizeof(note), "Couldn't receive %s: %s.", name, err);
        sudoku_ui_progress_done(note, 4000);
        return;
    }
    serial_printf("OK put %ld\n", got);
    snprintf(note, sizeof(note), LV_SYMBOL_OK "  Received %s from the computer (%s).", name, sz);
    sudoku_ui_progress_done(note, 3000);
}

/**
 * @brief import: a puzzle file on the card imported as Settings > Data > Import (or Files' Import) does,
 *        waited for (grading 100 puzzles takes a while): "IMPORT \<what happened\>", then "OK import
 *        \<imported\> \<left out\>"; or "ERR import: ...". The board shows its notice and its screens
 *        take the new collection as for an import from the screen (the UI runs meanwhile).
 * @param[in] full  the file (full path)
 */
static void card_import(const char *full)
{
    struct stat st;
    if (stat(full, &st) != 0 || S_ISDIR(st.st_mode)) {
        serial_printf("ERR import: %s: %s\n", full, stat(full, &st) != 0 ? strerror(errno) : "a folder");
        return;
    }
    if (!is_stats_db_ready()) {
        serial_println("ERR import: no sudoku.db");
        return;
    }
    stats_import_result_t before, r;
    stats_db_import_last(&before);
    stats_db_import_file(full);
    const int64_t t0 = esp_timer_get_time();
    do {
        devctl_pump(50);                            /* (the UI runs: its notice, the collection list) */
        stats_db_import_last(&r);
    } while (r.seq == before.seq && esp_timer_get_time() - t0 < (int64_t)IMPORT_TIMEOUT * 1000);
    if (r.seq == before.seq) {
        serial_println("ERR import: no answer from sudoku.db");
    } else if (r.got > 0) {
        serial_printf("IMPORT %s\n", r.msg);
        serial_printf("OK import %d %d\n", r.got, r.bad);
    } else {
        serial_printf("ERR import: %s\n", r.msg);
    }
}

/* (documented in devctl_internal.h) */
bool devctl_files_cmd(char *line)
{
    char cmd[8], a[200] = "", b[200] = "";
    long size = -1;
    unsigned long crc = 0;
    int n = sscanf(line, "%7s %199s %199s", cmd, a, b);
    if (n < 1) {
        return false;
    }
    bool is_ls = !strcmp(cmd, "ls"), is_get = !strcmp(cmd, "get"), is_put = !strcmp(cmd, "put"),
         is_mv = !strcmp(cmd, "mv"), is_rm = !strcmp(cmd, "rm"), is_mkdir = !strcmp(cmd, "mkdir"),
         is_import = !strcmp(cmd, "import");
    if (!(is_ls || is_get || is_put || is_mv || is_rm || is_mkdir || is_import)) {
        return false;
    }

    char p1[260], p2[260];
    const char *why = "";
    if (!is_ls && n < 2) {
        serial_printf("ERR %s: needs a path\n", cmd);
        return true;
    }
    if (!card_path(n >= 2 ? a : "", p1, sizeof(p1), &why)) {
        serial_printf("ERR %s: %s\n", cmd, why);
        return true;
    }
    if (is_ls) {
        if (!card_ls(p1)) {
            serial_printf("ERR ls: can't open %s (%s; is the card in?)\n", p1, strerror(errno));
        } else {
            serial_println("OK ls");
        }
        return true;
    }
    if (is_get) {
        if (!card_get(p1, n >= 3 && !strcmp(b, "z"))) {
            serial_printf("ERR get: can't read %s (%s)\n", p1, strerror(errno));
        } else {
            serial_println("OK get");
        }
        return true;
    }
    if (is_import) {
        card_import(p1);
        return true;
    }
    if (is_rm || is_mkdir) {
        if (is_db_busy(p1)) {
            serial_printf("ERR %s: sudoku.db is open (use Settings > USB drive)\n", cmd);
            return true;
        }
        struct stat st;
        int r = is_mkdir ? mkdir(p1, 0775) : stat(p1, &st) == 0 && S_ISDIR(st.st_mode) ? rmdir(p1) : remove(p1);
        if (r != 0) {
            serial_printf("ERR %s: %s: %s\n", cmd, p1, strerror(errno));
        } else {
            serial_printf("OK %s\n", cmd);
        }
        return true;
    }
    if (is_mv) {
        if (n < 3 || !card_path(b, p2, sizeof(p2), &why)) {
            serial_printf("ERR mv: %s\n", n < 3 ? "needs two paths" : why);
            return true;
        }
        if (is_db_busy(p1) || is_db_busy(p2)) {
            serial_println("ERR mv: sudoku.db is open (use Settings > USB drive)");
            return true;
        }
        struct stat st;
        if (stat(p2, &st) == 0) {
            serial_printf("ERR mv: %s exists (rm it first)\n", p2);
            return true;
        }
        if (rename(p1, p2) != 0) {
            serial_printf("ERR mv: %s\n", strerror(errno));
        } else {
            serial_println("OK mv");
        }
        return true;
    }
    /* put <path> <size> <crc32 hex> [z <compressed size>] */
    long zsize = -1;
    char zw[4] = "";
    int got = n < 2 ? 0 : sscanf(line, "%*s %*s %ld %lx %3s %ld", &size, &crc, zw, &zsize);
    if (got < 2 || size < 0 || (got >= 3 && (strcmp(zw, "z") || got < 4 || zsize < 0))) {
        serial_println("ERR put: put <path> <size> <crc32 hex> [z <compressed size>]");
        return true;
    }
    if (is_db_busy(p1)) {
        serial_println("ERR put: sudoku.db is open (use Settings > USB drive)");
        return true;
    }
    card_put(p1, size, (uint32_t)crc, got >= 4 ? zsize : -1);
    return true;
}
