/**
 * @file puzzles_search.c
 * @ingroup puzzles_search
 * @brief The Puzzles screen's search (puzzles_search.h): every puzzle's name, variant and level matched against
 *        what was typed (search_core.h).
 */
#include "puzzles_search.h"
#include "search_core.h"
#include "collections.h"
#include "generator.h"
#include "puzzles.h"
#include "stats_db.h"
#include <stdio.h>
#include <string.h>

static char s_query[40];                        /**< the search last run */
static bool s_on;                               /**< the right pane shows the results */
static uint16_t s_ids[PUZZLES_SEARCH_MAX];      /**< the results kept */
static int s_n;                                 /**< how many */
static int s_total;                             /**< how many matched */
static uint8_t s_solved[(PUZZLES_SEARCH_MAX + 7) / 8];   /**< bit i: result i solved by the current player */

/**
 * @brief A match: counted, and kept while there's room.
 * @param[in] id  the puzzle
 */
static void found(uint16_t id)
{
    if (s_n < PUZZLES_SEARCH_MAX) {
        s_ids[s_n++] = id;
    }
    s_total++;
}

/* (documented in puzzles_search.h) */
int puzzles_search_run(const char *query)
{
    snprintf(s_query, sizeof(s_query), "%s", query);
    s_n = s_total = 0;
    s_on = true;
    char name[48], text[112];
    const stats_gen_row_t *rows;                    /* made on the board or imported: newest first */
    const int nrows = is_stats_db_ready() ? stats_db_gen_rows(&rows) : -1;
    for (int i = 0; i < nrows; i++) {
        const stats_gen_row_t *r = &rows[i];
        puzzle_name_from(r->id, r->variant, r->level, r->source, r->number, r->day, name, sizeof(name));
        snprintf(text, sizeof(text), "%s %s %s", name, r->variant < VAR_COUNT ? variant_name(r->variant) : "",
                 r->level >= 0 && r->level < GEN_LEVELS ? gen_level_name(r->level) : "");
        if (is_search_match(s_query, text)) {
            found(r->id);
        }
    }
    for (int id = 0; id < PUZZLE_COUNT; id++) {     /* the built-in ones: Classic */
        const puzzle_collection_t *c = puzzle_collection(puzzle_collection_of((uint16_t)id));
        snprintf(text, sizeof(text), "%s #%d %s", c->name, id - c->first + 1, variant_name(VAR_CLASSIC));
        if (is_search_match(s_query, text)) {
            found((uint16_t)id);
        }
    }
    memset(s_solved, 0, sizeof(s_solved));
    if (s_n && is_stats_db_ready()) {
        stats_db_solved_list(s_ids, s_n, s_solved);
    }
    return s_total;
}

/* (documented in puzzles_search.h) */
void puzzles_search_off(void) { s_on = false; }
/* (documented in puzzles_search.h) */
bool is_puzzles_search(void) { return s_on; }
/* (documented in puzzles_search.h) */
const char *puzzles_search_query(void) { return s_query; }
/* (documented in puzzles_search.h) */
int puzzles_search_total(void) { return s_total; }
/* (documented in puzzles_search.h) */
int puzzles_search_count(void) { return s_n; }
/* (documented in puzzles_search.h) */
uint16_t puzzles_search_id(int i) { return i >= 0 && i < s_n ? s_ids[i] : 0; }
/* (documented in puzzles_search.h) */
bool is_puzzles_search_solved(int i) { return i >= 0 && i < s_n && (s_solved[i / 8] >> (i % 8) & 1); }
