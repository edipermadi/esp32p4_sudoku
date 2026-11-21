/**
 * @file stats_db_generated.c
 * @brief Puzzles generated on the board: storing and loading them, and the daily puzzle.
 */
#include "stats_db_internal.h"

/* (documented in stats_db_internal.h) */
bool sdb_gen_source_of(int gid, char name[STATS_IMPORT_NAME], int *number)
{
    bool found = false;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT source, number FROM generated WHERE id = ?1 AND source IS NOT NULL",
                                       -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, gid);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *n = (const char *)sqlite3_column_text(st, 0);
            snprintf(name, STATS_IMPORT_NAME, "%s", n ? n : "");
            *number = sqlite3_column_int(st, 1);
            found = true;
        }
        sqlite3_finalize(st);
    }
    return found;
}

/* (documented in stats_db_internal.h) */
int sdb_gen_level_of(int gid, bool *daily, int *variant)
{
    int level = -1;
    *daily = false;
    *variant = VAR_CLASSIC;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT level, day, variant FROM generated WHERE id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, gid);
        if (sqlite3_step(st) == SQLITE_ROW) {
            level = sqlite3_column_int(st, 0);
            *daily = sqlite3_column_type(st, 1) != SQLITE_NULL;
            *variant = sqlite3_column_int(st, 2);
        }
        sqlite3_finalize(st);
    }
    return level;
}

/* (documented in stats_db_internal.h) */
void sdb_do_gen_add(void)
{
    s_gen.ok = false;
    sqlite3_stmt *st;
    char cells[82];
    for (int i = 0; i < 81; i++) {
        cells[i] = (char)('0' + (s_gen.givens[i] <= 9 ? s_gen.givens[i] : 0));
    }
    cells[81] = 0;
    time_t now = time(NULL);
    char reg[82];
    for (int i = 0; i < 81; i++) {
        reg[i] = (char)('0' + (s_gen.regions[i] <= 8 ? s_gen.regions[i] : 0));
    }
    reg[81] = 0;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "INSERT INTO generated(level, givens, created_at, day, variant, regions, cages,"
                                       " marks, thermos, source, number) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9,"
                                       " ?10, (SELECT COALESCE(MAX(number), 0) + 1 FROM generated WHERE source = ?10))",
                                       -1, &st, NULL) == SQLITE_OK) {
        if (s_gen.own[0]) {
            sqlite3_bind_text(st, 10, s_gen.own, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(st, 10);    /* (number: NULL as well then) */
        }
        if (s_gen.thermos[0]) {
            sqlite3_bind_text(st, 9, s_gen.thermos, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(st, 9);
        }
        if (s_gen.marks[0]) {
            sqlite3_bind_text(st, 8, s_gen.marks, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(st, 8);
        }
        if (s_gen.cages[0]) {
            sqlite3_bind_text(st, 7, s_gen.cages, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(st, 7);
        }
        sqlite3_bind_int(st, 1, s_gen.level);
        sqlite3_bind_int(st, 5, s_gen.variant);
        if (s_gen.has_regions) {
            sqlite3_bind_text(st, 6, reg, 81, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(st, 6);
        }
        sqlite3_bind_text(st, 2, cells, 81, SQLITE_STATIC);
        if (s_gen.day[0]) {
            sqlite3_bind_text(st, 4, s_gen.day, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(st, 4);
        }
        if (now > 1700000000) {
            sqlite3_bind_int64(st, 3, now);
        } else {
            sqlite3_bind_null(st, 3);
        }
        if (sqlite3_step(st) == SQLITE_DONE) {
            s_gen.id = (int)sqlite3_last_insert_rowid(sdb_db());
            s_gen.ok = true;
            if (s_gen.own[0]) {
                sdb_gen_source_of(s_gen.id, s_gen.source, &s_gen.number);
            }
        }
        sqlite3_finalize(st);
    }
    sdb_gen_reply();
}

/* (documented in stats_db_internal.h) */
void sdb_do_gen_get(void)
{
    s_gen.ok = false;
    sqlite3_stmt *st;
    s_gen.has_regions = false;
    s_gen.cages[0] = 0;
    s_gen.marks[0] = 0;
    s_gen.thermos[0] = 0;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT level, givens, day, variant, regions, cages, marks, thermos"
                                       " FROM generated WHERE id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, s_gen.id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char *g = (const char *)sqlite3_column_text(st, 1);
            const char *day = (const char *)sqlite3_column_text(st, 2);
            snprintf(s_gen.day, sizeof(s_gen.day), "%s", day ? day : "");
            if (g && strlen(g) == 81) {
                s_gen.level = sqlite3_column_int(st, 0);
                s_gen.variant = sqlite3_column_int(st, 3);
                for (int i = 0; i < 81; i++) {
                    s_gen.givens[i] = (g[i] >= '0' && g[i] <= '9') ? g[i] - '0' : 0;
                }
                const char *r = (const char *)sqlite3_column_text(st, 4);
                if (r && strlen(r) == 81) {
                    for (int i = 0; i < 81; i++) {
                        s_gen.regions[i] = (r[i] >= '0' && r[i] <= '8') ? r[i] - '0' : 0;
                    }
                    s_gen.has_regions = true;
                }
                const char *k = (const char *)sqlite3_column_text(st, 5);
                snprintf(s_gen.cages, sizeof(s_gen.cages), "%s", k ? k : "");
                const char *mk = (const char *)sqlite3_column_text(st, 6);
                snprintf(s_gen.marks, sizeof(s_gen.marks), "%s", mk ? mk : "");
                const char *th = (const char *)sqlite3_column_text(st, 7);
                snprintf(s_gen.thermos, sizeof(s_gen.thermos), "%s", th ? th : "");
                s_gen.ok = true;
            }
        }
        sqlite3_finalize(st);
    }
    sdb_gen_reply();
}

/**
 * @brief The day before a date, in place.
 * @param d  'YYYY-MM-DD'
 */
static void prev_day(char *d)
{
    struct tm tm = {0};
    if (sscanf(d, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) {
        return;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_mday -= 1;
    tm.tm_hour = 12;                              /* clear of DST changes */
    mktime(&tm);                                  /* normalises day 0 to the month before */
    strftime(d, 11, "%Y-%m-%d", &tm);
}

/* (documented in stats_db_internal.h) */
void sdb_do_daily(void)
{
    s_gen.id = 0;
    s_gen.solved_today = false;
    s_gen.streak = 0;
    s_gen.ok = sdb_db() != NULL;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT id FROM generated WHERE day = ?1 ORDER BY id LIMIT 1",
                                       -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, s_gen.day, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            s_gen.id = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(),
                                       "SELECT DISTINCT g.day FROM solves s JOIN generated g ON s.puzzle = 2000 + g.id"
                                       " WHERE s.player_id = ?1 AND g.day IS NOT NULL AND g.day <= ?2 ORDER BY g.day DESC",
                                       -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_text(st, 2, s_gen.day, -1, SQLITE_STATIC);
        char want[12];
        snprintf(want, sizeof(want), "%s", s_gen.day);
        bool first = true;
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *d = (const char *)sqlite3_column_text(st, 0);
            if (!d) {
                break;
            }
            if (first) {
                first = false;
                s_gen.solved_today = strcmp(d, want) == 0;
                if (!s_gen.solved_today) {
                    prev_day(want);    /* not yet today: a streak can end yesterday */
                }
            }
            if (strcmp(d, want) != 0) {
                break;
            }
            s_gen.streak++;
            prev_day(want);
        }
        sqlite3_finalize(st);
    }
    sdb_gen_reply();
}

/* (documented in stats_db.h) */
int stats_db_gen_add(int variant, int level, const uint8_t givens[81], const uint8_t *regions, const char *cages,
                     const char *marks, const char *thermos, const char *day)
{
    if (!sdb_gen()) {
        return -1;    /* no database this session */
    }
    snprintf(s_gen.cages, sizeof(s_gen.cages), "%s", cages ? cages : "");
    snprintf(s_gen.marks, sizeof(s_gen.marks), "%s", marks ? marks : "");
    snprintf(s_gen.thermos, sizeof(s_gen.thermos), "%s", thermos ? thermos : "");
    s_gen.variant = variant;
    s_gen.level = level;
    memcpy(s_gen.givens, givens, 81);
    s_gen.has_regions = regions != NULL;
    if (regions) {
        memcpy(s_gen.regions, regions, 81);
    }
    snprintf(s_gen.day, sizeof(s_gen.day), "%s", day ? day : "");
    return sdb_gen_call(CMD_GEN_ADD) ? s_gen.id : -1;
}

/* (documented in stats_db.h) */
bool stats_db_gen_get(int id, uint8_t givens[81], uint8_t *regions, char *cages, char *marks, char *thermos,
                      int *variant, int *level, char day[12])
{
    if (!sdb_gen()) {
        return false;    /* no database this session */
    }
    s_gen.id = id;
    if (!sdb_gen_call(CMD_GEN_GET)) {
        return false;
    }
    memcpy(givens, s_gen.givens, 81);
    if (cages) {
        snprintf(cages, STATS_CAGES_LEN, "%s", s_gen.cages);
    }
    if (marks) {
        snprintf(marks, 82, "%s", s_gen.marks);
    }
    if (thermos) {
        snprintf(thermos, STATS_THERMOS_LEN, "%s", s_gen.thermos);
    }
    if (regions && s_gen.has_regions) {
        memcpy(regions, s_gen.regions, 81);
    }
    if (variant) {
        *variant = s_gen.variant;
    }
    if (level) {
        *level = s_gen.level;
    }
    if (day) {
        snprintf(day, 12, "%s", s_gen.day);
    }
    return true;
}

/* (documented in stats_db.h) */
bool stats_db_daily_status(const char *day, int *id, bool *solved_today, int *streak)
{
    if (!sdb_gen()) {
        return false;    /* no database this session */
    }
    snprintf(s_gen.day, sizeof(s_gen.day), "%s", day);
    if (!sdb_gen_call(CMD_DAILY)) {
        return false;
    }
    *id = s_gen.id;
    *solved_today = s_gen.solved_today;
    *streak = s_gen.streak;
    return true;
}

/* (documented in stats_db.h) */
int stats_db_gen_add_from(const char *source, int variant, int level, const uint8_t givens[81],
                          const uint8_t *regions, const char *cages, const char *marks, const char *thermos, int *number)
{
    if (!sdb_gen()) {
        return -1;
    }
    snprintf(s_gen.own, sizeof(s_gen.own), "%s", source);
    int id = stats_db_gen_add(variant, level, givens, regions, cages, marks, thermos, NULL);
    s_gen.own[0] = 0;
    if (id > 0 && number) {
        *number = s_gen.number;
    }
    return id;
}

/* (documented in stats_db.h) */
int stats_db_gen_add_own(const uint8_t givens[81], int level, int *number)
{
    return stats_db_gen_add_from(STATS_OWN_SOURCE, VAR_CLASSIC, level, givens, NULL, NULL, NULL, NULL, number);
}

/* (documented in stats_db.h) */
bool stats_db_gen_source(int gid, char name[STATS_IMPORT_NAME], int *number)
{
    if (!sdb_gen()) {
        return false;
    }
    s_gen.id = gid;
    if (!sdb_gen_call(CMD_GEN_SOURCE)) {
        return false;
    }
    snprintf(name, STATS_IMPORT_NAME, "%s", s_gen.source);
    *number = s_gen.number;
    return true;
}
