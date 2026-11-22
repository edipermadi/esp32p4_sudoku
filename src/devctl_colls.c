/**
 * @file devctl_colls.c
 * @ingroup devctl
 * @brief colls, open, colldel: the collections (Puzzles > Built-in, Saved, Imported) listed with their ids, a
 *        puzzle of one opened, a saved or imported one deleted.
 */
#include "devctl_internal.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "stats_db.h"
#include "puzzles.h"
#include "collections.h"
#include "ui_sudoku.h"

/**
 * @brief Sends a collection's name with %XX for spaces, '%' and control bytes (as ls does a file's).
 * @param[in] s  the name
 */
static void print_coded(const char *s)
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

/* (documented in devctl_internal.h) */
void devctl_colls(void)
{
    if (!is_stats_db_ready()) {
        devctl_reply("ERR colls: no sudoku.db\n");
        return;
    }
    static stats_import_t imp[STATS_MAX_IMPORTS];
    stats_saved_t *sav = (stats_saved_t *)heap_caps_calloc(STATS_MAX_SAVED, sizeof(stats_saved_t), MALLOC_CAP_SPIRAM);
    if (!sav) {
        devctl_reply("ERR colls: no memory\n");
        return;
    }
    int ni = stats_db_list_imports(imp), ns = stats_db_list_saved(sav), n = 0;
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++, n++) {   /* the tabs' order: built-in, saved, imported */
        serial_printf("COLL builtin %d %d ", c + 1, puzzle_collection(c)->count);
        print_coded(puzzle_collection(c)->name);
        serial_write_byte('\n');
    }
    for (int i = 0; i < ns; i++, n++) {
        serial_printf("COLL saved %d %d ", sav[i].id, sav[i].count);
        print_coded(sav[i].name);
        serial_write_byte('\n');
    }
    for (int i = 0; i < ni; i++, n++) {
        serial_printf("COLL imported %d %d ", imp[i].id, imp[i].count);
        print_coded(imp[i].name);
        serial_write_byte('\n');
    }
    heap_caps_free(sav);
    devctl_reply("OK colls %d\n", n);
}

/**
 * @brief A collection's tab from its kind as devctl names it
 * @param[in] kind  "builtin", "saved" or "imported"
 * @return COLL_TAB_*, or -1
 */
static int kind_tab(const char *kind)
{
    static const char *const KIND[COLL_TABS] = {"builtin", "saved", "imported"};   /* (COLL_TAB_*'s order) */
    for (int t = 0; t < COLL_TABS; t++) {
        if (!strcmp(kind, KIND[t])) {
            return t;
        }
    }
    return -1;
}

/* (documented in devctl_internal.h) */
void devctl_open(const char *line)
{
    char kind[16] = "";
    int id = 0, number = 0;
    const int tab = sscanf(line, "%*s %15s %d %d", kind, &id, &number) == 3 ? kind_tab(kind) : -1;
    if (tab < 0) {
        devctl_reply("ERR open: open builtin|saved|imported <collection id> <number> (ids: colls)\n");
        return;
    }
    char msg[96];
    if (sudoku_ui_open_puzzle(tab, id, number, msg, sizeof(msg))) {
        devctl_reply("OK open %s\n", msg);
    } else {
        devctl_reply("ERR open: %s\n", msg);
    }
}

/* (documented in devctl_internal.h) */
void devctl_colldel(const char *line)
{
    char kind[16] = "";
    int id = 0;
    const int tab = sscanf(line, "%*s %15s %d", kind, &id) == 2 ? kind_tab(kind) : -1;
    if (tab != COLL_TAB_SAVED && tab != COLL_TAB_IMPORTED) {
        devctl_reply("ERR colldel: colldel saved|imported <collection id> (ids: colls)\n");
        return;
    }
    const char *busy = sudoku_ui_io_busy();
    if (busy) {
        devctl_reply("ERR colldel: %s\n", busy);
        return;
    }
    char msg[96];
    if (sudoku_ui_coll_delete(tab, id, msg, sizeof(msg))) {
        devctl_reply("OK colldel %s\n", msg);
    } else {
        devctl_reply("ERR colldel: %s\n", msg);
    }
}
