/**
 * @file stats_db_small.c
 * @brief The small tables: bookmarks, seen tips, the time-attack board, learned techniques.
 */
#include "stats_db_internal.h"

/* (documented in stats_db_internal.h) */
void sdb_do_small_get(cmd_type_t type)
{
    sqlite3_stmt *st;
    s_gen.flag = false;
    s_gen.nids = s_gen.nattack = 0;
    const char *sql = type == CMD_BM_GET ? "SELECT 1 FROM bookmarks WHERE player_id = ?1 AND puzzle = ?2"
                      : type == CMD_BM_LIST ? "SELECT puzzle FROM bookmarks WHERE player_id = ?1 ORDER BY at DESC, rowid DESC"
                      : type == CMD_SEEN_GET ? "SELECT 1 FROM seen WHERE player_id = ?1 AND what = ?3"
                      : "SELECT p.name, MAX(a.solved), COUNT(*), p.id = ?1 FROM attack a JOIN players p ON p.id = a.player_id"
                      " GROUP BY a.player_id ORDER BY 2 DESC, MIN(a.finished_at)";
    bool ok = sdb_db() && sqlite3_prepare_v2(sdb_db(), sql, -1, &st, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int(st, 1, sdb_player());
        if (type == CMD_BM_GET) {
            sqlite3_bind_int(st, 2, s_gen.puzzle);
        }
        if (type == CMD_SEEN_GET) {
            sqlite3_bind_text(st, 3, s_gen.what, -1, SQLITE_STATIC);
        }
        while (sqlite3_step(st) == SQLITE_ROW) {
            if (type == CMD_BM_LIST) {
                if (s_gen.nids < STATS_MAX_BOOKMARKS) {
                    s_gen.ids[s_gen.nids++] = (uint16_t)sqlite3_column_int(st, 0);
                }
            } else if (type == CMD_ATTACK_BOARD) {
                if (s_gen.nattack >= STATS_ATTACK_ROWS) {
                    break;
                }
                stats_attack_row_t *r = &s_gen.attack[s_gen.nattack++];
                const unsigned char *n = sqlite3_column_text(st, 0);
                snprintf(r->name, sizeof(r->name), "%s", n ? (const char *)n : "?");
                r->best = sqlite3_column_int(st, 1);
                r->runs = sqlite3_column_int(st, 2);
                r->me = sqlite3_column_int(st, 3) != 0;
            } else {
                s_gen.flag = true;
            }
        }
        sqlite3_finalize(st);
    }
    s_gen.ok = ok;
    sdb_gen_reply();
}

/* (documented in stats_db_internal.h) */
void sdb_do_small_set(const cmd_t *c)
{
    sqlite3_stmt *st;
    const char *sql;
    if (c->type == CMD_BM_SET)
        sql = c->ival ? "INSERT OR IGNORE INTO bookmarks(player_id, puzzle, at) VALUES(?1, ?2, ?3)"
              : "DELETE FROM bookmarks WHERE player_id = ?1 AND puzzle = ?2";
    else if (c->type == CMD_SEEN_SET)
        sql = c->ival ? "INSERT OR IGNORE INTO seen(player_id, what, at) VALUES(?1, ?4, ?3)"
              : "DELETE FROM seen WHERE player_id = ?1 AND what = ?4";
    else {
        sql = "INSERT INTO attack(player_id, finished_at, solved, mistakes) VALUES(?1, ?3, ?2, ?5)";
    }
    if (!sdb_db() || sqlite3_prepare_v2(sdb_db(), sql, -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    time_t now = time(NULL);
    sqlite3_bind_int(st, 1, sdb_player());
    if (sqlite3_bind_parameter_index(st, "?2")) {
        sqlite3_bind_int(st, 2, c->type == CMD_ATTACK ? c->ival : c->puzzle);
    }
    if (sqlite3_bind_parameter_index(st, "?3")) {
        if (now > 1700000000) {
            sqlite3_bind_int64(st, 3, now);
        } else {
            sqlite3_bind_null(st, 3);
        }
    }
    if (sqlite3_bind_parameter_index(st, "?4")) {
        sqlite3_bind_text(st, 4, c->text, -1, SQLITE_TRANSIENT);
    }
    if (sqlite3_bind_parameter_index(st, "?5")) {
        sqlite3_bind_int(st, 5, (int)c->mistakes);
    }
    bool ok = sdb_step_done(st, "bookmark/seen/attack");
    sqlite3_finalize(st);
    if (ok && c->type == CMD_ATTACK) {
        sdb_ach_sync(sdb_player(), true);    /* (the time attack achievements) */
    }
}

/* (documented in stats_db_internal.h) */
void sdb_do_learned(int technique)
{
    sqlite3_stmt *st;
    if (!sdb_db() || sqlite3_prepare_v2(sdb_db(), "INSERT OR IGNORE INTO learned(player_id, technique, at) VALUES(?1, ?2, ?3)",
                                        -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    time_t now = time(NULL);
    sqlite3_bind_int(st, 1, sdb_player());
    sqlite3_bind_int(st, 2, technique);
    if (now > 1700000000) {
        sqlite3_bind_int64(st, 3, now);
    } else {
        sqlite3_bind_null(st, 3);
    }
    bool ok = sdb_step_done(st, "learned");
    sqlite3_finalize(st);
    if (ok && sqlite3_changes(sdb_db())) {
        sdb_ach_sync(sdb_player(), true);    /* (a first time: maybe Graduate) */
    }
}

/* (documented in stats_db.h) */
bool is_stats_db_bookmarked(uint16_t puzzle)
{
    if (!sdb_gen()) {
        return false;
    }
    s_gen.puzzle = puzzle;
    return sdb_gen_call(CMD_BM_GET) && s_gen.flag;
}

/* (documented in stats_db.h) */
void stats_db_set_bookmark(uint16_t puzzle, bool on)
{
    cmd_t c = {.type = CMD_BM_SET, .puzzle = puzzle, .ival = on};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
int stats_db_bookmarks(uint16_t *ids, int max)
{
    if (!sdb_gen() || !sdb_gen_call(CMD_BM_LIST)) {
        return -1;
    }
    int n = s_gen.nids < max ? s_gen.nids : max;
    memcpy(ids, s_gen.ids, sizeof(uint16_t) * (size_t)n);
    return n;
}

/* (documented in stats_db.h) */
bool is_stats_db_seen(const char *what)
{
    if (!sdb_gen()) {
        return true;    /* no database: don't nag every time */
    }
    snprintf(s_gen.what, sizeof(s_gen.what), "%s", what);
    return !sdb_gen_call(CMD_SEEN_GET) || s_gen.flag;
}

/* (documented in stats_db.h) */
void stats_db_set_seen(const char *what, bool seen)
{
    cmd_t c = {.type = CMD_SEEN_SET, .ival = seen};
    snprintf(c.text, sizeof(c.text), "%s", what);
    sdb_send(&c);
}

/* (documented in stats_db.h) */
void stats_db_record_attack(int solved, int mistakes)
{
    cmd_t c = {.type = CMD_ATTACK, .ival = solved, .mistakes = (uint32_t)mistakes};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
int stats_db_attack_board(stats_attack_row_t *rows, int max)
{
    if (!sdb_gen() || !sdb_gen_call(CMD_ATTACK_BOARD)) {
        return -1;
    }
    int n = s_gen.nattack < max ? s_gen.nattack : max;
    memcpy(rows, s_gen.attack, sizeof(*rows) * (size_t)n);
    return n;
}

/* (documented in stats_db.h) */
void stats_db_learned(int technique)
{
    cmd_t c = {.type = CMD_LEARNED, .ival = technique};
    sdb_send(&c);
}
