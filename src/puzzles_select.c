/**
 * @file puzzles_select.c
 * @ingroup puzzles_select
 * @brief What the Puzzles screen shows: the tab, the collection, Starred.
 */
#include "puzzles_select.h"
#include "collections.h"
#include "puzzles.h"

static int l_tab;                         /**< the tab shown: COLL_TAB_* */
static int l_tab_coll[COLL_TABS] = {0, -1, -1};   /**< the collection last shown in each tab (-1: none yet) */
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

/**
 * @brief A tab's first collection.
 * @param[in] t  the tab, COLL_TAB_*
 * @return it, or -1 if the tab has none
 */
static int tab_first(int t)
{
    for (int c = 0; c < coll_total(); c++) {
        if (coll_tab(c) == t) {
            return c;
        }
    }
    return -1;
}

/* (documented in puzzles_select.h) */
bool puzzles_settle(void)
{
    if (l_coll < 0 || l_coll >= coll_total() || coll_tab(l_coll) != l_tab) {   /* the tab shown decides */
        int c = l_tab_coll[l_tab];                  /* its last, or its first */
        if (c < 0 || c >= coll_total() || coll_tab(c) != l_tab) {
            c = tab_first(l_tab);
        }
        if (c >= 0) {
            l_coll = c;
        } else if (l_coll < 0 || l_coll >= coll_total()) {
            l_coll = 0;                             /* (a tab with nothing in it: some collection, not shown) */
        }
    }
    bool none = coll_tab(l_coll) != l_tab;          /* the Saved or Imported tab, with nothing in it */
    if (!none) {
        l_tab_coll[l_tab] = l_coll;
    }
    return !none;
}

/* (documented in puzzles_select.h) */
void puzzles_forget_saved(void)
{
    l_tab_coll[COLL_TAB_SAVED] = l_tab_coll[COLL_TAB_IMPORTED] = -1;
    if (l_tab != COLL_TAB_BUILTIN) {
        l_coll = -1;                                /* the tab's first again (puzzles_settle()) */
    }
}

/* (documented in puzzles_select.h) */
void puzzles_follow(int c)
{
    l_coll = c;
    l_tab = coll_tab(c);
}
