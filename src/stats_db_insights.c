/**
 * @file stats_db_insights.c
 * @brief Mistake insights (Statistics > ⋮ > Insights): the move logs of the last solves looked through for wrong digits.
 */
#include "stats_db_internal.h"

static stats_insights_t *s_ins;      /**< CMD_INSIGHTS result (PSRAM) */
static bool s_ins_new;               /**< s_ins is new */

/* (documented in stats_db_internal.h) */
void sdb_do_insights(void)
{
    static stats_insights_t *ip;
    static game_move_t *mv;
    if (!sdb_psram_buf((void **)&ip, sizeof(*ip)) || !sdb_psram_buf((void **)&s_ins, sizeof(*s_ins)) ||
            !sdb_psram_buf((void **)&mv, sizeof(game_move_t) * GAME_MOVES_MAX)) {
        return;
    }
    stats_insights_t *const in = ip;
    memset(in, 0, sizeof(*in));
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(),
                                       "SELECT s.moves FROM solves s LEFT JOIN generated g ON s.puzzle = 2000 + g.id"
                                       " WHERE s.player_id = ?1 AND s.moves IS NOT NULL AND COALESCE(g.variant, 0) != 2"   /* (not 6x6) */
                                       " ORDER BY s.id DESC LIMIT ?2", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, sdb_player());
        sqlite3_bind_int(st, 2, STATS_INSIGHT_SOLVES);
        while (sqlite3_step(st) == SQLITE_ROW) {
            int n = moves_unpack((const uint8_t *)sqlite3_column_blob(st, 0), sqlite3_column_bytes(st, 0), mv,
                                 GAME_MOVES_MAX);
            if (n <= 0) {
                continue;
            }
            uint8_t last[81] = {0};                 /* the solved board (the cells the player filled) */
            for (int i = 0; i < n; i++) {
                last[mv[i].cell] = mv[i].digit;
            }
            in->solves++;
            for (int i = 0; i < n; i++) {
                const game_move_t *m = &mv[i];
                if (m->kind != MOVE_HAND || !m->digit || !last[m->cell]) {
                    continue;    /* by hand, a digit */
                }
                in->placed++;
                if (m->digit == last[m->cell]) {
                    continue;
                }
                in->wrong++;
                in->by_digit[m->digit]++;
                in->by_box[(m->cell / 27) * 3 + (m->cell % 9) / 3]++;
                in->pair[m->digit][last[m->cell]]++;
            }
        }
        sqlite3_finalize(st);
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    *s_ins = *in;
    s_ins_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db.h) */
void stats_db_query_insights(void)
{
    cmd_t c = {.type = CMD_INSIGHTS};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_insights(stats_insights_t *out)
{
    if (!sdb_lock() || !s_ins) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_ins_new;
    if (fresh) {
        *out = *s_ins;
    }
    s_ins_new = false;
    xSemaphoreGive(sdb_lock());
    return fresh;
}
