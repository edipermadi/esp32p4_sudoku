/**
 * @file files_trash.h
 * @ingroup files_trash
 * @brief The Recycle bin: deleting moves things into FILES_TRASH_DIR, named after where they were, so
 *        they can be restored; deleting there, or Empty, removes them for good (by a task).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup files_trash Recycle bin
 * @ingroup files_core
 * @brief The Recycle bin: deleting moves things into FILES_TRASH_DIR, named after where they were, so
 *        they can be restored; deleting there, or Empty, removes them for good (by a task).
 *
 * Names in the bin: "replays/a.mp4" -> "replays%2Fa.mp4", "%" -> "%25"; "2~" in front if that name
 * is taken.
 * @{
 */

/**
 * @brief The Recycle bin's name of an item -> where it was, relative to the card's root.
 * @param[in] name  its name in the bin
 * @param[out] out   gets the path
 * @param[in] n     its size
 */
void files_trash_decode(const char *name, char *out, size_t n);
/**
 * @brief Moves an entry of the folder shown into the Recycle bin (made if it isn't there).
 * @param[in] name  its name
 * @param[out] err   gets why not, on false
 * @param[in] n     its size
 * @return false if it couldn't be moved
 */
bool files_trash(const char *name, char *err, size_t n);
/** @brief What files_restore() did. */
typedef enum {
    FILES_RESTORED = 0,                   /**< put back */
    FILES_RESTORE_PROTECTED,              /**< a root name of the database: never */
    FILES_RESTORE_TAKEN,                  /**< something is already where it was */
    FILES_RESTORE_FAILED                  /**< the rename failed */
} files_restore_t;
/**
 * @brief Puts a Recycle bin item back where it was (making the folders on the way if they're gone).
 * @param[in] name  its name in the bin
 * @return what happened
 */
files_restore_t files_restore(const char *name);
/** @brief What files_purge() did. */
typedef enum {
    FILES_PURGE_BUSY = 0,                 /**< a purge is running already: nothing started */
    FILES_PURGE_STARTED,                  /**< the task is deleting */
    FILES_PURGE_NO_TASK                   /**< the task couldn't be started */
} files_purge_t;
/**
 * @brief Deletes for good, on a task: a Recycle bin item, or everything in the bin.
 * @param[in] name  the item's name in the bin; NULL: Empty (every item)
 * @return what happened
 */
files_purge_t files_purge(const char *name);
/**
 * @brief Whether a purge has finished since the last call (once).
 * @return true: read the bin again
 */
bool files_take_purged(void);
/**
 * @brief Whether a purge is running.
 * @return true: running
 */
bool is_files_purging(void);

/** @} */

#ifdef __cplusplus
}
#endif
