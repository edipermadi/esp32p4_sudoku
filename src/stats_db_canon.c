/**
 * @file stats_db_canon.c
 * @brief Solves' canonical hashes (canon_core.h): worked out from the puzzle a solve is of, kept in
 *        solves.canon, filled in for older solves once, and asked for when a puzzle is opened: has the
 *        player solved it before under another name (turned, mirrored or renumbered)?
 */
#include "stats_db_internal.h"
#include "canon_core.h"

/* (documented in stats_db_internal.h) */
uint64_t sdb_puzzle_canon(int puzzle)
{
    uint8_t givens[81];
    int variant = VAR_CLASSIC;
    if (puzzle >= 0 && puzzle < PUZZLE_COUNT) {     /* built in: worked out once, in flash */
        return puzzle_canon((uint16_t)puzzle);
    }
    if (puzzle < PUZZLE_GEN_BASE || puzzle >= PUZZLE_GEN_RAM_BASE || !sdb_db()) {
        return 0;                                   /* (made without a card: not in sudoku.db) */
    }
    sqlite3_stmt *st;
    uint64_t h = 0;
    if (sqlite3_prepare_v2(sdb_db(), "SELECT givens, variant FROM generated WHERE id = ?1", -1, &st, NULL) ==
            SQLITE_OK) {
        sqlite3_bind_int(st, 1, puzzle - PUZZLE_GEN_BASE);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *g = (const char *)sqlite3_column_text(st, 0);
            variant = sqlite3_column_int(st, 1);
            if (g && strlen(g) == 81) {
                for (int i = 0; i < 81; i++) {
                    givens[i] = g[i] >= '1' && g[i] <= '9' ? (uint8_t)(g[i] - '0') : 0;
                }
                h = canon_hash(variant, givens);
            }
        }
        sqlite3_finalize(st);
    }
    return h;
}

/* (documented in stats_db_internal.h) */
bool sdb_canon_backfill(void)
{
    sqlite3_stmt *sel, *upd;
    if (sqlite3_prepare_v2(sdb_db(), "SELECT id, puzzle FROM solves WHERE canon IS NULL", -1, &sel, NULL) != SQLITE_OK) {
        return false;
    }
    if (sqlite3_prepare_v2(sdb_db(), "UPDATE solves SET canon = ?2 WHERE id = ?1", -1, &upd, NULL) != SQLITE_OK) {
        sqlite3_finalize(sel);
        return false;
    }
    bool ok = sdb_exec("BEGIN");
    int n = 0;
    while (ok && sqlite3_step(sel) == SQLITE_ROW) {
        const uint64_t h = sdb_puzzle_canon(sqlite3_column_int(sel, 1));
        if (!h) {
            continue;                               /* (a variant without one: stays NULL) */
        }
        sqlite3_bind_int64(upd, 1, sqlite3_column_int64(sel, 0));
        sqlite3_bind_int64(upd, 2, (int64_t)h);
        ok = sdb_step_done(upd, "canon");
        n++;
    }
    sqlite3_finalize(sel);
    sqlite3_finalize(upd);
    if (ok && sdb_exec("COMMIT")) {
        printf("stats_db: canonical hashes for %d earlier solves\n", n);
        return true;
    }
    sdb_exec("ROLLBACK");
    return false;
}

/* (documented in stats_db_internal.h) */
void sdb_do_twin(void)
{
    s_gen.twin.found = false;
    s_gen.ok = sdb_db() != NULL;
    const uint64_t h = sdb_puzzle_canon(s_gen.puzzle);
    sqlite3_stmt *st;
    if (h && sdb_db() &&
            sqlite3_prepare_v2(sdb_db(), "SELECT puzzle, finished_at FROM solves WHERE player_id = ?1 AND canon = ?2"
                               " AND puzzle != ?3 ORDER BY finished_at DESC LIMIT 1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_int64(st, 2, (int64_t)h);
        sqlite3_bind_int(st, 3, s_gen.puzzle);
        if (sqlite3_step(st) == SQLITE_ROW) {
            s_gen.twin.found = true;
            s_gen.twin.puzzle = (uint16_t)sqlite3_column_int(st, 0);
            s_gen.twin.finished_at = sqlite3_column_type(st, 1) == SQLITE_NULL ? 0 : sqlite3_column_int64(st, 1);
        }
        sqlite3_finalize(st);
    }
    sdb_gen_reply();
}

/* (documented in stats_db.h) */
bool stats_db_solved_twin(uint16_t puzzle, stats_twin_t *out)
{
    if (!sdb_gen()) {
        return false;
    }
    s_gen.puzzle = puzzle;
    if (!sdb_gen_call(CMD_TWIN) || !s_gen.twin.found) {
        return false;
    }
    *out = s_gen.twin;
    return true;
}
