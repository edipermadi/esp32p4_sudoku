/**
 * @file stats_db_saved.c
 * @brief Saved collections: puzzles put together by the player, and their export to the card.
 */
#include "stats_db_internal.h"

/* (documented in stats_db_internal.h) */
void sdb_do_saved_list(void)
{
    s_gen.nsaved = 0;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT id, name, created_at FROM saved_collections ORDER BY id", -1, &st, NULL) ==
            SQLITE_OK) {
        while (s_gen.nsaved < STATS_MAX_SAVED && sqlite3_step(st) == SQLITE_ROW) {
            stats_saved_t *c = &s_gen.saved[s_gen.nsaved++];
            c->id = sqlite3_column_int(st, 0);
            const char *n = (const char *)sqlite3_column_text(st, 1);
            snprintf(c->name, sizeof(c->name), "%s", n ? n : "");
            c->count = 0;
            c->created_at = sqlite3_column_int64(st, 2);   /* (NULL: 0) */
        }
        sqlite3_finalize(st);
    }
    for (int k = 0; sdb_db() && k < s_gen.nsaved; k++) {
        stats_saved_t *c = &s_gen.saved[k];
        if (sqlite3_prepare_v2(sdb_db(), "SELECT puzzle FROM saved_puzzles WHERE collection_id = ?1 ORDER BY number", -1,
                               &st, NULL) != SQLITE_OK) {
            continue;
        }
        sqlite3_bind_int(st, 1, c->id);
        while (c->count < STATS_SAVED_MAX && sqlite3_step(st) == SQLITE_ROW) {
            c->puzzles[c->count++] = (uint16_t)sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
    s_gen.ok = sdb_db() != NULL;
    sdb_gen_reply();
}

/**
 * @brief One number from a query with one int parameter.
 * @param[in] sql       the query
 * @param[in] a1        ?1 (bound if the query has a parameter)
 * @param[in] fallback  the answer for none or an error
 * @return the number
 */
static int query_int_arg(const char *sql, int a1, int fallback)
{
    sqlite3_stmt *st;
    int v = fallback;
    if (sqlite3_prepare_v2(sdb_db(), sql, -1, &st, NULL) != SQLITE_OK) {
        return fallback;
    }
    if (sqlite3_bind_parameter_count(st) >= 1) {
        sqlite3_bind_int(st, 1, a1);
    }
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL) {
        v = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return v;
}

/* (documented in stats_db_internal.h) */
void sdb_do_saved_add(void)
{
    s_gen.ok = false;
    s_gen.why[0] = 0;
    sqlite3_stmt *st;
    int cid = -1;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT id FROM saved_collections WHERE name = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, s_gen.name, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            cid = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
    if (!sdb_db()) {
        snprintf(s_gen.why, sizeof(s_gen.why), "There's no sudoku.db on a microSD card.");
    } else if (!CLOCK_VALID(time(NULL))) {             /* (every collection gets its date) */
        snprintf(s_gen.why, sizeof(s_gen.why), "%s", SDB_NO_CLOCK);
    } else if (cid < 0 && query_int_arg("SELECT COUNT(*) FROM saved_collections", -1, 0) >= STATS_MAX_SAVED) {
        snprintf(s_gen.why, sizeof(s_gen.why), "There are %d collections already: that's the most.", STATS_MAX_SAVED);
    } else if (cid >= 0 && query_int_arg("SELECT COUNT(*) FROM saved_puzzles WHERE collection_id = ?1", cid, 0) >=
               STATS_SAVED_MAX) {
        snprintf(s_gen.why, sizeof(s_gen.why), "%s is full (%d puzzles).", s_gen.name, STATS_SAVED_MAX);
    } else {
        time_t now = time(NULL);
        bool ok = true;
        if (cid < 0) {
            ok = sqlite3_prepare_v2(sdb_db(), "INSERT INTO saved_collections(name, created_at) VALUES(?1, ?2)", -1, &st,
                                    NULL) == SQLITE_OK;
            if (ok) {
                sqlite3_bind_text(st, 1, s_gen.name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(st, 2, now);         /* (the clock is set: checked above) */
                ok = sdb_step_done(st, "new collection");
                sqlite3_finalize(st);
                cid = (int)sqlite3_last_insert_rowid(sdb_db());
            }
        }
        int number = query_int_arg("SELECT MAX(number) FROM saved_puzzles WHERE collection_id = ?1", cid, 0) + 1;
        if (ok && sqlite3_prepare_v2(sdb_db(), "INSERT OR IGNORE INTO saved_puzzles(collection_id, puzzle, number, added_at)"
                                     " VALUES(?1, ?2, ?3, ?4)", -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, cid);
            sqlite3_bind_int(st, 2, s_gen.puzzle);
            sqlite3_bind_int(st, 3, number);
            if (now > 1700000000) {
                sqlite3_bind_int64(st, 4, now);
            } else {
                sqlite3_bind_null(st, 4);
            }
            ok = sdb_step_done(st, "save to collection");
            sqlite3_finalize(st);
        }
        s_gen.ok = ok;
        if (!ok) {
            snprintf(s_gen.why, sizeof(s_gen.why), "Couldn't write to sudoku.db.");
        }
    }
    sdb_gen_reply();
}

/* (documented in stats_db_internal.h) */
void sdb_do_saved_remove(void)
{
    s_gen.ok = false;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "DELETE FROM saved_puzzles WHERE collection_id = ?1 AND puzzle = ?2", -1, &st,
                                       NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_gen.id);
        sqlite3_bind_int(st, 2, s_gen.puzzle);
        s_gen.ok = sdb_step_done(st, "take out of collection");
        sqlite3_finalize(st);
        if (query_int_arg("SELECT COUNT(*) FROM saved_puzzles WHERE collection_id = ?1", s_gen.id, 1) == 0 &&
                sqlite3_prepare_v2(sdb_db(), "DELETE FROM saved_collections WHERE id = ?1", -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_int(st, 1, s_gen.id);                   /* (its last one: the collection goes) */
            sdb_step_done(st, "empty collection");
            sqlite3_finalize(st);
        }
    }
    sdb_gen_reply();
}

/* (documented in stats_db_internal.h) */
void sdb_do_saved_export(void)
{
    s_gen.ok = false;
    char name[STATS_IMPORT_NAME] = "";
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT name FROM saved_collections WHERE id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_gen.id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            snprintf(name, sizeof(name), "%s", (const char *)sqlite3_column_text(st, 0));
        }
        sqlite3_finalize(st);
    }
    if (!name[0]) {
        snprintf(s_gen.why, sizeof(s_gen.why), "That collection isn't there any more.");
        sdb_gen_reply();
        return;
    }
    char file[STATS_IMPORT_NAME], path[96];                /* its name, as a file name */
    size_t k = 0;
    for (const char *p = name; *p && k + 1 < sizeof(file); p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
                  c == '-' || c == '_' || (unsigned char)c >= 0x80;
        file[k++] = ok ? c : '_';
    }
    file[k] = 0;
    mkdir(IMPORT_DIR, 0775);
    snprintf(path, sizeof(path), IMPORT_DIR "/%s.sdx", file);
    FILE *f = fopen(path, "w");
    int n = 0;
    bool ok = f && fprintf(f, SDX_HEADER "\n# %s\n", name) > 0;
    if (ok && sqlite3_prepare_v2(sdb_db(), "SELECT g.variant, g.level, g.givens, g.regions, g.cages, g.marks, g.thermos"
                                 " FROM saved_puzzles sp JOIN generated g ON g.id = sp.puzzle - ?2"
                                 " WHERE sp.collection_id = ?1 ORDER BY sp.number", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_gen.id);
        sqlite3_bind_int(st, 2, PUZZLE_GEN_BASE);
        while (ok && sqlite3_step(st) == SQLITE_ROW) {
            int v = sqlite3_column_int(st, 0), lv = sqlite3_column_int(st, 1);
            const char *t[5];
            for (int i = 0; i < 5; i++) {
                t[i] = (const char *)sqlite3_column_text(st, 2 + i);
                if (!t[i]) {
                    t[i] = "";
                }
            }
            ok = fprintf(f, "%s|%s|%s|%s|%s|%s|%s\n", v >= 0 && v < VAR_COUNT ? variant_name(v) : "Classic",
                         lv >= 0 && lv < GEN_LEVELS ? gen_level_name(lv) : "Easy", t[0],
                         is_jigsaw_variant(v) ? t[1] : "", t[2], t[3], t[4]) > 0;
            n++;
        }
        sqlite3_finalize(st);
    }
    if (f && fclose(f) != 0) {
        ok = false;
    }
    if (ok) {
        snprintf(s_gen.why, sizeof(s_gen.why), "Wrote puzzles/%s.sdx: %d puzzle%s.", file, n, n == 1 ? "" : "s");
        printf("stats_db: exported %s (%d puzzles)\n", path, n);
    } else {
        if (f) {
            unlink(path);
        }
        snprintf(s_gen.why, sizeof(s_gen.why), "Couldn't write puzzles/%s.sdx.", file);
    }
    s_gen.ok = ok;
    sdb_gen_reply();
}

/* (documented in stats_db_internal.h) */
void sdb_do_coll_delete(void)
{
    static const char *const SAVED[] = {"DELETE FROM saved_puzzles WHERE collection_id = ?1",
                                        "DELETE FROM saved_collections WHERE id = ?1"
                                       };
    bool ok = sdb_db() && sdb_exec("BEGIN");
    for (int i = 0; ok && i < (s_gen.id > 0 ? 2 : 1); i++) {
        sqlite3_stmt *st;
        const char *sql = s_gen.id > 0 ? SAVED[i] : "DELETE FROM imported WHERE name = ?2";
        ok = sqlite3_prepare_v2(sdb_db(), sql, -1, &st, NULL) == SQLITE_OK;
        if (!ok) {
            break;
        }
        if (sqlite3_bind_parameter_index(st, "?1")) {
            sqlite3_bind_int(st, 1, s_gen.id);
        }
        if (sqlite3_bind_parameter_index(st, "?2")) {
            sqlite3_bind_text(st, 2, s_gen.name, -1, SQLITE_STATIC);
        }
        ok = sdb_step_done(st, "delete collection");
        sqlite3_finalize(st);
    }
    if (ok) {
        ok = sdb_exec("COMMIT");
    } else if (sdb_db()) {
        sdb_exec("ROLLBACK");
    }
    s_gen.ok = ok;
    sdb_gen_reply();
}

/* (documented in stats_db_internal.h) */
void sdb_do_solved_list(void)
{
    memset(s_gen.solved_bits, 0, sizeof(s_gen.solved_bits));
    s_gen.nsolved = 0;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT 1 FROM puzzle_stats WHERE player_id = ?1 AND puzzle = ?2", -1, &st,
                                       NULL) == SQLITE_OK) {
        for (int i = 0; i < s_gen.nlist && i < 128; i++) {
            sqlite3_reset(st);
            sqlite3_bind_int(st, 1, sdb_player());
            sqlite3_bind_int(st, 2, s_gen.list[i]);
            if (sqlite3_step(st) == SQLITE_ROW) {
                s_gen.solved_bits[i >> 3] |= (uint8_t)(1u << (i & 7));
                s_gen.nsolved++;
            }
        }
        sqlite3_finalize(st);
    }
    s_gen.ok = sdb_db() != NULL;
    sdb_gen_reply();
}

/* (documented in stats_db.h) */
int stats_db_list_saved(stats_saved_t *out)
{
    if (!sdb_gen() || !sdb_gen_call(CMD_SAVED_LIST)) {
        return -1;
    }
    memcpy(out, s_gen.saved, sizeof(stats_saved_t) * (size_t)s_gen.nsaved);
    return s_gen.nsaved;
}

/* (documented in stats_db.h) */
bool stats_db_saved_add(const char *name, uint16_t puzzle, char *why, size_t why_n)
{
    snprintf(why, why_n, "There's no sudoku.db on a microSD card.");
    if (!sdb_gen()) {
        return false;
    }
    snprintf(s_gen.name, sizeof(s_gen.name), "%s", name);
    s_gen.puzzle = puzzle;
    s_gen.why[0] = 0;
    bool ok = sdb_gen_call(CMD_SAVED_ADD);
    if (!ok) {
        snprintf(why, why_n, "%s", s_gen.why[0] ? s_gen.why : "sudoku.db didn't answer.");
    }
    return ok;
}

/* (documented in stats_db.h) */
bool stats_db_saved_remove(int collection, uint16_t puzzle)
{
    if (!sdb_gen()) {
        return false;
    }
    s_gen.id = collection;
    s_gen.puzzle = puzzle;
    return sdb_gen_call(CMD_SAVED_REMOVE);
}

/* (documented in stats_db.h) */
bool stats_db_saved_delete(int collection)
{
    if (!sdb_gen() || collection <= 0) {
        return false;
    }
    s_gen.id = collection;
    return sdb_gen_call(CMD_COLL_DELETE);
}

/* (documented in stats_db.h) */
bool stats_db_saved_export(int collection, char *msg, size_t msg_n)
{
    snprintf(msg, msg_n, "There's no sudoku.db on a microSD card.");
    if (!sdb_gen()) {
        return false;
    }
    s_gen.id = collection;
    s_gen.why[0] = 0;
    bool ok = sdb_gen_call(CMD_SAVED_EXPORT);
    snprintf(msg, msg_n, "%s", s_gen.why[0] ? s_gen.why : "sudoku.db didn't answer.");
    return ok;
}

/* (documented in stats_db.h) */
int stats_db_solved_list(const uint16_t *ids, int n, uint8_t *solved)
{
    if (!sdb_gen()) {
        return 0;
    }
    s_gen.nlist = n > 128 ? 128 : n;
    memcpy(s_gen.list, ids, sizeof(uint16_t) * (size_t)s_gen.nlist);
    if (!sdb_gen_call(CMD_SOLVED_LIST)) {
        return 0;
    }
    if (solved) {
        memcpy(solved, s_gen.solved_bits, (size_t)(s_gen.nlist + 7) / 8);
    }
    return s_gen.nsolved;
}
