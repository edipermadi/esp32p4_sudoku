/**
 * @file files_folder.h
 * @ingroup files_folder
 * @brief The folder shown, and reading it: a short worker task on core 0 (stat() of each entry walks the FAT
 *        directory, which adds up in a folder of hundreds of replays).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "bsp/bsp_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup files_folder The folder
 * @ingroup files_core
 * @brief The folder shown, and reading it: a short worker task on core 0 (stat() of each entry walks the FAT
 *        directory, which adds up in a folder of hundreds of replays).
 * @{
 */

#define FILES_MAX_ENTRIES  200            /**< listed per folder (sorted: folders, then A-Z) */
#define FILES_NAME_MAX     256            /**< FAT long names: up to 255 bytes of UTF-8 */
#define FILES_PATH_MAX     512            /**< a folder's path */
#define FILES_TRASH_NAME   ".trash"       /**< the Recycle bin's folder, in the card's root */
#define FILES_TRASH_DIR    BSP_SD_MOUNT_POINT "/" FILES_TRASH_NAME   /**< its path */

/** @brief An entry of the folder read. */
typedef struct {
    char name[FILES_NAME_MAX];            /**< its name */
    bool dir;                             /**< a folder */
    uint32_t size;                        /**< bytes (files) */
    time_t mtime;                         /**< last changed (0: unknown) */
} files_entry_t;

/**
 * @brief The folder shown (the card's root at first).
 * @return its path
 */
const char *files_path(void);
/**
 * @brief Whether the folder shown is the card's root.
 * @return true: it is
 */
bool is_files_at_root(void);
/**
 * @brief Whether the folder shown is the Recycle bin.
 * @return true: it is
 */
bool is_files_in_trash(void);
/**
 * @brief Opens a folder in the folder shown.
 * @param name  its name
 * @return false if the path would be too long (nothing changes)
 */
bool files_enter(const char *name);
/** @brief The folder above (not above the card's root). */
void files_up(void);

/**
 * @brief Makes the entries' buffer (PSRAM), the first time.
 * @return false if out of memory (files_scan() then does nothing)
 */
bool files_init(void);
/** @brief What files_scan() did. */
typedef enum {
    FILES_SCAN_NOT_NOW = 0,               /**< already reading, or no buffer: nothing started */
    FILES_SCAN_STARTED,                   /**< the worker is reading the folder shown */
    FILES_SCAN_NO_MEMORY                  /**< the worker couldn't be started */
} files_scan_t;
/**
 * @brief Reads the folder shown again, on a worker task.
 * @return what happened
 */
files_scan_t files_scan(void);
/**
 * @brief Whether a read has finished since the last call (once).
 * @return true: show files_count() entries
 */
bool files_take_scan(void);
/**
 * @brief Whether the worker is reading (it is rewriting the entries meanwhile).
 * @return true: reading
 */
bool is_files_scanning(void);
/**
 * @brief The entries read.
 * @return how many; -1: the folder couldn't be opened
 */
int files_count(void);
/**
 * @brief Whether the folder had more than FILES_MAX_ENTRIES entries (only the first are listed).
 * @return true: more
 */
bool is_files_more(void);
/**
 * @brief An entry read.
 * @param i  0 .. files_count() - 1
 * @return it
 */
const files_entry_t *files_entry(int i);

/** @} */

#ifdef __cplusplus
}
#endif
