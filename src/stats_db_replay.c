/**
 * @file stats_db_replay.c
 * @brief A solve's replay: its move log and puzzle loaded for the Replay screen.
 */
#include "stats_db_internal.h"

static stats_replay_t *s_replay;        /**< CMD_REPLAY result (PSRAM) */
static bool s_replay_new;               /**< s_replay is new */

/* (documented in stats_db_internal.h) */
void sdb_do_replay(int64_t id)
{
    if (!s_replay && !(s_replay = heap_caps_calloc(1, sizeof(*s_replay), MALLOC_CAP_SPIRAM))) {
        return;
    }
    static stats_replay_t *r;
    if (!r) {
        r = heap_caps_malloc(sizeof(*r), MALLOC_CAP_SPIRAM);
    }
    if (!r) {
        return;
    }
    memset(r, 0, offsetof(stats_replay_t, moves));
    r->solve_id = id;
    sqlite3_stmt *st;
    if (sdb_db() && sqlite3_prepare_v2(sdb_db(),
                                       "SELECT s.puzzle, s.seconds, s.mistakes, s.hints, s.moves, p.name, s.finished_at"
                                       " FROM solves s JOIN players p ON p.id = s.player_id WHERE s.id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            int pz = sqlite3_column_int(st, 0);
            const uint8_t *mb = (const uint8_t *)sqlite3_column_blob(st, 4);
            if (puzzle_id_valid(pz) && mb) {
                r->found = true;
                r->puzzle = (uint16_t)pz;
                r->seconds = (uint32_t)sqlite3_column_int64(st, 1);
                r->mistakes = (uint32_t)sqlite3_column_int(st, 2);
                r->hints = (uint32_t)sqlite3_column_int(st, 3);
                r->n = moves_unpack(mb, sqlite3_column_bytes(st, 4), r->moves, GAME_MOVES_MAX);
                const char *name = (const char *)sqlite3_column_text(st, 5);
                snprintf(r->player, sizeof(r->player), "%s", name ? name : "");
                r->finished_at = sqlite3_column_int64(st, 6);
            }
        }
        sqlite3_finalize(st);
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    memcpy(s_replay, r, offsetof(stats_replay_t, moves) + sizeof(game_move_t) * (size_t)r->n);
    s_replay_new = true;
    xSemaphoreGive(sdb_lock());
}

/* (documented in stats_db.h) */
void stats_db_query_replay(int64_t solve_id)
{
    cmd_t c = {.type = CMD_REPLAY, .solve_id = solve_id};
    sdb_send(&c);
}

/* (documented in stats_db.h) */
bool stats_db_take_replay(stats_replay_t *out)
{
    if (!sdb_lock() || !s_replay) {
        return false;
    }
    xSemaphoreTake(sdb_lock(), portMAX_DELAY);
    bool fresh = s_replay_new;
    if (fresh) {
        size_t head = offsetof(stats_replay_t, moves);
        memcpy(out, s_replay, head + sizeof(game_move_t) * (size_t)s_replay->n);
        s_replay_new = false;
    }
    xSemaphoreGive(sdb_lock());
    return fresh;
}
