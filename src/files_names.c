/**
 * @file files_names.c
 * @ingroup files_names
 * @brief Names in the folder shown: the game's database protected, a new name checked, rename, sizes.
 */
#include "files_names.h"
#include "files_folder.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>

/** The game's database and SQLite's files next to it, in the card's root (is_files_protected()). */
static const char *const PROTECTED[] = {"sudoku.db", "sudoku.db-journal", "sudoku.db-wal", "sudoku.db-shm"};

/* ------------------------------------------------------------- names */
/* (documented in files_names.h) */
bool is_files_protected(const char *name)
{
    if (!is_files_at_root()) {
        return false;
    }
    for (size_t i = 0; i < sizeof(PROTECTED) / sizeof(PROTECTED[0]); i++) {
        if (strcasecmp(name, PROTECTED[i]) == 0) {
            return true;    /* FAT ignores case */
        }
    }
    return false;
}

/* (documented in files_names.h) */
void files_fmt_size(char *buf, size_t n, uint32_t b)
{
    if (b < 1024) {
        snprintf(buf, n, "%u B", (unsigned)b);
    } else if (b < 1024 * 1024) {
        snprintf(buf, n, "%u.%u KB", (unsigned)(b / 1024), (unsigned)(b % 1024 * 10 / 1024));
    } else {
        uint32_t k = b / 1024;
        snprintf(buf, n, "%u.%u MB", (unsigned)(k / 1024), (unsigned)(k % 1024 * 10 / 1024));
    }
}

/* (documented in files_names.h) */
const char *files_bad_name(const char *name, const char *old)
{
    size_t len = strlen(name);
    if (len == 0) {
        return "Type a name.";
    }
    if (len >= FILES_NAME_MAX) {
        return "That name is too long.";
    }
    if (!strcmp(name, ".") || !strcmp(name, "..")) {
        return "That name can't be used.";
    }
    if (strpbrk(name, "\\/:*?\"<>|")) {
        return "A name can't have \\ / : * ? \" < > |";
    }
    if (name[0] == ' ' || name[len - 1] == ' ' || name[len - 1] == '.') {
        return "A name can't start with a space or end with a space or a dot.";
    }
    if (is_files_protected(name)) {
        return "That name belongs to the game's database.";
    }
    if (strcasecmp(name, old) != 0) {             /* (only the case changes: the same file) */
        char full[FILES_PATH_MAX + FILES_NAME_MAX];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", files_path(), name);
        if (stat(full, &st) == 0) {
            return "Something here already has that name.";
        }
    }
    return "";
}

/* (documented in files_names.h) */
bool is_files_name_ending(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

/* (documented in files_names.h) */
bool is_files_puzzle_file(const char *name)
{
    return is_files_name_ending(name, ".sdm") || is_files_name_ending(name, ".sdx") || is_files_name_ending(name, ".opensudoku");
}

/* ------------------------------------------------------------- changing things */
/* (documented in files_names.h) */
bool files_rename(const char *old, const char *name, char *err, size_t n)
{
    char from[FILES_PATH_MAX + FILES_NAME_MAX], to[FILES_PATH_MAX + FILES_NAME_MAX];
    snprintf(from, sizeof(from), "%s/%s", files_path(), old);
    snprintf(to, sizeof(to), "%s/%s", files_path(), name);
    if (rename(from, to) != 0) {
        snprintf(err, n, "Couldn't rename it (%s).", strerror(errno));
        return false;
    }
    printf("files: renamed %s -> %s\n", from, to);
    return true;
}
