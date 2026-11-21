/**
 * @file puzzles_select.c
 * @ingroup puzzles_select
 * @brief What the Puzzles screen shows: the tab, the collection, Starred.
 */
#include "puzzles_select.h"
#include "collections.h"
#include "puzzles.h"

static int l_tab;                         /**< the tab shown: 0 the built-in collections, 1 the player's (imported and saved) */
static int l_tab_coll[2] = {0, -1};       /**< the collection last shown in each tab */
static int l_coll;                        /**< the collection shown */
static bool l_starred;                    /**< the right pane shows the starred puzzles */

/* (documented in puzzles_select.h) */
int puzzles_tab(void)
{
    return l_tab;
}

/* (documented in puzzles_select.h) */
void puzzles_set_tab(int t)
{
    l_tab = t;
}

/* (documented in puzzles_select.h) */
int puzzles_coll(void)
{
    return l_coll;
}

/* (documented in puzzles_select.h) */
void puzzles_set_coll(int c)
{
    l_coll = c;
}

/* (documented in puzzles_select.h) */
bool is_puzzles_starred(void)
{
    return l_starred;
}

/* (documented in puzzles_select.h) */
void puzzles_set_starred(bool on)
{
    l_starred = on;
}

/* (documented in puzzles_select.h) */
bool puzzles_settle(void)
{
    if (l_coll >= coll_total()) {
        l_coll = 0;
    }
    if (coll_tab(l_coll) != l_tab) {               /* the tab shown decides: its last, or its first */
        int c = l_tab_coll[l_tab];
        if (c < 0 || c >= coll_total() || coll_tab(c) != l_tab) {
            c = l_tab ? PUZZLE_COLLECTION_COUNT : 0;
        }
        if (c < coll_total()) {
            l_coll = c;
        }
    }
    bool none = coll_tab(l_coll) != l_tab;          /* the Saved tab, with nothing in it */
    if (!none) {
        l_tab_coll[l_tab] = l_coll;
    }
    return !none;
}

/* (documented in puzzles_select.h) */
void puzzles_forget_saved(void)
{
    l_coll = PUZZLE_COLLECTION_COUNT;               /* the Saved tab's first, if any */
    l_tab_coll[1] = -1;
}

/* (documented in puzzles_select.h) */
void puzzles_follow(int c)
{
    l_coll = c;
    l_tab = coll_tab(c);
}
