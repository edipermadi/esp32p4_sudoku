/*
 * SQLite for ESP32-P4 (Arduino / ESP-IDF): the official amalgamation
 * (sqlite3_amalgamation.h, SQLite 3.54.1 from sqlite.org, SHA3-256 checked) built with
 * SQLITE_OS_OTHER, plus a small VFS on top of ESP-IDF's POSIX file API, so a
 * database can live on any mounted VFS path such as the FAT32 microSD card.
 *
 * The amalgamation (sqlite3.c) is kept under a header name so the Arduino builder doesn't compile it
 * a second time on its own; this file is its only translation unit.
 *
 * Built single-threaded (SQLITE_THREADSAFE=0): use one connection from one
 * task (stats_db.c's worker). File locking is a no-op, so only this firmware
 * may open the file while it runs.
 */

/* ------------------------------------------------------------ build options */
/* Release build: SQLite's assert()s call helpers that only exist with
 * SQLITE_DEBUG. NDEBUG alone isn't enough here: this core's sdkconfig has
 * CONFIG_COMPILER_ASSERT_NDEBUG_EVALUATE, which keeps evaluating the
 * expression. ESP-IDF's assert.h is #pragma once, so include it now and
 * make assert() a true no-op for the amalgamation. */
#define NDEBUG 1
#include <assert.h>
#undef assert
#define assert(x) ((void)0)
#define SQLITE_OS_OTHER                 1   /* no os_unix/os_win: our VFS below */
#define SQLITE_THREADSAFE               0
#define SQLITE_OMIT_WAL                 1   /* needs shared memory (xShm*) */
#define SQLITE_OMIT_LOAD_EXTENSION      1
#define SQLITE_OMIT_SHARED_CACHE        1
#define SQLITE_OMIT_DEPRECATED          1
#define SQLITE_DQS                      0
#define SQLITE_DEFAULT_MEMSTATUS        0
#define SQLITE_TEMP_STORE               3   /* temp tables/indices in RAM */
#define SQLITE_DEFAULT_CACHE_SIZE       (-64)   /* 64 KiB page cache */
#define SQLITE_MAX_MMAP_SIZE            0
#define SQLITE_DEFAULT_FOREIGN_KEYS     1
#define SQLITE_LIKE_DOESNT_MATCH_BLOBS  1
#define HAVE_LOCALTIME_R                1
#define HAVE_GMTIME_R                   1

/* The amalgamation is third-party code: keep the build log about our code. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#include "sqlite3_amalgamation.h"
#pragma GCC diagnostic pop

/* ------------------------------------------------------------------- VFS */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    sqlite3_file base;
    int fd;
    char *delete_path;          /* temp file: removed on close */
} esp_file_t;

static int esp_close(sqlite3_file *f)
{
    esp_file_t *p = (esp_file_t *)f;
    if (p->fd >= 0) {
        close(p->fd);
    }
    p->fd = -1;
    if (p->delete_path) {
        unlink(p->delete_path);
        sqlite3_free(p->delete_path);
        p->delete_path = NULL;
    }
    return SQLITE_OK;
}

static int esp_read(sqlite3_file *f, void *buf, int amt, sqlite3_int64 ofst)
{
    esp_file_t *p = (esp_file_t *)f;
    /* Never seek past the end to read: on a file open for writing, FatFs
     * f_lseek() beyond EOF grows the file (SQLite reads offset 24 of a new,
     * empty database, which would leave 24 zero bytes behind). */
    struct stat st;
    if (fstat(p->fd, &st) != 0) {
        return SQLITE_IOERR_READ;
    }
    if (ofst >= st.st_size) {
        memset(buf, 0, amt);
        return SQLITE_IOERR_SHORT_READ;
    }
    if (lseek(p->fd, (off_t)ofst, SEEK_SET) != (off_t)ofst) {
        return SQLITE_IOERR_READ;
    }
    int got = 0;
    while (got < amt) {
        ssize_t n = read(p->fd, (char *)buf + got, amt - got);
        if (n < 0) {
            return SQLITE_IOERR_READ;
        }
        if (n == 0) {
            break;
        }
        got += (int)n;
    }
    if (got < amt) {
        memset((char *)buf + got, 0, amt - got);   /* required on a short read */
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int esp_write(sqlite3_file *f, const void *buf, int amt, sqlite3_int64 ofst)
{
    esp_file_t *p = (esp_file_t *)f;
    if (lseek(p->fd, (off_t)ofst, SEEK_SET) != (off_t)ofst) {
        return SQLITE_IOERR_WRITE;    /* may extend: fine */
    }
    int put = 0;
    while (put < amt) {
        ssize_t n = write(p->fd, (const char *)buf + put, amt - put);
        if (n <= 0) {
            return n == 0 ? SQLITE_FULL : SQLITE_IOERR_WRITE;
        }
        put += (int)n;
    }
    return SQLITE_OK;
}

static int esp_truncate(sqlite3_file *f, sqlite3_int64 size)
{
    esp_file_t *p = (esp_file_t *)f;
    return ftruncate(p->fd, (off_t)size) == 0 ? SQLITE_OK : SQLITE_IOERR_TRUNCATE;
}

static int esp_sync(sqlite3_file *f, int flags)
{
    esp_file_t *p = (esp_file_t *)f;
    return fsync(p->fd) == 0 ? SQLITE_OK : SQLITE_IOERR_FSYNC;
}

static int esp_file_size(sqlite3_file *f, sqlite3_int64 *size)
{
    esp_file_t *p = (esp_file_t *)f;
    struct stat st;
    if (fstat(p->fd, &st) != 0) {
        return SQLITE_IOERR_FSTAT;
    }
    *size = st.st_size;
    return SQLITE_OK;
}

/* single process, single connection: nothing to lock against */
static int esp_lock(sqlite3_file *f, int level)                 { return SQLITE_OK; }
static int esp_unlock(sqlite3_file *f, int level)               { return SQLITE_OK; }
static int esp_check_reserved(sqlite3_file *f, int *out)        { *out = 0; return SQLITE_OK; }
static int esp_file_control(sqlite3_file *f, int op, void *arg) { return SQLITE_NOTFOUND; }
static int esp_sector_size(sqlite3_file *f)                     { return 512; }
static int esp_device_chars(sqlite3_file *f)                    { return 0; }

static const sqlite3_io_methods esp_io = {
    1,
    esp_close, esp_read, esp_write, esp_truncate, esp_sync, esp_file_size,
    esp_lock, esp_unlock, esp_check_reserved, esp_file_control,
    esp_sector_size, esp_device_chars,
};

static int esp_open(sqlite3_vfs *vfs, sqlite3_filename name, sqlite3_file *f, int flags, int *out_flags)
{
    esp_file_t *p = (esp_file_t *)f;
    memset(p, 0, sizeof(*p));
    p->fd = -1;

    char tmp[64];
    if (!name) {                                   /* temp file next to the data */
        static unsigned n;
        snprintf(tmp, sizeof(tmp), "%s/.sqlite_tmp%u", (const char *)vfs->pAppData, n++);
        name = tmp;
        flags |= SQLITE_OPEN_DELETEONCLOSE | SQLITE_OPEN_CREATE;
    }
    int oflags = (flags & SQLITE_OPEN_READWRITE) ? O_RDWR : O_RDONLY;
    if (flags & SQLITE_OPEN_CREATE) {
        oflags |= O_CREAT;
    }
    if (flags & SQLITE_OPEN_EXCLUSIVE) {
        oflags |= O_EXCL;
    }
    p->fd = open(name, oflags, 0644);
    if (p->fd < 0) {
        return SQLITE_CANTOPEN;
    }
    if (flags & SQLITE_OPEN_DELETEONCLOSE) {
        p->delete_path = sqlite3_mprintf("%s", name);
    }
    if (out_flags) {
        *out_flags = flags;
    }
    p->base.pMethods = &esp_io;
    return SQLITE_OK;
}

static int esp_delete(sqlite3_vfs *vfs, const char *name, int sync_dir)
{
    if (unlink(name) == 0) {
        return SQLITE_OK;
    }
    struct stat st;
    return stat(name, &st) != 0 ? SQLITE_IOERR_DELETE_NOENT : SQLITE_IOERR_DELETE;
}

static int esp_access(sqlite3_vfs *vfs, const char *name, int flags, int *out)
{
    struct stat st;
    *out = stat(name, &st) == 0;                   /* FAT has no per-user permissions */
    return SQLITE_OK;
}

static int esp_full_pathname(sqlite3_vfs *vfs, const char *name, int n, char *out)
{
    sqlite3_snprintf(n, out, "%s", name);          /* callers pass absolute VFS paths */
    return SQLITE_OK;
}

static int esp_randomness(sqlite3_vfs *vfs, int n, char *out)
{
    esp_fill_random(out, n);
    return n;
}

static int esp_sleep(sqlite3_vfs *vfs, int us)
{
    vTaskDelay(pdMS_TO_TICKS((us + 999) / 1000) ? pdMS_TO_TICKS((us + 999) / 1000) : 1);
    return us;
}

static int esp_current_time_i64(sqlite3_vfs *vfs, sqlite3_int64 *out)
{
    /* milliseconds since the Julian epoch */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *out = (sqlite3_int64)tv.tv_sec * 1000 + tv.tv_usec / 1000 + 210866760000000LL;
    return SQLITE_OK;
}

static int esp_current_time(sqlite3_vfs *vfs, double *out)
{
    sqlite3_int64 ms;
    esp_current_time_i64(vfs, &ms);
    *out = ms / 86400000.0;
    return SQLITE_OK;
}

static int esp_last_error(sqlite3_vfs *vfs, int n, char *out) { return 0; }

static sqlite3_vfs esp_vfs = {
    2,                          /* iVersion: has xCurrentTimeInt64 */
    sizeof(esp_file_t),
    128,                        /* mxPathname */
    NULL,
    "esp",
    (void *)"/sdcard",          /* pAppData: where temp files go */
    esp_open, esp_delete, esp_access, esp_full_pathname,
    NULL, NULL, NULL, NULL,     /* no loadable extensions */
    esp_randomness, esp_sleep, esp_current_time, esp_last_error,
    esp_current_time_i64,
};

int sqlite3_os_init(void)
{
    return sqlite3_vfs_register(&esp_vfs, 1);
}

int sqlite3_os_end(void)
{
    return SQLITE_OK;
}
