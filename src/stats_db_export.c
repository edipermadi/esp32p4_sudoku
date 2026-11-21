/**
 * @file stats_db_export.c
 * @brief Exporting the statistics as CSV files, and backing up the database to the card.
 */
#include "stats_db_internal.h"

#define EXPORT_DIR BSP_SD_MOUNT_POINT "/export"          /**< where the CSV files go */

/**
 * @brief Writes a CSV field: quoted when it holds a comma, quote or line break.
 * @param f     the file
 * @param s     the text
 * @param last  the row's last field (a line break follows, else a comma)
 */
static void csv_field(FILE *f, const char *s, bool last)
{
    s = s ? s : "";
    if (strpbrk(s, ",\"\r\n")) {
        fputc('"', f);
        for (; *s; s++) {
            if (*s == '"') {
                fputc('"', f);
            }
            fputc(*s, f);
        }
        fputc('"', f);
    } else {
        fputs(s, f);
    }
    fputc(last ? '\n' : ',', f);
}

/**
 * @brief Unix time as local "YYYY-MM-DD HH:MM:SS".
 * @param buf  gets it ("" for 0)
 * @param n    its size
 * @param t    the time
 */
static void csv_time(char *buf, size_t n, int64_t t)
{
    buf[0] = 0;
    if (t <= 0) {
        return;
    }
    time_t tt = (time_t)t;
    struct tm tm;
    localtime_r(&tt, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
}

/**
 * @brief Runs a query and writes every row to a CSV file (columns as text).
 * @param path       the file
 * @param header     its first line
 * @param sql        the query
 * @param time_cols  bit i: column i is a Unix time, written as a local date
 * @return the row count, -1 on failure
 */
static int csv_query(const char *path, const char *header, const char *sql, uint32_t time_cols)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        return -1;
    }
    fputs(header, f);
    fputc('\n', f);
    sqlite3_stmt *st;
    int rows = 0;
    if (sqlite3_prepare_v2(sdb_db(), sql, -1, &st, NULL) == SQLITE_OK) {
        int nc = sqlite3_column_count(st);
        while (sqlite3_step(st) == SQLITE_ROW) {
            for (int c = 0; c < nc; c++) {
                char t[24];
                if ((time_cols >> c & 1) && sqlite3_column_type(st, c) != SQLITE_NULL) {
                    csv_time(t, sizeof(t), sqlite3_column_int64(st, c));
                    csv_field(f, t, c == nc - 1);
                } else {
                    csv_field(f, (const char *)sqlite3_column_text(st, c), c == nc - 1);
                }
            }
            rows++;
        }
        sqlite3_finalize(st);
    }
    bool ok = fclose(f) == 0;
    return ok ? rows : -1;
}

/* (documented in stats_db_internal.h) */
void sdb_do_export(void)
{
    if (!sdb_db()) {
        return;
    }
    mkdir(EXPORT_DIR, 0775);                        /* fine if it's there */
    int solves = csv_query(EXPORT_DIR "/solves.csv",
                           "id,player,puzzle,level,number,seconds,mistakes,hints,started,finished",
                           "SELECT s.id, p.name, s.puzzle, s.level, s.number, s.seconds, s.mistakes, s.hints, s.started_at, s.finished_at"
                           " FROM solves s JOIN players p ON p.id = s.player_id ORDER BY s.id", (1u << 8) | (1u << 9));

    /* achievements: every player, every achievement, with progress */
    int ach_rows = -1;
    FILE *f = fopen(EXPORT_DIR "/achievements.csv", "w");
    int32_t ids[STATS_MAX_PLAYERS];
    char names[STATS_MAX_PLAYERS][STORE_NAME_LEN];
    int np = 0;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(sdb_db(), "SELECT id, name FROM players ORDER BY id", -1, &st, NULL) == SQLITE_OK) {
        while (np < STATS_MAX_PLAYERS && sqlite3_step(st) == SQLITE_ROW) {
            ids[np] = sqlite3_column_int(st, 0);
            const char *n = (const char *)sqlite3_column_text(st, 1);
            snprintf(names[np++], STORE_NAME_LEN, "%s", n ? n : "");
        }
        sqlite3_finalize(st);
    }
    if (f) {
        ach_rows = 0;
        fputs("player,key,achievement,description,progress,goal,unlocked,unlocked_at\n", f);
        for (int p = 0; p < np; p++) {
            int32_t val[STATS_ACH_MAX];
            int64_t when[STATS_ACH_MAX];
            sdb_ach_values(ids[p], val);
            sdb_ach_unlocked(ids[p], when);
            for (int i = 0; i < sdb_ach_count(); i++) {
                char a[16], b[16], t[24];
                csv_field(f, names[p], false);
                csv_field(f, sdb_ach(i)->key, false);
                csv_field(f, sdb_ach(i)->title, false);
                csv_field(f, sdb_ach(i)->desc, false);
                snprintf(a, sizeof(a), "%ld", (long)(when[i] || val[i] > sdb_ach(i)->goal ? sdb_ach(i)->goal : val[i]));
                snprintf(b, sizeof(b), "%ld", (long)sdb_ach(i)->goal);
                csv_field(f, a, false);
                csv_field(f, b, false);
                csv_field(f, when[i] ? "yes" : "no", false);
                csv_time(t, sizeof(t), when[i] > 1 ? when[i] : 0);
                csv_field(f, t, true);
                ach_rows++;
            }
        }
        if (fclose(f) != 0) {
            ach_rows = -1;
        }
    }

    /* records: every player */
    int rec_rows = -1;
    static stats_records_t *R;
    f = sdb_psram_buf((void **)&R, sizeof(*R)) ? fopen(EXPORT_DIR "/records.csv", "w") : NULL;
    if (f) {
        rec_rows = 0;
        fputs("player,record,value,unit,puzzle,set_on\n", f);
        static const char *const UNIT[] = {"seconds", "count", "days", "minutes"};
        for (int p = 0; p < np; p++) {
            sdb_records_fill(ids[p], R);
            for (int i = 0; i < R->n; i++) {
                const stats_record_t *r = &R->r[i];
                char v[16] = "", pz[16] = "", t[24];
                if (r->has) {
                    snprintf(v, sizeof(v), "%ld", (long)r->value);
                }
                if (r->has && r->puzzle != STORE_NO_GAME) {
                    snprintf(pz, sizeof(pz), "%u", (unsigned)r->puzzle);
                }
                csv_field(f, names[p], false);
                csv_field(f, r->title, false);
                csv_field(f, v, false);
                csv_field(f, UNIT[r->unit < 4 ? r->unit : 1], false);
                csv_field(f, pz, false);
                csv_time(t, sizeof(t), r->has ? r->when : 0);
                csv_field(f, t, true);
                rec_rows++;
            }
        }
        if (fclose(f) != 0) {
            rec_rows = -1;
        }
    }
    char msg[96];
    if (solves < 0 || ach_rows < 0 || rec_rows < 0) {
        snprintf(msg, sizeof(msg), "Export failed: couldn't write the files in export/ on the card.");
    } else {
        snprintf(msg, sizeof(msg), "Exported %d solve%s, achievements and records to export/ on the card.", solves,
                 solves == 1 ? "" : "s");
    }
    printf("stats_db: %s\n", msg);
    sdb_post_message(msg);
}

/**
 * Where backups go. A backup is a consistent copy of sudoku.db as backup/sudoku_\<unix time\>.db
 * (VACUUM INTO: one transaction, so pending writes are either all in it or not at all; it is
 * compacted too). Without the clock (no Wi-Fi yet) the time is small and may repeat after a
 * restart: a _2, _3, ... suffix keeps an older file.
 */
#define BACKUP_DIR BSP_SD_MOUNT_POINT "/backup"

/* (documented in stats_db_internal.h) */
void sdb_do_backup(void)
{
    if (!sdb_db()) {
        return;
    }
    mkdir(BACKUP_DIR, 0775);                        /* fine if it's there */
    long long ts = (long long)time(NULL);
    char path[96];
    struct stat st;
    snprintf(path, sizeof(path), BACKUP_DIR "/sudoku_%lld.db", ts);
    for (int n = 2; stat(path, &st) == 0 && n < 100; n++) {
        snprintf(path, sizeof(path), BACKUP_DIR "/sudoku_%lld_%d.db", ts, n);
    }
    char msg[160];                                  /* (the longest: "Backup failed: ..." with a 95-byte name) */
    sqlite3_stmt *q = NULL;
    int rc = sqlite3_prepare_v2(sdb_db(), "VACUUM INTO ?", -1, &q, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(q, 1, path, -1, SQLITE_STATIC);
        rc = sqlite3_step(q);
    }
    sqlite3_finalize(q);
    const char *name = path + strlen(BSP_SD_MOUNT_POINT "/");
    if (rc == SQLITE_DONE && stat(path, &st) == 0) {
        snprintf(msg, sizeof(msg), "Backed up to %s (%ld KB) on the card.", name, (long)((st.st_size + 1023) / 1024));
    } else {
        printf("stats_db: backup: %s\n", sqlite3_errmsg(sdb_db()));
        unlink(path);                               /* no half-written copy left behind */
        snprintf(msg, sizeof(msg), "Backup failed: couldn't write %s on the card.", name);
    }
    printf("stats_db: %s\n", msg);
    sdb_post_message(msg);
}

/* (documented in stats_db.h) */
void stats_db_export_csv(void)
{
    cmd_t c = {.type = CMD_EXPORT};
    if (!sdb_send(&c)) {
        sdb_post_message("Export: no database.");
    }
}

/* (documented in stats_db.h) */
void stats_db_backup(void)
{
    cmd_t c = {.type = CMD_BACKUP};
    if (!sdb_send(&c)) {
        sdb_post_message("Backup: no database.");
    }
}
