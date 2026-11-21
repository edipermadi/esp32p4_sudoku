/**
 * @file puzzles_starred.c
 * @ingroup puzzles_starred
 * @brief The current player's starred puzzles (bookmarks), for the Puzzles screen.
 */
#include "puzzles_starred.h"
#include "stats_db.h"

static uint16_t l_star_ids[STATS_MAX_BOOKMARKS];   /**< the starred puzzles, newest first */
static int l_nstar;                                /**< how many */

/* (documented in puzzles_starred.h) */
int puzzles_load_starred(void)
{
    int n = stats_db_bookmarks(l_star_ids, STATS_MAX_BOOKMARKS);
    l_nstar = n < 0 ? 0 : n;
    return n;
}

/* (documented in puzzles_starred.h) */
int puzzles_starred_count(void)
{
    return l_nstar;
}

/* (documented in puzzles_starred.h) */
uint16_t puzzles_starred_id(int i)
{
    return l_star_ids[i];
}

/* (documented in puzzles_starred.h) */
bool puzzles_unstar(int i, uint16_t *id)
{
    if (i < 0 || i >= l_nstar) {
        return false;
    }
    *id = l_star_ids[i];
    stats_db_set_bookmark(*id, false);
    return true;
}
