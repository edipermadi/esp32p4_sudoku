/**
 * @file ui_ctx_menu.h
 * @brief The ⋮ context menus (ui_ctx_menu.c): a card of icon rows under its button. In ui_internal.h too; files
 *        that keep to their own headers (the file manager) include just this.
 */
#pragma once
#include <stdbool.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A context menu: a card of icon rows under its button (right edges lined up), on the
 * top layer; a tap anywhere else, or on the button again, closes it. An item's cb gets
 * a NULL event. */
typedef struct {
    const char *icon;                   /**< its icon */
    const char *text;                   /**< its text */
    void (*cb)(lv_event_t *e);          /**< a tap (the event is NULL) */
    bool disabled;                      /**< shown dimmed, not tappable */
    bool danger;                        /**< red (Delete) */
} ctx_item_t;

/**
 * @brief Closes the open context menu.
 */
void ctx_menu_close(void);
/**
 * @brief the menu's card, w wide, under btn (any open menu closes first); its content is the caller's
 * @param[in] btn  the menu's button
 * @param[in] w  px
 * @return the card
 */
lv_obj_t *ctx_card_open(lv_obj_t *btn, int w);
/**
 * @brief opens btn's menu (or closes it, when it is open)
 * @param[in] btn  the menu's button
 * @param[in] items  the items
 * @param[in] n  how many
 */
void ctx_menu_toggle(lv_obj_t *btn, const ctx_item_t *items, int n);

#ifdef __cplusplus
}
#endif
