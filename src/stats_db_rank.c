/**
 * @file stats_db_rank.c
 * @brief The daily puzzle's rank and leaderboard, and head-to-head results on a puzzle.
 */
#include "stats_db_internal.h"

static stats_board_t *s_board;       /**< CMD_BOARD result (PSRAM) */
static bool s_board_new;             /**< s_board is new */

/* ------------------------------------------------------------- daily rank and leaderboard */

/** SQL: everyone's best solve of a puzzle (?1), fastest first */
#define BOARD_SQL \
    "SELECT p.id, p.name, b.best, b.mistakes, b.hints, b.n, " PLAYER_COLOR("p") ", b.flags FROM players p LEFT JOIN (" \
    "  SELECT player_id, MIN(seconds) AS best, mistakes, hints, COUNT(*) AS n, flags FROM solves" \
    "   WHERE puzzle = ?1 GROUP BY player_id) b ON b.player_id = p.id" \
    " ORDER BY b.best IS NULL, b.best, b.mistakes, b.hints, p.id"

/**
 * @brief The daily puzzle of a day.
 * @param[in] day      'YYYY-MM-DD'
 * @param[out] level    gets its gen_level_t
 * @param[out] variant  gets its sudoku_variant_t
 * @return its generated.id (0 = not made)
 */
static int daily_id_of(const char *day, int *level, int *variant)
{
    int id = 0;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), "SELECT id, level, variant FROM generated WHERE day = ?1 ORDER BY id LIMIT 1",
                                       -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, day, -1, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            id = sqlite3_column_int(st, 0);
            if (level) {
                *level = sqlite3_column_int(st, 1);
            }
            if (variant) {
                *variant = sqlite3_column_int(st, 2);
            }
        }
        sqlite3_finalize(st);
    }
    return id;
}

/**
 * @brief Everyone's best solve of a puzzle, fastest first, into b->rows.
 * @param[in,out] b       the leaderboard
 * @param[in] puzzle  the puzzle id (-1: none, every player unsolved)
 */
static void board_rows(stats_board_t *b, int puzzle)
{
    sqlite3_stmt *st;
    if (!sdb_db() || sqlite3_prepare_v2(sdb_db(), BOARD_SQL, -1, &st, NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_int(st, 1, puzzle);
    while (b->n < STATS_MAX_PLAYERS && sqlite3_step(st) == SQLITE_ROW) {
        stats_board_row_t *r = &b->rows[b->n++];
        r->player_id = sqlite3_column_int(st, 0);
        const char *name = (const char *)sqlite3_column_text(st, 1);
        snprintf(r->player, sizeof(r->player), "%s", name ? name : "");
        r->color = (uint8_t)sqlite3_column_int(st, 6);
        r->solved = sqlite3_column_type(st, 2) != SQLITE_NULL;
        if (!r->solved) {
            continue;
        }
        b->solved++;
        r->seconds = (uint32_t)sqlite3_column_int64(st, 2);
        r->mistakes = (uint16_t)sqlite3_column_int(st, 3);
        r->hints = (uint16_t)sqlite3_column_int(st, 4);
        r->solves = (uint16_t)sqlite3_column_int(st, 5);
        r->flags = (uint32_t)sqlite3_column_int64(st, 7);
    }
    sqlite3_finalize(st);
}

/**
 * @brief A day's daily leaderboard.
 * @param[out] b    gets it
 * @param[in] day  'YYYY-MM-DD'
 */
static void board_fill(stats_board_t *b, const char *day)
{
    memset(b, 0, sizeof(*b));
    snprintf(b->day, sizeof(b->day), "%s", day);
    int gid = daily_id_of(day, &b->level, &b->variant);
    b->exists = gid > 0;
    board_rows(b, gid > 0 ? PUZZLE_GEN_BASE + gid : -1);
}

/* (documented in stats_db_internal.h) */
void sdb_do_puzzle_rank(void)
{
    static stats_board_t *b;
    stats_h2h_t *h = &s_gen.h2h;
    memset(h, 0, sizeof(*h));
    s_gen.ok = sdb_db() != NULL && sdb_psram_buf((void **)&b, sizeof(*b));
    if (s_gen.ok) {
        memset(b, 0, sizeof(*b));
        board_rows(b, s_gen.puzzle);
        h->of = b->solved;
        for (int i = 0; i < b->n; i++) {
            if (b->rows[i].player_id == sdb_player() && b->rows[i].solved) {
                h->rank = i + 1;
            }
        }
        /* the one to beat: the leader, or when that's this player, the runner-up */
        int who = h->rank == 1 ? 1 : 0;
        if (who < b->n && b->rows[who].solved && b->rows[who].player_id != sdb_player()) {
            snprintf(h->leader, sizeof(h->leader), "%s", b->rows[who].player);
            h->leader_seconds = b->rows[who].seconds;
            h->second = who == 1;
        }
    }
    sdb_gen_reply();
}

/* (documented in stats_db_internal.h) */
void sdb_do_board(const char *day)
{
    static stats_board_t *b;
    if (!sdb_psram_buf((void **)&b, sizeof(*b)) || !sdb_psram_buf((void **)&s_board, sizeof(*s_board))) {
        return;
    }
    board_fill(b, day);
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    *s_board = *b;
    s_board_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db_internal.h) */
void sdb_do_rank(void)
{
    static stats_board_t *b;
    s_gen.rank = s_gen.of = 0;
    s_gen.ok = sdb_db() != NULL && sdb_psram_buf((void **)&b, sizeof(*b));
    if (s_gen.ok) {
        board_fill(b, s_gen.day);
        s_gen.of = b->solved;
        for (int i = 0; i < b->n; i++) {
            if (b->rows[i].player_id == sdb_player() && b->rows[i].solved) {
                s_gen.rank = i + 1;
            }
        }
    }
    sdb_gen_reply();
}

/* (documented in stats_db.h) */
bool stats_db_daily_rank(const char *day, int *rank, int *of)
{
    if (!sdb_gen()) {
        return false;    /* no database this session */
    }
    snprintf(s_gen.day, sizeof(s_gen.day), "%s", day);
    if (!sdb_gen_call(CMD_RANK)) {
        return false;
    }
    *rank = s_gen.rank;
    *of = s_gen.of;
    return true;
}

/* (documented in stats_db.h) */
void stats_db_query_board(const char *day)
{
    cmd_t c = {.type = CMD_BOARD};
    snprintf(c.text, sizeof(c.text), "%s", day ? day : "");
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_board(stats_board_t *out)
{
    if (!sdb_lock() || !s_board) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_board_new;
    if (fresh) {
        *out = *s_board;
    }
    s_board_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}

/* (documented in stats_db_internal.h) */
int sdb_query_int_player(const char *sql)
{
    int v = 0;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(), sql, -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        if (sqlite3_step(st) == SQLITE_ROW) {
            v = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
    }
    return v;
}

/* (documented in stats_db.h) */
bool stats_db_puzzle_rank(uint16_t puzzle, stats_h2h_t *out)
{
    if (!sdb_gen()) {
        return false;
    }
    s_gen.puzzle = puzzle;
    if (!sdb_gen_call(CMD_PRANK)) {
        return false;
    }
    *out = s_gen.h2h;
    return true;
}
