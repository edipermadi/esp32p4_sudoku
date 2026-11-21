#!/usr/bin/env python3
"""
Generate the players' avatars, every emoji of Noto Color Emoji (SIL Open Font
License 1.1) as an LVGL 9 image (RGB565A8):

    extras/avatars.bin   the pixels (7 MB), for the flash's "avatars" partition
                         (partitions.csv): flashed with the firmware, or installed by
                         the board from the card (src/avatar_store.c)
    src/avatars.c / .h   the groups, the count and size, and the pixels' CRC-32 (the
                         firmware uses the partition only if it holds these pixels)

avatars.bin: a 16-byte header, then each avatar's 36x36 RGB565 pixels then its
36x36 alpha bytes, one after another:

    0  "AVTR"   4  uint16 version (1)   6  uint16 count   8  uint16 size (px)
    10 uint16 0 12 uint32 CRC-32 (zlib's) of everything after the header

    python3 tools/gen_avatars.py [--font /usr/share/fonts/truetype/noto/NotoColorEmoji.ttf]
                                 [--test /usr/share/unicode/emoji/emoji-test.txt] [--size 36]

Which: emoji-test.txt's fully-qualified emoji, in its order (smileys, people, animals,
food, travel, activities, objects, symbols, flags), without the skin-tone variants
(the same emoji again) and the Component group. Needs Pillow with raqm, for the
sequences (flags, ZWJ ones).

sudoku.db keeps a player's avatar as its number here (1..AVATAR_COUNT), so a newer
emoji-test.txt must only add at the end: --keep FILE.txt lists the order to keep
(one emoji per line, as this script writes to tools/avatars_order.txt).
"""
import argparse
import os
import struct
import zlib

from PIL import Image, ImageDraw, ImageFont, features

HERE = os.path.dirname(os.path.abspath(__file__))


# the only code that reads the tables (avatars.h); the images are avatar_store.c's
ACCESSORS = """
const char *avatar_group_name(int g) { return group_names[g]; }
int avatar_group_first(int g) { return group_first[g]; }
"""

def read_test(path):
    out, group = [], None
    for line in open(path, encoding="utf-8"):
        if line.startswith("# group:"):
            group = line.split(":", 1)[1].strip()
            continue
        if line.startswith("#") or not line.strip():
            continue
        cps, rest = line.split(";", 1)
        if rest.split("#")[0].strip() != "fully-qualified" or group == "Component":
            continue
        cps = [int(x, 16) for x in cps.split()]
        if any(0x1F3FB <= c <= 0x1F3FF for c in cps):        # skin tones
            continue
        name = rest.split("#", 1)[1].strip().split(" ", 2)[2]  # "😀 E1.0 grinning face"
        out.append((group, "".join(chr(c) for c in cps), name))
    return out


def render(font, text, size):
    canvas = Image.new("RGBA", (400, 200), (0, 0, 0, 0))
    ImageDraw.Draw(canvas).text((20, 20), text, font=font, embedded_color=True)
    box = canvas.getbbox()
    if not box:
        return None
    img = canvas.crop(box)
    if img.width > img.height * 1.6:                         # not one glyph: not in the font
        return None
    side = max(img.size)                                     # centred in a square, aspect kept
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(img, ((side - img.width) // 2, (side - img.height) // 2))
    # premultiplied while scaling, so the transparent edge doesn't darken
    return sq.convert("RGBa").resize((size, size), Image.LANCZOS).convert("RGBA")


def rgb565a8(img):
    px = img.tobytes()
    rgb, a = bytearray(), bytearray()
    for p in range(0, len(px), 4):
        r, g, b, al = px[p], px[p + 1], px[p + 2], px[p + 3]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) if al else 0
        rgb += bytes((v & 0xFF, v >> 8))
        a.append(al)
    return bytes(rgb + a)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf")
    ap.add_argument("--test", default="/usr/share/unicode/emoji/emoji-test.txt")
    ap.add_argument("--size", type=int, default=36)
    ap.add_argument("--keep", help="an earlier avatars_order.txt: its emoji keep their numbers")
    args = ap.parse_args()
    if not features.check("raqm"):
        raise SystemExit("Pillow needs raqm (libraqm) to draw flags and ZWJ sequences")
    font = ImageFont.truetype(args.font, 109)                # the size Noto's colour bitmaps come in
    emoji = read_test(args.test)
    if args.keep:
        old = [l.rstrip("\n") for l in open(args.keep, encoding="utf-8") if l.strip()]
        by = {e[1]: e for e in emoji}
        emoji = [by[t] for t in old if t in by] + [e for e in emoji if e[1] not in set(old)]

    size, data, kept, skipped = args.size, bytearray(), [], []
    for group, text, name in emoji:
        img = render(font, text, size)
        if img is None:
            skipped.append(name)
            continue
        kept.append((group, text, name))
        data += rgb565a8(img)
    n, one = len(kept), size * size * 3

    groups = []                                              # (name, first index)
    for i, (g, _, _) in enumerate(kept):
        if not groups or groups[-1][0] != g:
            groups.append((g, i))

    crc = zlib.crc32(data)
    with open(os.path.join(HERE, "..", "extras", "avatars.bin"), "wb") as f:
        f.write(b"AVTR" + struct.pack("<HHHHI", 1, n, size, 0, crc) + data)

    c = ["/* Generated by tools/gen_avatars.py: don't edit. The players' avatars: %d emoji" % n,
         " * of Noto Color Emoji (SIL Open Font License 1.1), %dx%d RGB565A8; their pixels are" % (size, size),
         " * in extras/avatars.bin, for the flash's avatars partition (avatar_store.c). */",
         '#include "avatars.h"', "",
         "static const char *const group_names[AVATAR_GROUPS] = {"]
    c += ['    "%s",' % g for g, _ in groups]
    c += ["};", "static const uint16_t group_first[AVATAR_GROUPS] = {"]
    c += ["    %d,%s/* %s */" % (i, " " * (6 - len(str(i))), kept[i][2]) for _, i in groups]
    c += ["};"] + ACCESSORS.split("\n")
    with open(os.path.join(HERE, "..", "src", "avatars.c"), "w") as f:
        f.write("\n".join(c))

    h = """/* The players' avatars (avatars.c, generated by tools/gen_avatars.py): every emoji of
 * Noto Color Emoji, skin-tone variants left out, in emoji-test.txt's order. A player's
 * avatar is 0 for none, else k: avatar_image(k - 1). sudoku.db keeps k, so a newer
 * set only adds at the end (gen_avatars.py --keep tools/avatars_order.txt). */
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AVATAR_COUNT  %d
#define AVATAR_SIZE   %d                       /* px, square */
#define AVATAR_GROUPS %d                       /* Smileys & Emotion, People & Body, ... */
#define AVATAR_CRC    0x%08Xu                /* CRC-32 of extras/avatars.bin's pixels */

/* avatar_store.c: the pixels are in the flash's "avatars" partition. avatar_image() is a
 * blank (transparent) image until avatars_init() finds them there. */
const lv_image_dsc_t *avatar_image(int i);   /* 0..AVATAR_COUNT-1 */
const char *avatar_group_name(int g);        /* 0..AVATAR_GROUPS-1 */
int avatar_group_first(int g);               /* the group's first avatar, from 0 */

#ifdef __cplusplus
}
#endif
""" % (n, size, len(groups), crc)
    with open(os.path.join(HERE, "..", "src", "avatars.h"), "w") as f:
        f.write(h)
    with open(os.path.join(HERE, "avatars_order.txt"), "w", encoding="utf-8") as f:
        f.write("".join(t + "\n" for _, t, _ in kept))
    print("%d avatars, %dx%d, %.1f MB (extras/avatars.bin, CRC %08X); %d groups; not in the font: %d %s"
          % (n, size, size, len(data) / 1e6, crc, len(groups), len(skipped), skipped[:10]))


if __name__ == "__main__":
    main()
