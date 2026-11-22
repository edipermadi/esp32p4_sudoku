/**
 * @file statistics_core.c
 * @ingroup statistics_core
 * @brief The Statistics screen's data, without LVGL: the row picked, the view (from sudoku.db or
 *        RAM), totals, and the chart's numbers.
 */
#include "statistics_core.h"
#include "puzzles.h"
#include "store.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"

static stats_view_t *x_view_p;           /**< the view shown (PSRAM: internal RAM is tight) */
static bool x_from_db;                   /**< it came from sudoku.db (else from RAM) */
static int x_sel;                        /**< 0 = all levels, 1.. = collection x_sel-1, then the kinds (STATS_LEVEL_KIND) */

/**
 * @brief Adds a puzzle's statistics from RAM to a level's.
 * @param[in,out] st  the level
 * @param[in] id  the puzzle
 */
static void add_puzzle_stat(stats_level_t *st, uint16_t id)
{
    uint32_t t = store_best_time(id);
    if (!t) {
        return;
    }
    st->solved++;
    st->solves += store_solve_count(id);
    st->time_sum += t;
    if (!st->fastest || t < st->fastest) {
        st->fastest = t;
    }
    uint8_t m = store_fewest_mistakes(id);
    if (m != STORE_MISTAKES_UNKNOWN) {
        st->known++;
        st->miss_sum += m;
        if (m == 0) {
            st->perfect++;
        }
    }
}

/**
 * @brief The fallback when there is no sudoku.db: this session's RAM statistics as a view.
 * @param[in] level  -1 all levels, or a collection
 * @param[out] v      gets the view
 */
static void view_from_ram(int level, stats_view_t *v)
{
    memset(v, 0, sizeof(*v));
    v->level = level;
    for (int c = 0; c < PUZZLE_COLLECTION_COUNT; c++) {
        const puzzle_collection_t *pc = puzzle_collection(c);
        for (uint16_t i = 0; i < pc->count; i++) {
            add_puzzle_stat(&v->levels[c], pc->first + i);
            add_puzzle_stat(&v->total, pc->first + i);
            if (c != level || !store_best_time(pc->first + i) || v->n_puzzles >= STATS_MAX_PUZZLES) {
                continue;
            }
            stats_puzzle_t *p = &v->puzzles[v->n_puzzles++];
            p->number = i + 1;
            p->best = (uint16_t)store_best_time(pc->first + i);
            p->fewest = store_fewest_mistakes(pc->first + i);
            p->solves = store_solve_count(pc->first + i);
        }
    }
}

/* (documented in statistics_core.h) */
bool statistics_init(void)
{
    if (!x_view_p) {
        x_view_p = heap_caps_calloc(1, sizeof(stats_view_t), MALLOC_CAP_SPIRAM);
    }
    return x_view_p != NULL;
}

/* (documented in statistics_core.h) */
void statistics_select(int row)
{
    x_sel = row;
}

/* (documented in statistics_core.h) */
int statistics_sel(void)
{
    return x_sel;
}

/* (documented in statistics_core.h) */
bool statistics_ask(void)
{
    if (is_stats_db_ready()) {
        stats_db_query(x_sel - 1);
        return true;
    }
    view_from_ram(x_sel - 1, x_view_p);
    x_from_db = false;
    return false;
}

/* (documented in statistics_core.h) */
void statistics_take(const stats_view_t *v)
{
    *x_view_p = *v;
    x_from_db = true;
}

/* (documented in statistics_core.h) */
const stats_view_t *statistics_view(void)
{
    return x_view_p;
}

/* (documented in statistics_core.h) */
bool is_statistics_from_db(void)
{
    return x_from_db;
}

/* (documented in statistics_core.h) */
void statistics_kind_total(const stats_view_t *v, int k, stats_level_t *t)
{
    memset(t, 0, sizeof(*t));
    for (int l = 0; l < GEN_LEVELS; l++) {
        const stats_level_t *x = &v->kinds[k][l];
        t->solved += x->solved;
        t->perfect += x->perfect;
        t->known += x->known;
        t->solves += x->solves;
        t->time_sum += x->time_sum;
        t->miss_sum += x->miss_sum;
        if (x->fastest && (!t->fastest || x->fastest < t->fastest)) {
            t->fastest = x->fastest;
        }
    }
}

/* (documented in statistics_core.h) */
void statistics_avg_mistakes(char *buf, size_t n, const stats_level_t *st)
{
    if (!st->known) {
        snprintf(buf, n, "-");
        return;
    }
    uint32_t x10 = (st->miss_sum * 10 + st->known / 2) / st->known;
    snprintf(buf, n, "%lu.%lu", (unsigned long)(x10 / 10), (unsigned long)(x10 % 10));
}

/* (documented in statistics_core.h) */
uint32_t statistics_chart_step(uint32_t max)
{
    static const uint32_t STEPS[] = {30, 60, 120, 300, 600, 900, 1200, 1800, 3600, 7200};
    for (size_t i = 0; i < sizeof(STEPS) / sizeof(STEPS[0]); i++) {
        if (max <= STEPS[i] * 5) {
            return STEPS[i];
        }
    }
    return (max / 5 / 3600 + 1) * 3600;
}

/* (documented in statistics_core.h) */
uint32_t statistics_chart_avg(const stats_view_t *v, int i)
{
    int from = i - AVG_WIN + 1 < 0 ? 0 : i - AVG_WIN + 1;
    uint32_t sum = 0;
    for (int k = from; k <= i; k++) {
        sum += v->recent[k];
    }
    return sum / (uint32_t)(i - from + 1);
}

/* (documented in statistics_core.h) */
uint32_t statistics_fastest(const stats_view_t *v)
{
    uint32_t fastest = 0xFFFFFFFF;
    for (int i = 0; i < v->n_recent; i++) {
        if (v->recent[i] < fastest) {
            fastest = v->recent[i];
        }
    }
    return fastest;
}

/* (documented in statistics_core.h) */
bool statistics_trend(const stats_view_t *v, uint32_t *first, uint32_t *last, int *pct)
{
    const int n = v->n_recent;
    if (n < 2 * AVG_WIN) {
        return false;
    }
    uint32_t f = 0, l = 0;                          /* newest 5 against the first 5 shown */
    for (int i = 0; i < AVG_WIN; i++) {
        f += v->recent[i];
        l += v->recent[n - AVG_WIN + i];
    }
    f /= AVG_WIN;
    l /= AVG_WIN;
    *first = f;
    *last = l;
    *pct = f ? (int)(((int64_t)f - (int64_t)l) * 100 / (int64_t)f) : 0;
    return true;
}
