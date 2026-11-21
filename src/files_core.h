/**
 * @file files_core.h
 * @ingroup files_core
 * @brief The Files page's work on the microSD card, without LVGL: one header for its parts (the
 *        page: ui_file.c).
 */
#pragma once
#include "files_folder.h"
#include "files_names.h"
#include "files_trash.h"
#include "files_preview.h"

/**
 * @defgroup files_core Files core
 * @ingroup topic_files
 * @brief What the Files page does on the card, apart from showing it.
 *
 * - **The folder shown** (files_path()) and moving in it: files_enter(), files_up().
 * - **Reading it**: a short worker task on core 0, away from LVGL: stat() of each entry walks the
 *   FAT directory, which adds up in a folder of hundreds of replays. Start it with files_scan(),
 *   poll files_take_scan(), then read files_count() / files_entry().
 * - **Rename** and **Delete** (to the Recycle bin): single calls, on the spot.
 * - **The Recycle bin**: deleting moves things into FILES_TRASH_DIR, named after where they were
 *   ("replays/a.mp4" -> "replays%2Fa.mp4", "%" -> "%25"; "2~" in front if that name is taken), so
 *   files_restore() can put them back. Deleting there, or Empty, removes them for good: by a task
 *   (files_purge()), as a folder can hold many files.
 * - **The game's database** (sudoku.db and SQLite's files next to it, in the root) is protected:
 *   it can't be renamed, deleted or replaced by a rename (is_files_protected()).
 * - **A puzzle file's preview**: the file read and its puzzles found (files_pv_load()), one parsed
 *   at a time (files_pv_parse()).
 *
 * | Files                  | Part |
 * |------------------------|------|
 * | files_folder.c / .h    | @ref files_folder "The folder": the folder shown, reading it, its entries |
 * | files_names.c / .h     | @ref files_names "Names": protected names, a new name checked, rename, sizes |
 * | files_trash.c / .h     | @ref files_trash "Recycle bin": trash, restore, deleting for good |
 * | files_preview.c / .h   | @ref files_preview "Puzzle file preview": the file read, its puzzles |
 *
 * Include files_core.h for all of them.
 *
 * Everything goes through the POSIX file API on the card's FAT volume (BSP_SD_MOUNT_POINT), which
 * stats_db keeps mounted.
 */
