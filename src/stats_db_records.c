/**
 * @file stats_db_records.c
 * @brief Personal records.
 */
#include "stats_db_internal.h"

static stats_records_t *s_records;   /**< CMD_RECORDS result (PSRAM) */
static bool s_records_new;           /**< s_records is new */

/* ------------------------------------------------------------- personal records */

/** @brief A fastest-solve record: of everything, or of a kind (daily ones of a variant count for it too). */
typedef struct {
    const char *title;                  /**< shown */
    const char *where;                  /**< a condition on solves */
} rec_fast_t;

/** The fastest-solve records. */
static const rec_fast_t REC_FAST[] = {
    {"Fastest solve", "1"},
    {"Fastest without a mistake or a hint", "mistakes = 0 AND hints = 0"},
    {"Fastest from the collections", "puzzle < 2000"},
    {
        "Fastest New puzzle (Classic)",
        "puzzle IN (SELECT 2000 + id FROM generated WHERE variant = 0 AND day IS NULL AND source IS NULL)"
    },
    {"Fastest imported or own puzzle", "puzzle IN (SELECT 2000 + id FROM generated WHERE source IS NOT NULL)"},
    {"Fastest daily puzzle", "puzzle IN (SELECT 2000 + id FROM generated WHERE day IS NOT NULL)"},
    {"Fastest X-Sudoku", OF_VARIANT(1)},
    {"Fastest Hyper", OF_VARIANT(3)},
    {"Fastest Jigsaw", OF_VARIANT(4)},
    {"Fastest Killer", OF_VARIANT(5)},
    {"Fastest Anti-Knight", OF_VARIANT(6)},
    {"Fastest Even-Odd", OF_VARIANT(7)},
    {"Fastest Consecutive", OF_VARIANT(8)},
    {"Fastest Thermo", OF_VARIANT(9)},
    {"Fastest Sandwich", OF_VARIANT(10)},
    {"Fastest Mini 6x6", OF_VARIANT(2)},
    {"Fastest Kids 4x4", OF_VARIANT(11)},
    {"Fastest Anti-King", OF_VARIANT(12)},
    {"Fastest Non-Consecutive", OF_VARIANT(13)},
    {"Fastest Disjoint Groups", OF_VARIANT(14)},
    {"Fastest Asterisk", OF_VARIANT(15)},
    {"Fastest Kropki", OF_VARIANT(16)},
    {"Fastest XV", OF_VARIANT(17)},
    {"Fastest Greater-Than", OF_VARIANT(18)},
    {"Fastest Quadruple", OF_VARIANT(19)},
    {"Fastest Arrow", OF_VARIANT(20)},
    {"Fastest Renban", OF_VARIANT(21)},
    {"Fastest German Whispers", OF_VARIANT(22)},
    {"Fastest Skyscraper", OF_VARIANT(23)},
    {"Fastest Little Killer", OF_VARIANT(24)},
    {"Fastest Palindrome", OF_VARIANT(25)},
    {"Fastest Region Sum Lines", OF_VARIANT(26)},
    {"Fastest X-Sums", OF_VARIANT(27)},
    {"Fastest Zipper Lines", OF_VARIANT(28)},
    {"Fastest Frame", OF_VARIANT(29)},
    {"Fastest Between Lines", OF_VARIANT(30)},
    {"Fastest Entropic Lines", OF_VARIANT(31)},
    {"Fastest Modular Lines", OF_VARIANT(32)},
    {"Fastest Center Dot", OF_VARIANT(33)},
    {"Fastest Outside Sudoku", OF_VARIANT(34)},
    {"Fastest Rossini", OF_VARIANT(35)},
    {"Fastest Diagonal Non-Consecutive", OF_VARIANT(36)},
    {"Fastest Dutch Whispers", OF_VARIANT(37)},
    {"Fastest Parity Lines", OF_VARIANT(38)},
    {"Fastest Slow Thermo", OF_VARIANT(39)},
    {"Fastest Fortress", OF_VARIANT(40)},
    {"Fastest Argyle", OF_VARIANT(41)},
    {"Fastest Nabner", OF_VARIANT(42)},
    {"Fastest Numbered Rooms", OF_VARIANT(43)},
    {"Fastest Sum Skyscrapers", OF_VARIANT(44)},
    {"Fastest Lockout Lines", OF_VARIANT(45)},
    {"Fastest Sumless Killer", OF_VARIANT(46)},
    {"Fastest Double Arrow", OF_VARIANT(47)},
    {"Fastest Diagonal Consecutive", OF_VARIANT(48)},
    {"Fastest 10 Lines", OF_VARIANT(49)},
    {"Fastest Girandola", OF_VARIANT(50)},
    {"Fastest 159 Sudoku", OF_VARIANT(51)},
    {"Fastest Product Killer", OF_VARIANT(52)},
    {"Fastest Extra Regions", OF_VARIANT(53)},
};

#define REC_OTHERS 5                    /**< the records after the fastest ones (a day, a run, a streak, solved, time) */
_Static_assert(sizeof(REC_FAST) / sizeof(REC_FAST[0]) + REC_OTHERS <= STATS_MAX_RECORDS, "raise STATS_MAX_RECORDS");

/* (documented in stats_db_internal.h) */
void sdb_records_fill(int32_t player, stats_records_t *R)
{
    const int32_t me = sdb_player();
    sdb_set_player(player);                              /* (the helpers below ask about sdb_player()) */
    memset(R, 0, sizeof(*R));
    R->player_id = sdb_player();
    sqlite3_stmt *st;
    char sql[320];
    for (size_t i = 0; i < sizeof(REC_FAST) / sizeof(REC_FAST[0]); i++) {
        stats_record_t *r = &R->r[R->n++];
        r->title = REC_FAST[i].title;
        r->unit = REC_TIME;
        r->puzzle = STORE_NO_GAME;
        snprintf(sql, sizeof(sql), "SELECT seconds, puzzle, finished_at FROM solves WHERE player_id = ?1 AND %s"
                 " ORDER BY seconds, id LIMIT 1", REC_FAST[i].where);
        if (!sdb_db() || sqlite3_prepare_v2(sdb_db(), sql, -1, &st, NULL) != SQLITE_OK) {
            continue;
        }
        sqlite3_bind_int(st, 1, sdb_player());
        if (sqlite3_step(st) == SQLITE_ROW) {
            r->has = true;
            r->value = sqlite3_column_int(st, 0);
            r->puzzle = (uint16_t)sqlite3_column_int(st, 1);
            r->when = sqlite3_column_int64(st, 2);
        }
        sqlite3_finalize(st);
    }

    /* most solves in one day (local date) */
    stats_record_t *r = &R->r[R->n++];
    r->title = "Most solves in a day";
    r->unit = REC_COUNT;
    r->puzzle = STORE_NO_GAME;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT COUNT(*) AS n, MIN(finished_at) FROM solves"
                                       " WHERE player_id = ?1 AND finished_at IS NOT NULL"
                                       " GROUP BY date(finished_at, 'unixepoch', 'localtime') ORDER BY n DESC, 2 DESC"
                                       " LIMIT 1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        if (sqlite3_step(st) == SQLITE_ROW) {
            r->has = true;
            r->value = sqlite3_column_int(st, 0);
            r->when = sqlite3_column_int64(st, 1);
        }
        sqlite3_finalize(st);
    }

    /* longest run of clean solves (no mistake, no hint), in the order they were solved */
    r = &R->r[R->n++];
    r->title = "Clean solves in a row";
    r->unit = REC_COUNT;
    r->puzzle = STORE_NO_GAME;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT mistakes = 0 AND hints = 0, finished_at FROM solves"
                                       " WHERE player_id = ?1 ORDER BY id", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        int run = 0;
        while (sqlite3_step(st) == SQLITE_ROW) {
            run = sqlite3_column_int(st, 0) ? run + 1 : 0;
            if (run > r->value) {
                r->value = run;
                r->when = sqlite3_column_int64(st, 1);     /* when the best run reached its length */
            }
        }
        r->has = r->value > 0;
        sqlite3_finalize(st);
    }

    /* longest daily streak, total solves, total time */
    r = &R->r[R->n++];
    r->title = "Longest daily streak";
    r->unit = REC_DAYS;
    r->puzzle = STORE_NO_GAME;
    r->value = sdb_db() ? sdb_longest_daily_streak(sdb_player()) : 0;
    r->has = r->value > 0;
    r = &R->r[R->n++];
    r->title = "Puzzles solved";
    r->unit = REC_COUNT;
    r->puzzle = STORE_NO_GAME;
    r->value = sdb_db() ? sdb_query_int_player("SELECT COUNT(*) FROM solves WHERE player_id = ?1") : 0;
    r->has = r->value > 0;
    r = &R->r[R->n++];
    r->title = "Time spent solving";
    r->unit = REC_MINUTES;
    r->puzzle = STORE_NO_GAME;
    r->value = sdb_db() ? sdb_query_int_player("SELECT CAST(TOTAL(seconds) / 60 AS INTEGER) FROM solves WHERE player_id = ?1")
               : 0;
    r->has = r->value > 0;
    sdb_set_player(me);
}

/* (documented in stats_db_internal.h) */
void sdb_do_records(void)
{
    static stats_records_t *R;
    static stats_records_t *out;
    if (!sdb_psram_buf((void **)&R, sizeof(*R)) || !sdb_psram_buf((void **)&out, sizeof(*out))) {
        return;
    }
    sdb_records_fill(sdb_player(), R);
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    *out = *R;
    s_records = out;
    s_records_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db.h) */
void stats_db_query_records(void)
{
    cmd_t c = {.type = CMD_RECORDS};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_records(stats_records_t *out)
{
    if (!sdb_lock() || !s_records) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_records_new;
    if (fresh) {
        *out = *s_records;
    }
    s_records_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}
