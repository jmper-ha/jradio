#!/usr/bin/env python3
"""Draw the home screen icons and emit them as LVGL A8 bitmaps.

Why bitmaps and not a font: LVGL's built-in symbols have no antenna, no flash
drive and no DLNA mark, so three feed items ended up sharing one music note.
Three of the eight icons here are drawn from scratch (the flash drive, the DLNA
disc, the Yandex Music spark) and no icon font contains them either. A8 keeps
one bitmap per size and lets the carousel recolour it per slot, which is how
the row dims towards its edges.

Every icon is laid out on the Material Symbols 24x24 grid so the whole row
shares one metric - that is what removes the per-glyph vertical fudge the old
label-based row needed.

Usage:
    pip install --user pillow           # if PIL is missing
    curl -L -o /tmp/ms.ttf https://raw.githubusercontent.com/google/material-design-icons/master/variablefont/MaterialSymbolsRounded%5BFILL%2CGRAD%2Copsz%2Cwght%5D.ttf
    python3 tools/gen_feed_icons.py --font /tmp/ms.ttf

The five bought-in glyphs come from that variable font at its default instance
(weight 400, FILL 0). Regenerate whenever an icon or a size changes.
"""

import argparse
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

# Outer neighbours, inner neighbours, selected tile. Kept in this order because
# ui_feed_icons.h indexes the table with the same enum.
SIZES = (24, 32, 48)

# Icons that are not carousel slots and need only their own size.
EXTRA_SIZES = {"volume": (16,), "heart": (16,), "heart_filled": (16,)}

# Supersampling factor. The glyphs are rasterised this many times larger and
# boxed down, which is what keeps a 2-unit stroke smooth at 24 px.
SS = 8

# Material Symbols sit on the baseline and the tallest of them is 220/240 of
# the em. Placing the baseline here centres that run inside the square, so a
# bought-in glyph and a drawn one share the same optical centre.
BASELINE = 0.9583

GRID = 24.0  # design grid, in units


def canvas(size):
    """Supersampled alpha canvas for one icon, plus its units-to-pixels scale."""
    px = size * SS
    image = Image.new("L", (px, px), 0)
    return image, ImageDraw.Draw(image), px / GRID


def finish(image, size):
    return image.resize((size, size), Image.LANCZOS)


# --- drawn icons ----------------------------------------------------------

def draw_usb_stick(size, stroke=2.0):
    """A flash drive standing up, not the USB connector trident.

    The housing is taller than it is wide (10 x 13.2 units) on purpose: at the
    square proportions this started from, the shape read as a small bottle.
    """
    image, d, s = canvas(size)
    w = stroke * s
    bx0, bx1, by0, by1 = 7.0, 17.0, 8.2, 21.4
    # Flat top, rounded bottom: a rounded top corner is the other half of the
    # bottle look.
    d.rounded_rectangle([bx0 * s, by0 * s, bx1 * s, by1 * s], radius=2.2 * s,
                        outline=255, width=int(round(w)), corners=(False, False, True, True))
    px0, px1, py0, py1 = 8.7, 15.3, 2.2, 8.4
    d.rounded_rectangle([px0 * s, py0 * s, px1 * s, py1 * s], radius=0.8 * s,
                        fill=255, corners=(True, True, False, False))
    # The two punched contacts are what name the shape as USB.
    hole_w, hole_h = 1.72, 2.11
    for cx in (10.75, 13.25):
        d.rectangle([(cx - hole_w / 2) * s, 3.94 * s,
                     (cx + hole_w / 2) * s, (3.94 + hole_h) * s], fill=0)
    d.rounded_rectangle([8.6 * s, 12.2 * s, 15.4 * s, 14.2 * s], radius=0.9 * s, fill=255)
    return finish(image, size)


def draw_dlna(size):
    """The DLNA mark: a disc with three keyhole cutouts.

    The slits are 2.8 units wide against roughly 1.1 in the original artwork -
    at 24 px the original ones fill in and the disc turns into a blob.
    """
    image, d, s = canvas(size)
    radius = 10.9
    d.ellipse([(12 - radius) * s, (12 - radius) * s, (12 + radius) * s, (12 + radius) * s],
              fill=255)
    hole, half = 2.7, 1.4
    for cx, cy, direction in ((15.0, 7.0, -1), (9.0, 12.0, 1), (15.0, 17.0, -1)):
        x0, x1 = (cx, GRID) if direction > 0 else (0.0, cx)
        d.rectangle([x0 * s, (cy - half) * s, x1 * s, (cy + half) * s], fill=0)
        d.ellipse([(cx - hole) * s, (cy - hole) * s, (cx + hole) * s, (cy + hole) * s], fill=0)
    return finish(image, size)


# Measured off the Yandex Music icon: the centre was found as the largest
# inscribed circle, then a radial profile over all 360 degrees gave these
# twelve maxima. The graded lengths - short at the upper right, long at the
# lower left - are what make it a spark instead of a snowflake.
SPARK_RAYS = ((23, 0.61), (47, 0.71), (71, 0.80), (103, 0.92), (144, 1.00), (180, 0.93),
              (208, 0.82), (234, 0.69), (263, 0.59), (296, 0.53), (326, 0.53), (355, 0.55))
SPARK_CORE = 0.30


def draw_spark(size, cx=14.08, cy=10.24, rmax=11.3):
    image, d, s = canvas(size)
    points = []
    count = len(SPARK_RAYS)
    for index, (angle, reach) in enumerate(SPARK_RAYS):
        points.append((angle, reach * rmax))
        gap = (SPARK_RAYS[(index + 1) % count][0] - angle) % 360
        points.append((angle + gap / 2.0, SPARK_CORE * rmax))
    d.polygon([((cx + math.cos(math.radians(a)) * r) * s,
                (cy + math.sin(math.radians(a)) * r) * s) for a, r in points], fill=255)
    return finish(image, size)


# --- bought-in glyphs -----------------------------------------------------

def draw_glyph(size, font_path, codepoint, filled=False):
    image, d, _ = canvas(size)
    font = ImageFont.truetype(font_path, size * SS)
    if filled:
        # A hollow heart and a solid one are the same glyph at the two ends of
        # this font's FILL axis, not two codepoints. The axes are Fill, Grade,
        # Optical Size, Weight, and the last three are left where the default
        # instance has them so a filled icon matches the rest of the set.
        font.set_variation_by_axes([1, 0, 24, 400])
    d.text((size * SS / 2, size * SS * BASELINE), chr(codepoint), font=font, fill=255, anchor="ms")
    return finish(image, size)


# name in C, how to draw it
ICONS = (
    ("podcasts", ("glyph", 0xF048)),
    ("usb_stick", ("draw", draw_usb_stick)),
    ("sd_card", ("glyph", 0xE623)),
    ("bluetooth", ("glyph", 0xE1A7)),
    ("radio", ("glyph", 0xE03E)),
    ("dlna", ("draw", draw_dlna)),
    ("spark", ("draw", draw_spark)),
    ("settings", ("glyph", 0xE8B8)),
    # Not a source: the speaker in front of the player's volume bar. Drawn
    # from the same family so the footer does not mix two icon styles.
    ("volume", ("glyph", 0xE050)),
    # The like mark on the Yandex player screen, in both its states: empty
    # while the account has not marked the track, solid once it has.
    ("heart", ("glyph", 0xE87D)),
    ("heart_filled", ("glyph_filled", 0xE87D)),
)

HEADER_NOTE = """/* Generated by tools/gen_feed_icons.py - do not edit by hand.
 *
 * Alpha-only bitmaps for the home screen carousel. LVGL blends an A8 image
 * with the object's image_recolor, so one bitmap per size serves the accent
 * centre and both dimmed neighbours.
 */
"""


def emit(rows, out_c, out_h):
    with open(out_h, "w") as handle:
        handle.write(HEADER_NOTE)
        handle.write("\n#pragma once\n\n#include \"lvgl.h\"\n\n")
        for name, size, _ in rows:
            handle.write(f"extern const lv_image_dsc_t ui_feed_icon_{name}_{size};\n")

    with open(out_c, "w") as handle:
        handle.write(HEADER_NOTE)
        handle.write("\n#include \"ui_feed_icon_bitmaps.h\"\n")
        for name, size, image in rows:
            data = image.tobytes()
            handle.write(f"\nstatic const uint8_t s_{name}_{size}[] = {{\n")
            for offset in range(0, len(data), 16):
                chunk = ", ".join(f"0x{byte:02X}" for byte in data[offset:offset + 16])
                handle.write(f"    {chunk},\n")
            handle.write("};\n")
            handle.write(f"const lv_image_dsc_t ui_feed_icon_{name}_{size} = {{\n")
            handle.write("    .header = {.magic = LV_IMAGE_HEADER_MAGIC, "
                         ".cf = LV_COLOR_FORMAT_A8,\n")
            handle.write(f"               .w = {size}, .h = {size}, .stride = {size}}},\n")
            handle.write(f"    .data_size = sizeof(s_{name}_{size}),\n")
            handle.write(f"    .data = s_{name}_{size},\n")
            handle.write("};\n")


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", required=True,
                        help="Material Symbols Rounded variable TTF")
    parser.add_argument("--out-dir", default=os.path.join(here, "components", "ui"))
    parser.add_argument("--preview", help="also write a PNG contact sheet here")
    args = parser.parse_args()

    if not os.path.exists(args.font):
        sys.exit(f"font not found: {args.font}")

    rows = []
    for name, (kind, spec) in ICONS:
        for size in EXTRA_SIZES.get(name, SIZES):
            if kind == "glyph" or kind == "glyph_filled":
                image = draw_glyph(size, args.font, spec, filled=kind == "glyph_filled")
            else:
                image = spec(size)
            rows.append((name, size, image))

    emit(rows,
         os.path.join(args.out_dir, "ui_feed_icon_bitmaps.c"),
         os.path.join(args.out_dir, "ui_feed_icon_bitmaps.h"))
    total = sum(len(image.tobytes()) for _, _, image in rows)
    print(f"{len(rows)} bitmaps, {total} bytes of pixels")

    if args.preview:
        pad = 12
        width = sum(SIZES) + pad * (len(SIZES) + 1)
        sheet = Image.new("RGB", (width, (48 + pad) * len(ICONS) + pad), (0x10, 0x18, 0x20))
        y = pad
        for name, _ in ICONS:
            x = pad
            for size in EXTRA_SIZES.get(name, SIZES):
                image = next(i for n, s, i in rows if n == name and s == size)
                tile = Image.new("RGB", (size, size), (0x10, 0x18, 0x20))
                tile.paste(Image.new("RGB", (size, size), (0xF2, 0xA3, 0x3C)), (0, 0), image)
                sheet.paste(tile, (x, y + (48 - size) // 2))
                x += size + pad
            y += 48 + pad
        sheet.save(args.preview)
        print(f"preview: {args.preview}")


if __name__ == "__main__":
    main()
