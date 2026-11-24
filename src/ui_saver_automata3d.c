/**
 * @file ui_saver_automata3d.c
 * @ingroup ui_sudoku
 * @brief The 3D cellular automata screen savers: a rule of saver_automata3d_core.c (Crystal growth, Diamond
 *        growth, Pyroclastic or Builder 1) in a cube of cells that turns slowly in space, each cell a small bevelled block in 3Blue1Brown's colours on
 *        black: live cells deep blue at the bottom through blue, teal and green to yellow at the top,
 *        dying ones from red through pink and purple to deep blue, the far ones dimmer.
 *
 * Each frame the cube is seen from a new angle (orthographic: turning about the vertical, tilted a little
 * up and down), so the whole picture is drawn again: the cells with a dead neighbour face (the others
 * can't be seen) are sorted far to near by a bucket sort on their depth and painted over each other.
 * Only the box the cube covered last frame and this one is cleared and redrawn.
 */
#include "ui_internal.h"
#include "saver_automata3d_core.h"
#include <math.h>

#define A3D_FRAME_MS    80                      /**< a frame each, ms */
#define A3D_SCALE       12.0f                   /**< a cell's spacing on screen, px */
#define A3D_BLOCK       10                      /**< a block's size, px */
#define A3D_TURN        0.010f                  /**< the turn about the vertical a frame, radians */
#define A3D_TILT        0.50f                   /**< looking down at the cube this much, radians ... */
#define A3D_NOD         0.18f                   /**< ... give or take this much, slowly */
#define A3D_SHADES      16                      /**< brightness levels by depth */
#define A3D_FAR         0.35f                   /**< the farthest cells' brightness, 0..1 */
#define A3D_ALIVE_TONES 12                      /**< live cells' colours, middle to edge */
#define A3D_TONES       (A3D_ALIVE_TONES + A3_STATES_MAX)   /**< then a colour each dying state */
#define A3D_BUCKETS     256                     /**< depth buckets for the sort */
#define A3D_PURPLE      0x9A72AC                /**< 3Blue1Brown's purple (only here) */
#define A3D_MID         ((A3_N - 1) * 0.5f)     /**< the cube's middle, in cells */
#define A3D_RADIUS      (A3D_MID * 1.7320508f)  /**< its corners' distance from the middle */

/** @brief A cell to draw. */
typedef struct {
    int16_t x, y;                         /**< its block's top left, px */
    uint8_t tone;                         /**< its colour (0..A3D_TONES-1) */
    uint8_t depth;                        /**< its depth bucket: 0 far .. A3D_BUCKETS-1 near */
} a3d_cell_t;

static uint16_t *a3d_buf;                 /**< the picture (saver_pic_start()) */
static a3d_cell_t *a3d_cells;             /**< this frame's cells to draw */
static uint16_t *a3d_order;               /**< them far to near */
static uint8_t *a3d_tone;                 /**< each cell's live colour by where it is (A3_CELLS) */
static uint16_t a3d_col[A3D_TONES][A3D_SHADES][3];  /**< each colour and depth: face, lit edge, shaded edge */
static lv_timer_t *a3d_timer;             /**< a frame each A3D_FRAME_MS */
static uint32_t a3d_frame;                /**< frames since it started */
static int a3d_due;                       /**< ms of frames since the last generation */
static int a3d_box[4];                    /**< the box drawn last frame: x0, y0, x1, y1 (x1 < 0: none) */

/**
 * @brief A colour along a list of colours.
 * @param[in] stops  the colours, 0xRRGGBB
 * @param[in] n  how many
 * @param[in] u  where, 0.0 (the first) .. 1.0 (the last)
 * @return the colour, 0xRRGGBB
 */
static uint32_t along(const uint32_t *stops, int n, float u)
{
    float v = LV_CLAMP(0.0f, u, 1.0f) * (n - 1);
    int k = LV_MIN((int)v, n - 2);
    float f = v - k;
    uint32_t a = stops[k], b = stops[k + 1], c = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int p = (a >> sh) & 0xFF, q = (b >> sh) & 0xFF;
        c |= (uint32_t)(p + (q - p) * f) << sh;
    }
    return c;
}

/** @brief The colours: each tone at each depth, its face and its edges. */
static void make_colors(void)
{
    static const uint32_t ALIVE[] = {SV_BLUE_D, SV_BLUE, SV_TEAL, SV_GREEN, SV_YELLOW};
    static const uint32_t DYING[] = {SV_RED, SV_PINK, A3D_PURPLE, SV_BLUE_E};
    int dying = LV_MAX(1, a3_states() - 2);
    for (int t = 0; t < A3D_TONES; t++) {
        uint32_t c = t < A3D_ALIVE_TONES ? along(ALIVE, 5, (float)t / (A3D_ALIVE_TONES - 1))
                     : along(DYING, 4, (float)(t - A3D_ALIVE_TONES) / dying);
        for (int s = 0; s < A3D_SHADES; s++) {
            int lum = (int)(255 * (A3D_FAR + (1.0f - A3D_FAR) * s / (A3D_SHADES - 1)));
            uint32_t face = 0;
            for (int sh = 0; sh <= 16; sh += 8) {
                face |= (uint32_t)(((c >> sh) & 0xFF) * lum / 255) << sh;
            }
            a3d_col[t][s][0] = lv_color_to_u16(lv_color_hex(face));
            a3d_col[t][s][1] = saver_pic_mix(face, 0xFFFFFF, 70);   /* lit: top and left */
            a3d_col[t][s][2] = saver_pic_mix(face, 0x000000, 120);  /* shaded: bottom and right */
        }
    }
}

/** @brief Each cell's live colour: by its height, deep blue at the bottom to yellow at the top. */
static void make_tones(void)
{
    for (int z = 0, i = 0; z < A3_N; z++) {
        for (int y = 0; y < A3_N; y++) {
            for (int x = 0; x < A3_N; x++, i++) {
                a3d_tone[i] = (uint8_t)(y * A3D_ALIVE_TONES / A3_N);
            }
        }
    }
}

/**
 * @brief Paints a block: its face, a lit top and left edge, a shaded bottom and right one (clipped).
 * @param[in] x  left, px
 * @param[in] y  top, px
 * @param[in] c  its colours: face, lit, shaded
 */
static void block(int x, int y, const uint16_t *c)
{
    for (int r = 0; r < A3D_BLOCK; r++) {
        int yy = y + r;
        if ((unsigned)yy >= SCR_W) {
            continue;
        }
        uint16_t *p = a3d_buf + yy * SCR_W;
        for (int k = 0; k < A3D_BLOCK; k++) {
            int xx = x + k;
            if ((unsigned)xx < SCR_W) {
                p[xx] = r == 0 || k == 0 ? c[1] : r == A3D_BLOCK - 1 || k == A3D_BLOCK - 1 ? c[2] : c[0];
            }
        }
    }
}

/**
 * @brief The cube from this frame's angle: the cells that can be seen, far to near, painted; the box
 *        they and last frame's covered cleared before and redrawn after.
 */
static void a3d_paint(void)
{
    float yaw = a3d_frame * A3D_TURN;
    float tilt = A3D_TILT + A3D_NOD * sinf(a3d_frame * (A3D_TURN * 0.37f));
    float cy = cosf(yaw), sy = sinf(yaw), ct = cosf(tilt), st = sinf(tilt);
    float px[3][A3_N], py[3][A3_N], pd[3][A3_N];    /* each axis's part in screen x, y and depth */
    for (int i = 0; i < A3_N; i++) {
        float c = i - A3D_MID;
        px[0][i] = A3D_SCALE * c * cy;              /* x */
        py[0][i] = A3D_SCALE * -c * sy * st;
        pd[0][i] = -c * sy * ct;
        px[1][i] = 0;                               /* y (up) */
        py[1][i] = -A3D_SCALE * c * ct;
        pd[1][i] = c * st;
        px[2][i] = A3D_SCALE * c * sy;              /* z */
        py[2][i] = A3D_SCALE * c * cy * st;
        pd[2][i] = c * cy * ct;
    }
    const float half = SCR_W / 2 - A3D_BLOCK / 2, dscale = (A3D_BUCKETS - 1) / (2 * A3D_RADIUS);
    const uint8_t *cell = a3_cells();
    const int plane = A3_N * A3_N, dying_tone = A3D_ALIVE_TONES - 2;
    static uint16_t count[A3D_BUCKETS];
    memset(count, 0, sizeof(count));
    int n = 0, box[4] = {SCR_W, SCR_W, -1, -1};
    for (int z = 0, i = 0; z < A3_N; z++) {
        for (int y = 0; y < A3_N; y++) {
            for (int x = 0; x < A3_N; x++, i++) {
                uint8_t s = cell[i];
                if (!s || (x > 0 && x < A3_N - 1 && y > 0 && y < A3_N - 1 && z > 0 && z < A3_N - 1 &&
                           cell[i - 1] && cell[i + 1] && cell[i - A3_N] && cell[i + A3_N] &&
                           cell[i - plane] && cell[i + plane])) {
                    continue;                       /* dead, or hidden on every side */
                }
                a3d_cell_t *c = &a3d_cells[n++];
                c->x = (int16_t)(half + px[0][x] + px[2][z]);
                c->y = (int16_t)(half + py[0][x] + py[1][y] + py[2][z]);
                c->tone = s == 1 ? a3d_tone[i] : (uint8_t)(dying_tone + s);
                c->depth = (uint8_t)LV_CLAMP(0, (int)((pd[0][x] + pd[1][y] + pd[2][z] + A3D_RADIUS) * dscale),
                                             A3D_BUCKETS - 1);
                count[c->depth]++;
                box[0] = LV_MIN(box[0], c->x);
                box[1] = LV_MIN(box[1], c->y);
                box[2] = LV_MAX(box[2], c->x + A3D_BLOCK - 1);
                box[3] = LV_MAX(box[3], c->y + A3D_BLOCK - 1);
            }
        }
    }
    for (int b = 0, at = 0; b < A3D_BUCKETS; b++) { /* (each bucket's first place) */
        int k = count[b];
        count[b] = (uint16_t)at;
        at += k;
    }
    for (int k = 0; k < n; k++) {
        a3d_order[count[a3d_cells[k].depth]++] = (uint16_t)k;
    }
    if (a3d_box[2] >= 0) {                          /* last frame's box cleared */
        for (int y = a3d_box[1]; y <= a3d_box[3]; y++) {
            memset(a3d_buf + y * SCR_W + a3d_box[0], 0, (size_t)(a3d_box[2] - a3d_box[0] + 1) * 2);
        }
    }
    for (int k = 0; k < n; k++) {
        const a3d_cell_t *c = &a3d_cells[a3d_order[k]];
        block(c->x, c->y, a3d_col[c->tone][c->depth * A3D_SHADES / A3D_BUCKETS]);
    }
    if (box[2] >= 0) {
        box[0] = LV_MAX(box[0], 0);
        box[1] = LV_MAX(box[1], 0);
        box[2] = LV_MIN(box[2], SCR_W - 1);
        box[3] = LV_MIN(box[3], SCR_W - 1);
    }
    int all[4] = {box[0], box[1], box[2], box[3]};  /* this frame's box and last frame's */
    if (a3d_box[2] >= 0) {
        all[0] = LV_MIN(all[0], a3d_box[0]);
        all[1] = LV_MIN(all[1], a3d_box[1]);
        all[2] = LV_MAX(all[2], a3d_box[2]);
        all[3] = LV_MAX(all[3], a3d_box[3]);
    }
    memcpy(a3d_box, box, sizeof(box));
    if (all[2] >= 0) {
        saver_pic_flush(all[1], all[3]);
        saver_pic_redraw(all[0], all[1], all[2], all[3]);
    }
}

/**
 * @brief A frame: the cube a little further round, and a generation each a3_step_ms().
 * @param[in] t  the timer
 */
static void a3d_tick(lv_timer_t *t)
{
    a3d_frame++;
    a3d_due += A3D_FRAME_MS;
    if (a3d_due >= a3_step_ms()) {
        a3d_due = 0;
        a3_step();
    }
    a3d_paint();
}

/* (documented in ui_internal.h) */
bool saver_automata3d_start(lv_obj_t *parent, int rule)
{
    if (!a3d_cells) {
        a3d_cells = malloc(A3_CELLS * sizeof(*a3d_cells));
        a3d_order = malloc(A3_CELLS * sizeof(*a3d_order));
        a3d_tone = malloc(A3_CELLS);
    }
    if (!a3d_cells || !a3d_order || !a3d_tone || !a3_start(esp_random(), rule) ||
            !(a3d_buf = saver_pic_start(parent))) {
        saver_automata3d_stop();
        return false;
    }
    make_tones();
    make_colors();
    a3d_frame = 0;
    a3d_due = 0;
    a3d_box[2] = -1;
    a3d_paint();
    a3d_timer = lv_timer_create(a3d_tick, A3D_FRAME_MS, NULL);
    return true;
}

/* (documented in ui_internal.h) */
void saver_automata3d_stop(void)
{
    if (a3d_timer) {
        lv_timer_delete(a3d_timer);
        a3d_timer = NULL;
    }
    if (a3d_buf) {
        saver_pic_stop();                           /* (its image goes before its memory) */
        a3d_buf = NULL;
    }
    free(a3d_cells);
    free(a3d_order);
    free(a3d_tone);
    a3d_cells = NULL;
    a3d_order = NULL;
    a3d_tone = NULL;
    a3_free();
}
