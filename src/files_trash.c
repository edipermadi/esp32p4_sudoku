/**
 * @file files_trash.c
 * @ingroup files_trash
 * @brief The Recycle bin: trash, restore, and deleting for good (a task).
 */
#include "files_trash.h"
#include "files_folder.h"
#include "files_names.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static volatile bool s_job_busy;        /**< the purge task is deleting */
static volatile bool s_job_done;        /**< a purge has finished (files_take_purged()) */
static char s_job_path[FILES_PATH_MAX + FILES_NAME_MAX];   /**< what it deletes; "" = every item in the bin */
static char s_job_msg[96];              /**< how the last purge went, if some couldn't be deleted (not shown yet) */

/* ------------------------------------------------------------- the Recycle bin's names */
/**
 * @brief A path as one name in the Recycle bin: "replays/a.mp4" -> "replays%2Fa.mp4".
 * @param rel  the path, relative to the card's root
 * @param out  gets the name
 * @param n    its size
 * @return false if it doesn't fit a FAT name
 */
static bool trash_encode(const char *rel, char *out, size_t n)
{
    size_t k = 0;
    for (const char *p = rel; *p; p++) {
        const char *put = *p == '/' ? "%2F" : (*p == '%' ? "%25" : NULL);
        size_t need = put ? 3 : 1;
        if (k + need >= n || k + need > 240) {
            return false;    /* (room for a "99~" in front) */
        }
        if (put) {
            memcpy(out + k, put, 3);
        } else {
            out[k] = *p;
        }
        k += need;
    }
    out[k] = 0;
    return k > 0;
}

/* (documented in files_trash.h) */
void files_trash_decode(const char *name, char *out, size_t n)
{
    const char *p = name;
    while (*p >= '0' && *p <= '9') {
        p++;
    }
    if (p != name && *p == '~') {
        name = p + 1;    /* a "2~" added for a taken name */
    }
    size_t k = 0;
    for (p = name; *p && k + 1 < n; p++) {
        if (p[0] == '%' && p[1] == '2' && (p[2] == 'F' || p[2] == 'f')) {
            out[k++] = '/';
            p += 2;
        } else if (p[0] == '%' && p[1] == '2' && p[2] == '5') {
            out[k++] = '%';
            p += 2;
        } else {
            out[k++] = *p;
        }
    }
    out[k] = 0;
}

/* ------------------------------------------------------------- deleting for good */
/**
 * @brief Deletes a path, and everything in it if it is a folder (the purge task).
 * @param path  the path
 * @return 0, or -1 on failure
 */
static int rm_rf(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }
    DIR *d = opendir(path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
                continue;
            }
            char *child = malloc(strlen(path) + strlen(de->d_name) + 2);
            if (!child) {
                break;
            }
            sprintf(child, "%s/%s", path, de->d_name);
            rm_rf(child);
            free(child);
        }
        closedir(d);
    }
    return rmdir(path);
}

/**
 * @brief The purge task: deletes s_job_path, or every item in the bin, then ends.
 * @param arg  unused
 */
static void job_task(void *arg)
{
    int n = 0, failed = 0;
    if (s_job_path[0]) {                           /* one item */
        if (rm_rf(s_job_path) == 0) {
            n++;
        } else {
            failed++;
        }
    } else {                                        /* Empty: everything in the bin */
        DIR *d = opendir(FILES_TRASH_DIR);
        if (d) {
            struct dirent *de;
            static char p[FILES_PATH_MAX + FILES_NAME_MAX];
            while ((de = readdir(d)) != NULL) {
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
                    continue;
                }
                snprintf(p, sizeof(p), "%s/%s", FILES_TRASH_DIR, de->d_name);
                if (rm_rf(p) == 0) {
                    n++;
                } else {
                    failed++;
                }
            }
            closedir(d);
        }
    }
    if (failed) {
        snprintf(s_job_msg, sizeof(s_job_msg), "Deleted %d; %d couldn't be.", n, failed);
    } else {
        s_job_msg[0] = 0;
    }
    printf("files: deleted for good: %d (%d failed)\n", n, failed);
    s_job_done = true;
    s_job_busy = false;
    vTaskDelete(NULL);
}

/* (documented in files_trash.h) */
files_purge_t files_purge(const char *name)
{
    if (s_job_busy) {
        return FILES_PURGE_BUSY;
    }
    if (name) {
        snprintf(s_job_path, sizeof(s_job_path), "%s/%s", FILES_TRASH_DIR, name);
    } else {
        s_job_path[0] = 0;
    }
    s_job_busy = true;
    if (xTaskCreatePinnedToCore(job_task, "files_rm", 8192, NULL, 2, NULL, 0) != pdPASS) {
        s_job_busy = false;
        return FILES_PURGE_NO_TASK;
    }
    return FILES_PURGE_STARTED;
}

/* (documented in files_trash.h) */
bool files_take_purged(void)
{
    if (!s_job_done) {
        return false;
    }
    s_job_done = false;
    return true;
}

/* (documented in files_trash.h) */
bool is_files_purging(void)
{
    return s_job_busy;
}

/* (documented in files_trash.h) */
bool files_trash(const char *name, char *err, size_t n)
{
    char rel[FILES_PATH_MAX + FILES_NAME_MAX], enc[FILES_NAME_MAX], from[FILES_PATH_MAX + FILES_NAME_MAX],
         to[FILES_PATH_MAX + FILES_NAME_MAX];
    snprintf(rel, sizeof(rel), "%s/%s", files_path() + strlen(BSP_SD_MOUNT_POINT) + (is_files_at_root() ? 0 : 1), name);
    if (rel[0] == '/') {
        memmove(rel, rel + 1, strlen(rel));
    }
    if (!trash_encode(rel, enc, sizeof(enc))) {
        snprintf(err, n, "Its path is too long for the Recycle bin.");
        return false;
    }
    mkdir(FILES_TRASH_DIR, 0775);                   /* (already there: fine) */
    snprintf(from, sizeof(from), "%s/%s", files_path(), name);
    struct stat st;
    snprintf(to, sizeof(to), "%s/%s", FILES_TRASH_DIR, enc);
    for (int k = 2; stat(to, &st) == 0 && k < 100; k++) {   /* that name is taken in the bin */
        snprintf(to, sizeof(to), "%s/%d~%s", FILES_TRASH_DIR, k, enc);
    }
    if (rename(from, to) != 0) {
        snprintf(err, n, "Couldn't move it to the Recycle bin (%s).", strerror(errno));
        return false;
    }
    printf("files: %s -> %s\n", from, to);
    return true;
}

/* (documented in files_trash.h) */
files_restore_t files_restore(const char *name)
{
    char rel[FILES_PATH_MAX], from[FILES_PATH_MAX + FILES_NAME_MAX], to[FILES_PATH_MAX + FILES_NAME_MAX];
    files_trash_decode(name, rel, sizeof(rel));
    snprintf(from, sizeof(from), "%s/%s", FILES_TRASH_DIR, name);
    snprintf(to, sizeof(to), "%s/%s", BSP_SD_MOUNT_POINT, rel);
    const char *base = strrchr(rel, '/') ? strrchr(rel, '/') + 1 : rel;
    if (base == rel && is_files_protected(rel)) {
        return FILES_RESTORE_PROTECTED;    /* (a root name of the database: never) */
    }
    struct stat st;
    if (stat(to, &st) == 0) {
        char msg[FILES_PATH_MAX + 48];
        snprintf(msg, sizeof(msg), "Can't restore: something is already at %s.", rel);
        printf("files: %s\n", msg);
        return FILES_RESTORE_TAKEN;
    }
    for (char *p = to + strlen(BSP_SD_MOUNT_POINT) + 1; (p = strchr(p, '/')) != NULL; p++) {
        *p = 0;                                     /* its folders, if they were deleted since */
        mkdir(to, 0775);
        *p = '/';
    }
    if (rename(from, to) != 0) {
        printf("files: restore %s failed (%s)\n", rel, strerror(errno));
        return FILES_RESTORE_FAILED;
    }
    printf("files: restored %s\n", to);
    return FILES_RESTORED;
}
