/**
 * @file ui_common.c
 * @brief What every screen shares: the palette, layout, widgets and helpers, context menus (the puzzle cache and collections: puzzles_lookup.c).
 */
#include "ui_internal.h"

/* this file's own functions, in any order */
static void ctx_backdrop_cb(lv_event_t *e);
static void ctx_item_cb(lv_event_t *e);

/* ------------------------------------------------------------- palette */
/* palette_t and the C_* colour macros are in ui_palette.h (shared with ui_wifi.c) */
/** @brief The dark scheme. */
static const palette_t PAL_DARK = {
    .bg = 0x101218, .card = 0x1B1E27, .card2 = 0x2A2F3D, .text = 0xEDEFF4, .muted = 0x8B92A5,
    .accent = 0x4F8CFF, .green = 0x3DD68C, .orange = 0xFFA94D, .red = 0xFF6B6B,
    .grid_thick = 0x8B92A5, .grid_thin = 0x3A3F4E,
    .cell = 0x1B1E27, .cell_given = 0x22262F, .cell_peer = 0x272D3D, .cell_same = 0x2F3F63,
    .cell_sel = 0x3A5BA0, .cell_bad = 0x4A2329, .cell_solved = 0x173A2A, .digit_user = 0x7FB0FF,
    .list_solved = 0x1E7A4F, .list_active = 0xA0621C, .notice = 0x5A2A1A, .scrim_opa = LV_OPA_60,
};

/** @brief The light scheme. */
static const palette_t PAL_LIGHT = {
    .bg = 0xF2F3F7, .card = 0xFFFFFF, .card2 = 0xE2E5EC, .text = 0x1B1E27, .muted = 0x6B7285,
    .accent = 0x2F6FE0, .green = 0x1E9E62, .orange = 0xD9822B, .red = 0xD93A3A,
    .grid_thick = 0x3A3F4E, .grid_thin = 0xC5CAD6,
    .cell = 0xFFFFFF, .cell_given = 0xEEF0F4, .cell_peer = 0xE3E9F5, .cell_same = 0xC9D8F5,
    .cell_sel = 0x9DBBF2, .cell_bad = 0xF8D7DA, .cell_solved = 0xD4F1E2, .digit_user = 0x2F6FE0,
    .list_solved = 0x2FA36B, .list_active = 0xD98A2B, .notice = 0xFFE8D6, .scrim_opa = LV_OPA_40,
};

static const palette_t *ui_pal = &PAL_DARK;   /**< the scheme in use (PAL_DARK or PAL_LIGHT) */
static palette_t s_pal;                   /**< the active scheme with the chosen accent (palette_build) */

/** @brief accent colours (Settings): dark-mode and light-mode shades; 0 = the schemes' own blue */
static const uint32_t ACCENTS[ACCENT_COUNT][2] = {
    {0x4F8CFF, 0x2F6FE0}, {0x2DD4BF, 0x0D9488}, {0x4ADE80, 0x16A34A}, {0xA78BFA, 0x7C3AED}, {0xF472B6, 0xDB2777},
};

/* (documented in ui_internal.h) */
int effective_brightness(void)
{
    int b = store_brightness();
    if (is_ui_night() && store_night_brightness() < b) {
        b = store_night_brightness();
    }
    return b;
}

/* (documented in ui_internal.h) */
uint32_t mix_hex(uint32_t a, uint32_t b, int pct)   /* pct % of a over b */
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        out |= (uint32_t)((ca * pct + cb * (100 - pct)) / 100) << sh;
    }
    return out;
}

/* (documented in ui_internal.h) */
void palette_build(bool dark, int accent)
{
    s_pal = dark ? PAL_DARK : PAL_LIGHT;
    if (accent > 0 && accent < ACCENT_COUNT) {
        uint32_t a = ACCENTS[accent][dark ? 0 : 1];
        s_pal.accent = a;
        s_pal.cell_sel = mix_hex(a, s_pal.cell, dark ? 55 : 45);
        s_pal.cell_same = mix_hex(a, s_pal.cell, 25);
        s_pal.cell_peer = mix_hex(a, s_pal.cell, dark ? 8 : 10);
        s_pal.digit_user = dark ? mix_hex(a, 0xFFFFFF, 75) : a;
    }
    if (is_store_cvd()) {
        /* Colour-blind friendly (Settings > Display): the Okabe-Ito colours. Red and green
         * look alike to most colour-blind people; vermillion (mistakes), bluish green (solved,
         * complete) and yellow-orange (in progress) stay apart, and apart from the blues,
         * in lightness as well as hue. */
        if (dark) {
            s_pal.red = 0xFF8A3D;
            s_pal.green = 0x3CC9A7;
            s_pal.orange = 0xF2B638;
            s_pal.cell_bad = 0x55300F;
            s_pal.cell_solved = 0x113D36;
            s_pal.list_solved = 0x0E7D66;
            s_pal.list_active = 0xA87C10;
        } else {
            s_pal.red = 0xD55E00;
            s_pal.green = 0x00876A;
            s_pal.orange = 0xC98A00;
            s_pal.cell_bad = 0xFBDCC4;
            s_pal.cell_solved = 0xCDEFE6;
            s_pal.list_solved = 0x009E73;
            s_pal.list_active = 0xE69F00;
        }
    }
    ui_pal = &s_pal;
}

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

/* ------------------------------------------------------------- widgets */

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

/* ============================================================= helpers */

/* (documented in ui_internal.h) */
void fmt_time(char *buf, size_t n, uint32_t s)
{
    if (s >= 3600) {
        snprintf(buf, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    } else {
        snprintf(buf, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    }
}

/* (documented in ui_internal.h) */
const char *ordinal(int n)
{
    int t = n % 100;
    if (t >= 11 && t <= 13) {
        return "th";
    }
    return n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
}

/* (documented in ui_internal.h) */
lv_obj_t *make_tab_strip(lv_obj_t *parent, const char *const *map, int checked)
{
    lv_obj_t *tabs = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(tabs, map);
    lv_buttonmatrix_set_button_ctrl_all(tabs, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(tabs, true);
    lv_buttonmatrix_set_button_ctrl(tabs, (uint32_t)checked, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(tabs, LV_PCT(100), 44);
    lv_obj_set_style_pad_all(tabs, 0, 0);
    lv_obj_set_style_pad_column(tabs, 8, 0);
    lv_obj_set_style_margin_top(tabs, 12, 0);
    lv_obj_set_style_border_width(tabs, 0, 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(tabs, &lv_font_montserrat_18, 0);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_ITEMS);   /* a tab: its name, underlined when shown */
    lv_obj_set_style_shadow_width(tabs, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(tabs, C_MUTED, LV_PART_ITEMS);
    lv_obj_set_style_border_side(tabs, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_width(tabs, 2, LV_PART_ITEMS);
    lv_obj_set_style_border_color(tabs, C_GRID_THIN, LV_PART_ITEMS);
    lv_obj_set_style_radius(tabs, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(tabs, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tabs, 4, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tabs, C_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    return tabs;
}

/* (documented in ui_internal.h) */
lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                   const lv_font_t *font, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, C_CARD, 0);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (text) {
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, C_TEXT, 0);
        lv_obj_center(l);
    }
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    }
    return b;
}

/* (documented in ui_internal.h) */
lv_obj_t *make_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, C_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s, false);
    return s;
}

/* (documented in ui_internal.h) */
lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

/* ------------------------------------------------------------- icon buttons, context menus */
/* (documented in ui_internal.h) */
void icon_btn_set(lv_obj_t *b, bool on, lv_color_t off_c)
{
    lv_obj_set_state(b, LV_STATE_CHECKED, on);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), on ? lv_color_white() : off_c, 0);
}
static lv_obj_t *ctx_menu;              /**< the open menu's backdrop (NULL: none) */
static lv_obj_t *ctx_btn;               /**< its button */

/* (documented in ui_internal.h) */
void ctx_menu_close(void)
{
    if (!ctx_menu) {
        return;
    }
    lv_obj_delete_async(ctx_menu);
    ctx_menu = NULL;
    if (ctx_btn) {
        icon_btn_set(ctx_btn, false, C_TEXT);
    }
    ctx_btn = NULL;
}

/**
 * @brief A tap beside the menu closes it.
 * @param[in] e  the event
 */
static void ctx_backdrop_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) {
        ctx_menu_close();
    }
}

/**
 * @brief A menu item tapped: the menu closes and the item's action runs.
 * @param[in] e  the event (user data: the item's action)
 */
static void ctx_item_cb(lv_event_t *e)
{
    void (*cb)(lv_event_t *) = (void (*)(lv_event_t *))lv_event_get_user_data(e);
    ctx_menu_close();
    cb(NULL);
}

/* (documented in ui_internal.h) */
lv_obj_t *ctx_card_open(lv_obj_t *btn, int w)
{
    ctx_menu_close();
    ctx_menu = lv_obj_create(lv_layer_top());       /* the backdrop: invisible, catches taps outside */
    lv_obj_remove_style_all(ctx_menu);
    lv_obj_set_size(ctx_menu, SCR_W, SCR_W);
    lv_obj_set_clickable(ctx_menu, true);
    lv_obj_add_event_cb(ctx_menu, ctx_backdrop_cb, LV_EVENT_CLICKED, NULL);
    ctx_btn = btn;
    icon_btn_set(btn, true, C_TEXT);

    lv_area_t a;
    lv_obj_get_coords(btn, &a);
    lv_obj_t *card = lv_obj_create(ctx_menu);
    lv_obj_set_pos(card, LV_MAX(8, a.x2 + 1 - w), a.y2 + 7);   /* (a button on the left: from the edge) */
    lv_obj_set_size(card, w, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_border_color(card, C_CARD2, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_shadow_offset_y(card, 4, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(card, false);
    return card;
}

/* (documented in ui_internal.h) */
void ctx_menu_toggle(lv_obj_t *btn, const ctx_item_t *items, int n)
{
    bool was = ctx_menu && ctx_btn == btn;
    ctx_menu_close();
    if (was) {
        return;
    }
    lv_obj_t *card = ctx_card_open(btn, 260);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = make_btn(card, 0, 0, LV_PCT(100), 56, NULL, NULL, items[i].disabled ? NULL : ctx_item_cb,
                               (void *)items[i].cb);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_clickable(b, !items[i].disabled);
        if (items[i].disabled) {
            lv_obj_set_style_opa(b, LV_OPA_40, 0);
        }
        lv_obj_t *ic = make_label(b, &font_icons_20, items[i].danger ? C_RED : C_ACCENT);   /* own column; LVGL's
                                                                     symbols and a few FontAwesome ones (Montserrat 20 fallback) */
        lv_label_set_text(ic, items[i].icon);
        lv_obj_set_width(ic, 32);
        lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t *l = make_label(b, &lv_font_montserrat_18, items[i].danger ? C_RED : C_TEXT);
        lv_label_set_text(l, items[i].text);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 8 + 32 + 12, 0);
    }
}

/* ------------------------------------------------------------- dialog text */
/* (documented in ui_internal.h) */
lv_obj_t *make_wrap_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    if (font) {
        lv_obj_set_style_text_font(l, font, 0);
    }
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

/* ---- accessors: the other UI files reach this file's state through these */
const palette_t *ui_palette_dark(void) { return &PAL_DARK; }
const palette_t *ui_palette(void) { return ui_pal; }

/* ------------------------------------------------------------- dialogs */
/* (documented in ui_internal.h) */
void mbox_close_cb(lv_event_t *e)
{
    lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
}
/* (documented in ui_internal.h) */
lv_obj_t *make_msgbox(const char *title, const char *text)
{
    lv_obj_t *m = lv_msgbox_create(NULL);
    lv_msgbox_add_title(m, title);
    /* left-aligned, wrapped; NULL: the caller fills the content */
    if (text) {
        make_wrap_text(lv_msgbox_get_content(m), NULL, C_TEXT, text);
    }
    lv_obj_set_width(m, 520);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_20, 0);
    return m;
}
/* (documented in ui_internal.h) */
void msgbox_button(lv_obj_t *m, const char *text, lv_event_cb_t cb, bool primary)
{
    lv_obj_t *b = lv_msgbox_add_footer_button(m, text);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, 52);
    if (!primary) {                                 /* Cancel, Close ...: grey, not the dialog's own white */
        lv_obj_set_style_bg_color(b, C_CARD2, 0);
        lv_obj_set_style_text_color(b, C_TEXT, 0);
    }
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, m);
    /* LVGL makes the footer LV_DPI_DEF / 3 (~43 px) tall, less than these
     * 52 px buttons, which then spill over the text and past the box edge */
    lv_obj_t *foot = lv_msgbox_get_footer(m);
    lv_obj_set_height(foot, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(foot, 10, 0);
    lv_obj_set_style_pad_bottom(foot, 18, 0);                  /* room under the buttons */
}
