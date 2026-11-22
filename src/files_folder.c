/**
 * @file files_folder.c
 * @ingroup files_folder
 * @brief The folder shown, and reading it (a worker task).
 */
#include "files_folder.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static char s_path[FILES_PATH_MAX] = BSP_SD_MOUNT_POINT;   /**< the folder shown */

static files_entry_t *s_ent;            /**< the entries read (FILES_MAX_ENTRIES, PSRAM) */
static volatile int s_n;                /**< entries found; -1: the folder couldn't be opened */
static volatile bool s_more;            /**< more than FILES_MAX_ENTRIES */
static volatile bool s_busy;            /**< the scan worker is reading */
static volatile bool s_done;            /**< a read has finished (files_take_scan()) */
static char s_scan_path[FILES_PATH_MAX];   /**< the folder the worker reads */

/* ------------------------------------------------------------- the folder shown */
/* (documented in files_folder.h) */
const char *files_path(void)
{
    return s_path;
}

/* (documented in files_folder.h) */
bool is_files_at_root(void)
{
    return strcmp(s_path, BSP_SD_MOUNT_POINT) == 0;
}

/* (documented in files_folder.h) */
bool is_files_in_trash(void)
{
    return strcmp(s_path, FILES_TRASH_DIR) == 0;
}

/* (documented in files_folder.h) */
bool files_enter(const char *name)
{
    size_t len = strlen(s_path);
    if (len + 1 + strlen(name) >= sizeof(s_path)) {
        return false;
    }
    snprintf(s_path + len, sizeof(s_path) - len, "/%s", name);
    return true;
}

/* (documented in files_folder.h) */
void files_up(void)
{
    char *slash = strrchr(s_path, '/');
    if (slash && slash != s_path) {
        *slash = 0;
    }
}

/* ------------------------------------------------------------- reading the folder */
/**
 * @brief qsort's order: folders first, then by name (any case).
 * @param[in] a  an entry
 * @param[in] b  another
 * @return \<0, 0, \>0
 */
static int entry_cmp(const void *a, const void *b)
{
    const files_entry_t *x = a, *y = b;
    if (x->dir != y->dir) {
        return x->dir ? -1 : 1;    /* folders first */
    }
    return strcasecmp(x->name, y->name);
}

/**
 * @brief The scan worker: reads s_scan_path into s_ent, sorted, then ends.
 * @param[in] arg  unused
 */
static void scan_task(void *arg)
{
    int n = 0;
    bool more = false;
    DIR *d = opendir(s_scan_path);
    if (!d) {
        n = -1;
    } else {
        struct dirent *de;
        char full[FILES_PATH_MAX + FILES_NAME_MAX];
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
                continue;
            }
            if (n >= FILES_MAX_ENTRIES) {
                more = true;
                break;
            }
            files_entry_t *e = &s_ent[n];
            strlcpy(e->name, de->d_name, sizeof(e->name));
            e->dir = de->d_type == DT_DIR;
            e->size = 0;
            e->mtime = 0;
            snprintf(full, sizeof(full), "%s/%s", s_scan_path, de->d_name);
            struct stat st;
            if (stat(full, &st) == 0) {
                e->dir = S_ISDIR(st.st_mode);
                e->size = (uint32_t)st.st_size;
                e->mtime = st.st_mtime;
            }
            n++;
        }
        closedir(d);
        qsort(s_ent, (size_t)n, sizeof(files_entry_t), entry_cmp);
    }
    s_n = n;
    s_more = more;
    s_done = true;
    s_busy = false;
    vTaskDelete(NULL);
}

/* (documented in files_folder.h) */
bool files_init(void)
{
    if (!s_ent) {
        s_ent = heap_caps_calloc(FILES_MAX_ENTRIES, sizeof(files_entry_t), MALLOC_CAP_SPIRAM);
    }
    return s_ent != NULL;
}

/* (documented in files_folder.h) */
files_scan_t files_scan(void)
{
    if (s_busy || !s_ent) {
        return FILES_SCAN_NOT_NOW;
    }
    strlcpy(s_scan_path, s_path, sizeof(s_scan_path));
    s_busy = true;
    s_done = false;
    if (xTaskCreatePinnedToCore(scan_task, "files", 6144, NULL, 2, NULL, 0) != pdPASS) {
        s_busy = false;
        return FILES_SCAN_NO_MEMORY;
    }
    return FILES_SCAN_STARTED;
}

/* (documented in files_folder.h) */
bool files_take_scan(void)
{
    if (!s_done) {
        return false;
    }
    s_done = false;
    return true;
}

/* (documented in files_folder.h) */
bool is_files_scanning(void)
{
    return s_busy;
}

/* (documented in files_folder.h) */
int files_count(void)
{
    return s_n;
}

/* (documented in files_folder.h) */
bool is_files_more(void)
{
    return s_more;
}

/* (documented in files_folder.h) */
const files_entry_t *files_entry(int i)
{
    return &s_ent[i];
}
