/**
 * @file ui_palette.c
 * @brief The colours: the dark and light schemes, the accent colours, colour-blind friendly colours, and the
 *        brightness in effect (night mode).
 */
#include "ui_internal.h"

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

/* ---- accessors: the other UI files reach this file's state through these */
const palette_t *ui_palette_dark(void) { return &PAL_DARK; }
const palette_t *ui_palette(void) { return ui_pal; }
