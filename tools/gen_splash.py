#!/usr/bin/env python3
"""
Generate the start-up splash screen from extras/splashscreen.png (720x720):

    src/splash_data.c / .h   the picture as a JPEG (quality 90, no chroma subsampling: sharp
                             text), with splash_jpeg() to reach it; and its loading bar: where
                             it is, and the watercolour fill as RGB565 pixels (splash_bar())

The picture's "Loading..." bar is painted part-filled. The script erases that fill (its pixels
take the cream of the bar's empty part, row by row) and keeps the fill's texture apart, its
middle mirrored to the bar's whole width; the board shows as much of it, rounded at the ends, as
start-up has got through (src/ui_splash.c). The bar's place (BAR below) is this picture's: a new
picture with the bar elsewhere needs it changed (--bar).

The board decodes it with the ESP32-P4's hardware JPEG decoder (src/ui_splash.c) and shows it
while the card and sudoku.db are read and the screens are built. A JPEG keeps the firmware
small: about 160 KB, where the raw RGB565 pixels would be about 1 MB. Its YCbCr is written in the
"limited range" the hardware decoder expects (see below), so the file looks a little flat in a PC's
image viewer; on the board the colours are right.

    python3 tools/gen_splash.py [--png extras/splashscreen.png] [--quality 90] [--bar X0,Y0,X1,Y1]

Needs Pillow.
"""
import argparse
import io
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 720                                  # the screen: 720x720
BAR = (154, 657, 564, 683)                  # the loading bar's inside: its first and last column and row
FILL_END = 446                              # the painted fill ends before this column
CORE = (175, 425)                           # the columns of the fill's texture (its middle, no round ends)


def is_blueish(p):
    """the bar's blue (fill or border), not the cream or white around it"""
    return p[2] - p[0] > 30 and p[0] < 235


def erase_fill(im, bar):
    """the painted fill turned into the empty bar: each row, from inside the left border up to
    FILL_END, the blue (and the fill's white highlight) takes the empty part's cream"""
    x0, y0, x1, y1 = bar
    orig = im.copy()
    op, px = orig.load(), im.load()
    for y in range(y0, y1 + 1):
        x = x0 - 8
        while x < x0 + 12 and not is_blueish(px[x, y]):    # up to the left border
            x += 1
        while x < x0 + 12 and is_blueish(px[x, y]):        # through it
            x += 1
        for xx in range(x, FILL_END):
            p = px[xx, y]
            if is_blueish(p) or (xx > FILL_END - 16 and min(p) > 245):
                px[xx, y] = op[FILL_END + 4 + (xx - x) % 100, y]
    return orig


def fill_texture(orig, bar):
    """the fill's middle columns, mirrored every other time, to the bar's inside width"""
    x0, y0, x1, y1 = bar
    core = orig.crop((CORE[0], y0, CORE[1], y1 + 1))
    tex = Image.new("RGB", (x1 - x0 + 1, y1 - y0 + 1))
    x, flip = 0, False
    while x < tex.size[0]:
        tex.paste(core.transpose(Image.FLIP_LEFT_RIGHT) if flip else core, (x, 0))
        x += core.size[0]
        flip = not flip
    return tex


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--png", default=os.path.join(ROOT, "extras", "splashscreen.png"))
    ap.add_argument("--quality", type=int, default=90)
    ap.add_argument("--bar", default=",".join(map(str, BAR)), help="the loading bar's inside: x0,y0,x1,y1")
    args = ap.parse_args()
    bar = tuple(int(v) for v in args.bar.split(","))

    im = Image.open(args.png).convert("RGB")
    if im.size != (SIZE, SIZE):
        raise SystemExit("%s is %dx%d: the screen is %dx%d" % (args.png, im.size[0], im.size[1], SIZE, SIZE))
    tex = fill_texture(erase_fill(im, bar), bar)
    # The ESP32-P4's decoder converts YCbCr to RGB as video does, "limited range" (Y 16-235, Cb and Cr
    # 16-240), and a normal JPEG is full range (0-255): light colours came out white and the cream
    # background lost. So the YCbCr goes in already squeezed into that range; the decoder's stretch
    # gives the colours back.
    y, cb, cr = im.convert("YCbCr").split()
    y = y.point(lambda v: round(16 + v * 219 / 255))
    cb = cb.point(lambda v: round(128 + (v - 128) * 224 / 255))
    cr = cr.point(lambda v: round(128 + (v - 128) * 224 / 255))
    out = io.BytesIO()
    Image.merge("YCbCr", (y, cb, cr)).save(out, "JPEG", quality=args.quality, subsampling=0)   # (4:4:4)
    data = out.getvalue()

    rows = []
    for i in range(0, len(data), 24):
        rows.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 24]) + ",")
    px = []                                 # the texture as RGB565, LVGL's order (little-endian)
    for y in range(tex.size[1]):
        for x in range(tex.size[0]):
            r, g, b = tex.getpixel((x, y))
            px.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    trows = []
    for i in range(0, len(px), 16):
        trows.append("    " + ", ".join("0x%04x" % v for v in px[i:i + 16]) + ",")
    c = """/*
 * The start-up splash screen as a JPEG (720x720, %d bytes), generated by tools/gen_splash.py
 * from extras/splashscreen.png: edit the picture and run the script, not this file.
 */
#include "splash_data.h"

static const uint8_t SPLASH_JPEG[] = {
%s
};

/* the loading bar's fill, SPLASH_BAR_W x SPLASH_BAR_H, RGB565 */
static const uint16_t SPLASH_BAR[] = {
%s
};

const uint8_t *splash_jpeg(size_t *len)
{
    *len = sizeof(SPLASH_JPEG);
    return SPLASH_JPEG;
}

const uint16_t *splash_bar(void)
{
    return SPLASH_BAR;
}
""" % (len(data), "\n".join(rows), "\n".join(trows))
    h = """/**
 * @file splash_data.h
 * @brief The start-up splash screen as a JPEG (splash_data.c, generated by tools/gen_splash.py from
 *        extras/splashscreen.png); src/ui_splash.c decodes and shows it.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPLASH_SIZE  %d                         /**< px, square: the whole screen */
#define SPLASH_BAR_X %d                         /**< the loading bar's inside: its left column */
#define SPLASH_BAR_Y %d                         /**< ... its top row */
#define SPLASH_BAR_W %d                         /**< ... its width, px */
#define SPLASH_BAR_H %d                          /**< ... its height, px */

/**
 * @brief The splash screen's JPEG.
 * @param len  gets its length, bytes
 * @return its bytes (in flash)
 */
const uint8_t *splash_jpeg(size_t *len);
/**
 * @brief The loading bar's fill: SPLASH_BAR_W x SPLASH_BAR_H RGB565 pixels, row by row (the picture's
 * bar is empty; this is shown over it, as far as start-up has got).
 * @return its pixels (in flash)
 */
const uint16_t *splash_bar(void);

#ifdef __cplusplus
}
#endif
""" % (SIZE, bar[0], bar[1], bar[2] - bar[0] + 1, bar[3] - bar[1] + 1)
    open(os.path.join(ROOT, "src", "splash_data.c"), "w").write(c)
    open(os.path.join(ROOT, "src", "splash_data.h"), "w").write(h)
    print("splash: %s -> src/splash_data.c (%d bytes of JPEG, quality %d; the bar's fill %dx%d)" % (
        os.path.relpath(args.png, ROOT), len(data), args.quality, tex.size[0], tex.size[1]))


if __name__ == "__main__":
    main()
