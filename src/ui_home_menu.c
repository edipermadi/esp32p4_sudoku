/**
 * @file ui_home_menu.c
 * @ingroup ui_home_menu
 * @brief Home's menu layout: a column of full-width buttons while they fit between the header and the
 *        bottom of the screen; otherwise a grid of two per row; and when even that doesn't fit, pages
 *        of the grid side by side, swiped between, with dots under them (was in ui_list.c).
 */
#include "ui_internal.h"

/**
 * @defgroup ui_home_menu Home menu
 * @brief Home's menu buttons laid out in a column, a grid or pages of it.
 * @{
 */

static int h_menu_grid;                   /**< layout signature (home_layout), -1 = not laid out */
static lv_obj_t *h_items[16];             /**< the menu buttons, in order */
static int h_nitems;                      /**< how many */
static int h_npages;                      /**< pages of the menu */

/** @brief The dots under the menu (small circles, the page shown in the accent colour). */
static void home_dots(void)
{
    if (!home_dots_obj()) {
        return;
    }
    lv_obj_set_hidden(home_dots_obj(), h_npages < 2);
    if (h_npages < 2) {
        return;
    }
    int cur = (int)((lv_obj_get_scroll_x(home_menu()) + HOME_GRID_W / 2) / HOME_GRID_W);
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        lv_obj_t *d = lv_obj_get_child(home_dots_obj(), p);
        lv_obj_set_hidden(d, p >= h_npages);
        lv_obj_set_style_bg_color(d, p == cur ? C_ACCENT : C_CARD2, 0);
    }
}

/**
 * @brief The menu scrolled: the dots follow.
 * @param e  the event
 */
void home_scroll_cb(lv_event_t *e)
{
    home_dots();
}

/** @brief Lays the menu out (again only when what's shown changed). */
void home_layout(void)
{
    lv_obj_t *vis[16];
    int n = 0;
    for (int i = 0; i < h_nitems; i++) {
        if (!lv_obj_is_hidden(h_items[i])) {
            vis[n++] = h_items[i];
        }
    }
    const int rows = (HOME_MENU_MAX_H + HOME_GAP) / (HOME_BTN_H + HOME_GAP);   /* that fit under the header */
    bool grid = n > rows;
    int per_page = grid ? rows * 2 : rows;
    int npages = (n + per_page - 1) / per_page;
    if (npages > HOME_PAGES_MAX) {
        npages = HOME_PAGES_MAX;
    }
    int sig = (grid ? 1000 : 0) + n * 10 + npages;      /* (the same layout: nothing to do) */
    if (sig == h_menu_grid) {
        return;
    }
    h_menu_grid = sig;
    h_npages = npages;
    for (int p = 0; p < HOME_PAGES_MAX; p++) {
        lv_obj_set_flex_flow(home_page()[p], grid ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(home_page()[p], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_hidden(home_page()[p], p >= npages);
    }
    for (int i = 0; i < h_nitems; i++) {               /* hidden ones wait on page 1 */
        if (lv_obj_is_hidden(h_items[i])) {
            lv_obj_set_parent(h_items[i], home_page()[0]);
        }
    }
    for (int k = 0; k < n; k++) {
        int p = k / per_page;
        if (p >= npages) {
            p = npages - 1;
        }
        lv_obj_set_parent(vis[k], home_page()[p]);
        lv_obj_move_to_index(vis[k], -1);              /* (in order on its page) */
        lv_obj_set_width(vis[k], grid ? (HOME_GRID_W - HOME_GAP) / 2 : HOME_COL_W);
    }
    lv_obj_set_scrollable(home_menu(), npages > 1);
    lv_obj_update_layout(home_menu());
    if (lv_obj_get_scroll_x(home_menu()) > (npages - 1) * HOME_GRID_W) {
        lv_obj_scroll_to_x(home_menu(), 0, LV_ANIM_OFF);
    }
    home_dots();
}

/* ---- accessors: the other UI files reach this file's state through these */
/** @brief The layout signature. @return it (-1: not laid out) */
int home_menu_grid(void) { return h_menu_grid; }
/** @brief Sets the layout signature (-1: lay out again). @param v the signature */
void home_set_menu_grid(int v) { h_menu_grid = v; }
/** @brief The menu buttons. @return them, in order */
lv_obj_t *const *home_items(void) { return h_items; }
/** @brief Sets a menu button. @param i its place @param v the button */
void home_set_items_at(int i, lv_obj_t *v) { h_items[i] = v; }
/** @brief The menu buttons. @return how many */
int home_nitems(void) { return h_nitems; }
/** @brief Sets how many menu buttons there are. @param v how many */
void home_set_nitems(int v) { h_nitems = v; }

/** @} */
