/**
 * @file stats_db_sql.c
 * @brief Running SQL from the host (devctl sql).
 */
#include "stats_db_internal.h"

/** @brief stats_db_sql()'s call, run by the worker. */
typedef struct {
    const char *sql;                    /**< the statements */
    char *out;                          /**< the result lines */
    size_t cap;                         /**< out's size */
    size_t len;                         /**< out's length so far */
    stats_sql_result_t *r;              /**< what happened */
} sql_job_t;

static sql_job_t s_sqlj;                /**< the call (one at a time) */
static SemaphoreHandle_t s_sql_done;    /**< given when the worker has run it */

/**
 * @brief Appends to the result (or marks it truncated).
 * @param s  the text
 * @param n  its length
 */
static void sql_put(const char *s, size_t n)
{
    if (s_sqlj.len + n + 1 > s_sqlj.cap) {
        s_sqlj.r->truncated = true;
        return;
    }
    memcpy(s_sqlj.out + s_sqlj.len, s, n);
    s_sqlj.len += n;
    s_sqlj.out[s_sqlj.len] = 0;
}

/**
 * @brief Appends a column's value, escaped (backslash, tab, newline, carriage return), NULL as
 *        \\N, a blob as x'hex'.
 * @param st  the statement, on a row
 * @param i   the column
 */
static void sql_value(sqlite3_stmt *st, int i)
{
    char buf[48];
    switch (sqlite3_column_type(st, i)) {
    case SQLITE_NULL:
        sql_put("\\N", 2);
        break;
    case SQLITE_INTEGER:
        sql_put(buf, (size_t)snprintf(buf, sizeof(buf), "%lld", (long long)sqlite3_column_int64(st, i)));
        break;
    case SQLITE_FLOAT:
        sql_put(buf, (size_t)snprintf(buf, sizeof(buf), "%.17g", sqlite3_column_double(st, i)));
        break;
    case SQLITE_BLOB: {
        const uint8_t *b = sqlite3_column_blob(st, i);
        int n = sqlite3_column_bytes(st, i);
        sql_put("x'", 2);
        for (int k = 0; k < n && !s_sqlj.r->truncated; k++) {
            sql_put(buf, (size_t)snprintf(buf, sizeof(buf), "%02x", b[k]));
        }
        sql_put("'", 1);
        break;
    }
    default: {
        const char *t = (const char *)sqlite3_column_text(st, i);
        for (; t && *t; t++) {
            const char *e = *t == '\\' ? "\\\\" : *t == '\t' ? "\\t" : *t == '\n' ? "\\n" : *t == '\r' ? "\\r" : NULL;
            if (e) {
                sql_put(e, 2);
            } else {
                sql_put(t, 1);
            }
        }
        break;
    }
    }
}

/**
 * @brief SQLite's progress handler: stops a statement that runs past its time.
 * @param deadline  the time it must end (esp_timer, microseconds)
 * @return non-zero: interrupt it
 */
static int sql_progress(void *deadline)
{
    return esp_timer_get_time() > *(int64_t *)deadline;   /* past it: interrupted */
}

/* (documented in stats_db_internal.h) */
void sdb_do_sql(void)
{
    stats_sql_result_t *r = s_sqlj.r;
    s_sqlj.len = 0;
    s_sqlj.out[0] = 0;
    if (!sdb_db()) {
        snprintf(r->err, sizeof(r->err), "no sudoku.db");
        xSemaphoreGive(s_sql_done);
        return;
    }
    int64_t deadline = esp_timer_get_time() + 20000000;   /* 20 s, then interrupted */
    sqlite3_progress_handler(sdb_db(), 1000, sql_progress, &deadline);
    int changes0 = sqlite3_total_changes(sdb_db());
    const char *p = s_sqlj.sql;
    r->ok = true;
    while (p && *p) {
        sqlite3_stmt *st = NULL;
        const char *tail = NULL;
        if (sqlite3_prepare_v2(sdb_db(), p, -1, &st, &tail) != SQLITE_OK) {
            snprintf(r->err, sizeof(r->err), "%s", sqlite3_errmsg(sdb_db()));
            r->ok = false;
            break;
        }
        p = tail;
        if (!st) {
            continue;    /* (only white space or a comment) */
        }
        int nc = sqlite3_column_count(st);
        if (nc) {
            sql_put("C", 1);
            for (int i = 0; i < nc; i++) {
                const char *name = sqlite3_column_name(st, i);
                sql_put("\t", 1);
                sql_put(name, strlen(name));
            }
            sql_put("\n", 1);
        }
        int rc;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            r->rows++;
            sql_put("R", 1);
            for (int i = 0; i < nc; i++) {
                sql_put("\t", 1);
                sql_value(st, i);
            }
            sql_put("\n", 1);
        }
        if (rc != SQLITE_DONE) {
            snprintf(r->err, sizeof(r->err), "%s", rc == SQLITE_INTERRUPT ? "interrupted after 20 s" : sqlite3_errmsg(sdb_db()));
            r->ok = false;
        }
        sqlite3_finalize(st);
        if (!r->ok) {
            break;
        }
    }
    sqlite3_progress_handler(sdb_db(), 0, NULL, NULL);
    r->changes = sqlite3_total_changes(sdb_db()) - changes0;
    if (!sqlite3_get_autocommit(sdb_db())) {          /* a BEGIN without its COMMIT would hold up the game's writes */
        sdb_exec("ROLLBACK");
        r->ok = false;
        snprintf(r->err, sizeof(r->err), "a transaction was left open: rolled back");
    }
    xSemaphoreGive(s_sql_done);
}

/* (documented in stats_db.h) */
bool stats_db_sql(const char *sql, char *out, size_t cap, stats_sql_result_t *r)
{
    memset(r, 0, sizeof(*r));
    if (!is_stats_db_ready()) {
        snprintf(r->err, sizeof(r->err), "no sudoku.db (no card, or it's lent over USB)");
        return false;
    }
    if (!s_sql_done && !(s_sql_done = xSemaphoreCreateBinary())) {
        snprintf(r->err, sizeof(r->err), "no memory");
        return false;
    }
    xSemaphoreTake(s_sql_done, 0);                  /* clear a stale give */
    s_sqlj = (sql_job_t) {.sql = sql, .out = out, .cap = cap, .r = r};
    cmd_t c = {.type = CMD_SQL};
    if (!sdb_send(&c)) {
        snprintf(r->err, sizeof(r->err), "the database is busy");
        return false;
    }
    if (xSemaphoreTake(s_sql_done, pdMS_TO_TICKS(60000)) != pdTRUE) {   /* (20 s for the SQL, the queue before it) */
        snprintf(r->err, sizeof(r->err), "no answer from the database task");
        return false;
    }
    return r->ok;
}
