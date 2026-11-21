/**
 * @file gameplays_list.c
 * @ingroup gameplays_list
 * @brief The Gameplays list's data: its pages of recorded solves, and opening one.
 */
#include "gameplays_list.h"
#include "replay_video.h"
#include "esp_heap_caps.h"

static int pl_offset;                               /**< the page shown's first row */
static int pl_total;                                /**< rows in all */
static int64_t pl_row_solve[STATS_MAX_DAY_ROWS];    /**< each row's solves.id */

/* (documented in gameplays_list.h) */
bool gameplays_ask(int offset)
{
    if (!is_stats_db_ready()) {
        pl_total = 0;
        return false;
    }
    stats_db_query_plays(offset < 0 ? 0 : offset);
    return true;
}

/* (documented in gameplays_list.h) */
const stats_plays_t *gameplays_take(void)
{
    static stats_plays_t *p;                        /* PSRAM: internal RAM is tight */
    if (!p) {
        p = heap_caps_malloc(sizeof(*p), MALLOC_CAP_SPIRAM);
    }
    if (!p || !stats_db_take_plays(p)) {
        return NULL;
    }
    pl_offset = p->offset;
    pl_total = p->total;
    for (int i = 0; i < p->n; i++) {
        pl_row_solve[i] = p->rows[i].solve_id;
    }
    return p;
}

/* (documented in gameplays_list.h) */
int gameplays_offset(void)
{
    return pl_offset;
}

/* (documented in gameplays_list.h) */
bool is_gameplays_newer(void)
{
    return pl_offset > 0;
}

/* (documented in gameplays_list.h) */
bool is_gameplays_older(void)
{
    return pl_offset + STATS_MAX_DAY_ROWS < pl_total;
}

/* (documented in gameplays_list.h) */
void gameplays_open(int i, bool to_video)
{
    if (i < 0 || i >= STATS_MAX_DAY_ROWS || !pl_row_solve[i] || is_replay_busy()) {
        return;
    }
    replay_set_want_export(to_video);
    stats_db_query_replay(pl_row_solve[i]);
}
