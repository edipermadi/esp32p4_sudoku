/**
 * @file devctl_colls.c
 * @ingroup devctl
 * @brief colls, colldel: the saved and imported collections (Puzzles > Saved, Imported) listed, and one deleted.
 */
#include "devctl_internal.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "stats_db.h"
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

/**
 * @brief A hex digit's value.
 * @param[in] c  the character
 * @return 0..15, or -1 if it isn't one
 */
static int unhex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
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
    for (int i = 0; i < ni; i++, n++) {             /* imported, then saved */
        serial_printf("COLL imported %d ", imp[i].count);
        print_coded(imp[i].name);
        serial_write_byte('\n');
    }
    for (int i = 0; i < ns; i++, n++) {
        serial_printf("COLL saved %d ", sav[i].count);
        print_coded(sav[i].name);
        serial_write_byte('\n');
    }
    heap_caps_free(sav);
    devctl_reply("OK colls %d\n", n);
}

/**
 * @brief A collection's name as sent: %XX for a space, % or a control byte (as paths are)
 * @param[in] word  the name as sent
 * @param[out] name  gets it
 * @param[in] size  its size
 */
static void decode_name(const char *word, char *name, size_t size)
{
    size_t k = 0;
    for (const char *p = word; *p && k + 1 < size; p++) {
        char c = *p;
        if (c == '%' && unhex(p[1]) >= 0 && unhex(p[2]) >= 0) {
            c = (char)(unhex(p[1]) << 4 | unhex(p[2]));
            p += 2;
        }
        name[k++] = c;
    }
    name[k] = 0;
}

/* (documented in devctl_internal.h) */
void devctl_open(const char *line)
{
    static const char *const KIND[] = {"builtin", "saved", "imported"};   /* (COLL_TAB_*'s order) */
    char kind[16] = "", word[3 * STATS_IMPORT_NAME + 1] = "", name[STATS_IMPORT_NAME];
    int number = 0, tab = -1;
    if (sscanf(line, "%*s %15s %84s %d", kind, word, &number) == 3) {
        for (int t = 0; t < 3; t++) {
            tab = strcmp(kind, KIND[t]) ? tab : t;
        }
    }
    if (tab < 0) {
        devctl_reply("ERR open: open builtin|saved|imported <collection> <number> (%%XX for a space)\n");
        return;
    }
    decode_name(word, name, sizeof(name));
    char msg[96];
    if (sudoku_ui_open_puzzle(tab, name, number, msg, sizeof(msg))) {
        devctl_reply("OK open %s\n", msg);
    } else {
        devctl_reply("ERR open: %s\n", msg);
    }
}

/* (documented in devctl_internal.h) */
void devctl_colldel(const char *line)
{
    char kind[16] = "", word[3 * STATS_IMPORT_NAME + 1] = "", name[STATS_IMPORT_NAME];
    if (sscanf(line, "%*s %15s %84s", kind, word) != 2 || (strcmp(kind, "saved") && strcmp(kind, "imported"))) {
        devctl_reply("ERR colldel: colldel saved|imported <name> (%%XX for a space)\n");
        return;
    }
    decode_name(word, name, sizeof(name));
    const char *busy = sudoku_ui_io_busy();
    if (busy) {
        devctl_reply("ERR colldel: %s\n", busy);
        return;
    }
    char msg[96];
    if (sudoku_ui_coll_delete(!strcmp(kind, "saved"), name, msg, sizeof(msg))) {
        devctl_reply("OK colldel\n");
    } else {
        devctl_reply("ERR colldel: %s\n", msg);
    }
}
