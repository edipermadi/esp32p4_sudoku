/**
 * @file ui_player_badge.c
 * @brief A player shown small: their colour, their dot, their avatar on a circle of that colour (Players, Home, leaderboards, Gameplays, Replay).
 */
#include "ui_internal.h"

/* (documented in ui_internal.h) */
lv_color_t player_color(int k)
{
    static const uint32_t C[STATS_PLAYER_COLORS] = {0x3E7BFA, 0xE5484D, 0x30A46C, 0xF5A524,
                                                    0x9E6CF0, 0xE93D82, 0x12A594, 0x8B7355
                                                   };
    return lv_color_hex(C[(k % STATS_PLAYER_COLORS + STATS_PLAYER_COLORS) % STATS_PLAYER_COLORS]);
}

/* (documented in ui_internal.h) */
lv_obj_t *player_dot(lv_obj_t *parent, int color, int size)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, size, size);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, player_color(color), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_clickable(d, false);
    return d;
}

/* (documented in ui_internal.h) */
const stats_player_t *player_find(int32_t id, const char *name)
{
    for (int i = 0; i < players_count(); i++) {
        if (id ? players_all()[i].id == id : (name && !strcasecmp(players_all()[i].name, name))) {
            return &players_all()[i];
        }
    }
    return NULL;
}

/* (documented in ui_internal.h) */
lv_obj_t *player_badge(lv_obj_t *parent, const stats_player_t *pl, int color, int size, int dot)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    if (color < 0) {
        color = pl ? pl->color : 0;
    }
    if (pl && pl->avatar && pl->avatar <= AVATAR_COUNT) {
        lv_obj_t *im = lv_image_create(box);
        lv_image_set_src(im, avatar_image(pl->avatar - 1));
        if (size < AVATAR_SIZE) {
            lv_image_set_scale(im, (uint32_t)(256 * size / AVATAR_SIZE));
        }
        lv_obj_center(im);
    } else {
        lv_obj_center(player_dot(box, color, dot));
    }
    return box;
}
