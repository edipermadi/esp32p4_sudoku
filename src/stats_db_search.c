/**
 * @file stats_db_search.c
 * @brief The Puzzles screen's search: every puzzle made on the board or imported, with what its name is
 *        made of (variant, level, source and number, the daily date), for the UI to match.
 */
#include "stats_db_internal.h"

static stats_gen_row_t *s_rows;        /**< CMD_GEN_ROWS result (PSRAM): rewritten only by the next call */

/* (documented in stats_db_internal.h) */
void sdb_do_gen_rows(void)
{
    s_gen.count = 0;
    s_gen.ok = sdb_db() != NULL && sdb_psram_buf((void **)&s_rows, sizeof(*s_rows) * STATS_MAX_GEN_ROWS);
    sqlite3_stmt *st;
    if (s_gen.ok && sqlite3_prepare_v2(sdb_db(), "SELECT id, variant, level, source, number, day FROM generated"
                                       " ORDER BY id DESC", -1, &st, NULL) == SQLITE_OK) {
        while (s_gen.count < STATS_MAX_GEN_ROWS && sqlite3_step(st) == SQLITE_ROW) {   /* (newest first) */
            stats_gen_row_t *r = &s_rows[s_gen.count++];
            const char *src = (const char *)sqlite3_column_text(st, 3), *day = (const char *)sqlite3_column_text(st, 5);
            r->id = (uint16_t)(PUZZLE_GEN_BASE + sqlite3_column_int(st, 0));
            r->variant = (uint8_t)sqlite3_column_int(st, 1);
            r->level = (int8_t)sqlite3_column_int(st, 2);
            r->number = sqlite3_column_type(st, 4) == SQLITE_NULL ? 0 : sqlite3_column_int(st, 4);
            snprintf(r->source, sizeof(r->source), "%s", src ? src : "");
            snprintf(r->day, sizeof(r->day), "%s", day ? day : "");
        }
        sqlite3_finalize(st);
    }
    sdb_gen_reply();
}

/* (documented in stats_db.h) */
int stats_db_gen_rows(const stats_gen_row_t **rows)
{
    if (!sdb_gen() || !sdb_gen_call(CMD_GEN_ROWS)) {
        return -1;
    }
    *rows = s_rows;
    return s_gen.count;
}
