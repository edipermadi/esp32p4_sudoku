/**
 * @file puzzles_colls.c
 * @ingroup puzzles_colls
 * @brief The player's collections on the Puzzles screen: deleting one, exporting one, taking a puzzle
 *        out of one.
 */
#include "puzzles_colls.h"
#include "collections.h"
#include "puzzles.h"
#include <stdio.h>

/* (documented in puzzles_colls.h) */
bool puzzles_delete_coll(int c, char *name, size_t n)
{
    int k = coll_saved_index(c);
    snprintf(name, n, "%s", coll_name(c));
    return k >= 0 ? stats_db_saved_delete(ui_saved()[k].id) : stats_db_import_delete(name);
}

/* (documented in puzzles_colls.h) */
bool puzzles_export(int c, char *msg, size_t n)
{
    int k = c >= PUZZLE_COLLECTION_COUNT ? coll_saved_index(c) : -1;
    if (k < 0) {
        return false;
    }
    stats_db_saved_export(ui_saved()[k].id, msg, n);
    return true;
}

/* (documented in puzzles_colls.h) */
int puzzles_take_out(int c, int i, char *msg, size_t n)
{
    int k = c >= PUZZLE_COLLECTION_COUNT ? coll_saved_index(c) : -1;
    if (k < 0 || i < 0 || i >= coll_size(c)) {
        return -1;
    }
    char name[48], cname[STATS_IMPORT_NAME];
    uint16_t id = coll_puzzle(c, i);
    puzzle_name(id, name, sizeof(name));
    snprintf(cname, sizeof(cname), "%s", ui_saved()[k].name);
    bool last = ui_saved()[k].count == 1;
    if (!stats_db_saved_remove(ui_saved()[k].id, id)) {
        return 0;
    }
    snprintf(msg, n, last ? "%s taken out of %s: it was the last, so the collection is gone."
             : "%s taken out of %s.", name, cname);
    return 1;
}
