#!/usr/bin/env python3
"""Draw the boot splash: a green magic-eye tube and the jRadio wordmark.

Everything is drawn at SS times the final size and boxed down, which is what
keeps the wedge edges and the rotated lettering smooth on a panel that is
RGB565 and has no antialiasing of its own.

Both orientations are drawn and both are emitted into one C file, guarded by
BOARD_DISPLAY_PORTRAIT: the firmware compiles whichever the DISPLAY line in
board_options.h selects, and changing that line needs no regeneration here.
The one that is not selected costs nothing - it is inside a false #if and
never becomes data.

Emits the preview PNGs and the run-length encoded pixels the firmware draws
(components/board/boot_splash_image.c). Rerun after any change here.

Usage:
    python3 tools/gen_boot_splash.py
"""

import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

# Set per orientation by render(); every drawing function reads them.
W, H = 320, 240
SS = 4  # supersampling factor

# Z003 was picked over the two heavier faces: the chancery italic is the
# closest thing installed here to 50s American sign lettering.
CHOSEN = "script"

FONTS = {
    "bookman": ("/usr/share/fonts/opentype/urw-base35/URWBookman-DemiItalic.otf", 44),
    "script": ("/usr/share/fonts/opentype/urw-base35/Z003-MediumItalic.otf", 62),
    "palatino": ("/usr/share/fonts/opentype/urw-base35/P052-BoldItalic.otf", 44),
}

# Tube: centred, sitting a touch above the middle so the wordmark has room
# below it without covering the eye.
# Proportions taken off a photograph of a lit 6E5P: the dark centre is half
# the diameter of the lit target, and the shadow sector is a third of a
# turn wide.
TUBE = dict(cx=158.0, cy=108.0, r_screen=52.0, r_hub=25.0, r_rim=57.0)
# The tube is the same size either way up - it is a part, not a decoration that
# scales with the canvas. Only where it sits changes, and with it where the
# wordmark lands: beside and below the eye on the wide panel, straight under it
# on the tall one, which is the only arrangement that leaves both room.
LAYOUT = {
    "landscape": dict(size=(320, 240), tube=(158.0, 108.0), wordmark=(160, 165)),
    "portrait": dict(size=(240, 320), tube=(120.0, 124.0), wordmark=(120, 224)),
}
WEDGE_CENTER_DEG = 90.0  # 6 o'clock, the way a 6E5 is normally seen
WEDGE_HALF_DEG = 33.0

GREEN = (40, 252, 128)
GREEN_DIM = (18, 120, 56)
MAROON = (190, 38, 62)
MAROON_DARK = (92, 12, 26)


def scaled(value):
    return int(round(value * SS))


def background():
    """Near-black with a faint green lift behind the tube."""
    edge = Image.new("RGB", (W * SS, H * SS), (5, 6, 6))
    core = Image.new("RGB", (W * SS, H * SS), (16, 24, 20))
    # radial_gradient is square and black in the middle, so it doubles as the
    # blend mask: centre keeps `core`, edges fall to `edge`.
    mask = Image.radial_gradient("L").resize((W * SS, H * SS), Image.LANCZOS)
    return Image.composite(edge, core, mask)


def tube_layer():
    """The magic eye, as an RGB image over black plus its own glow."""
    size = (W * SS, H * SS)
    cx, cy = scaled(TUBE["cx"]), scaled(TUBE["cy"])
    r_screen, r_hub = scaled(TUBE["r_screen"]), scaled(TUBE["r_hub"])
    r_rim = scaled(TUBE["r_rim"])
    start = WEDGE_CENTER_DEG - WEDGE_HALF_DEG
    end = WEDGE_CENTER_DEG + WEDGE_HALF_DEG

    # Brightness as a mask, so the phosphor, the shadow sector and the glow all
    # come off one shape. Rings rather than a resized radial gradient: the
    # target is nearly even and lifts a little short of the rim, and a square
    # gradient only reaches full value at the corners of the canvas.
    shade = Image.new("L", size, 0)
    sd = ImageDraw.Draw(shade)
    rings = 200
    for i in range(rings, -1, -1):
        r = r_hub + (r_screen - r_hub) * i / rings
        t = i / rings
        # Peak at 0.75 of the way out, easing off at both ends.
        value = int(255 - 46 * abs(t - 0.75) / 0.75)
        sd.ellipse((cx - r, cy - r, cx + r, cy + r), fill=value)
    # The shadow sector is dimmer phosphor, not an unlit gap - on the tube it
    # reads as a paler green wedge with hard radial edges.
    sd.pieslice((cx - r_screen, cy - r_screen, cx + r_screen, cy + r_screen),
                start, end, fill=176)
    # Two dark seams cross the target left and right, wider at the hub. They
    # are what stops the lit ring looking like a printed circle.
    for angle in (0.0, 180.0):
        rad = math.radians(angle)
        nx, ny = -math.sin(rad), math.cos(rad)
        inner_w, outer_w = scaled(2.6), scaled(1.0)
        x0, y0 = cx + r_hub * math.cos(rad), cy + r_hub * math.sin(rad)
        x1, y1 = cx + r_screen * math.cos(rad), cy + r_screen * math.sin(rad)
        sd.polygon([(x0 + nx * inner_w, y0 + ny * inner_w),
                    (x1 + nx * outer_w, y1 + ny * outer_w),
                    (x1 - nx * outer_w, y1 - ny * outer_w),
                    (x0 - nx * inner_w, y0 - ny * inner_w)], fill=10)
    sd.ellipse((cx - r_hub, cy - r_hub, cx + r_hub, cy + r_hub), fill=0)

    layer = Image.new("RGB", size, (0, 0, 0))
    layer.paste(Image.new("RGB", size, GREEN), mask=shade)

    ld = ImageDraw.Draw(layer)
    # Hub: the anode cap, unlit but not black - it picks up the glow around it.
    ld.ellipse((cx - r_hub, cy - r_hub, cx + r_hub, cy + r_hub), fill=(10, 34, 24))
    ld.ellipse((cx - r_hub * 0.55, cy - r_hub * 0.75, cx + r_hub * 0.15,
                cy - r_hub * 0.25), fill=(18, 52, 38))
    # Rim: the glass edge of the envelope, lit from the target inside it.
    ld.ellipse((cx - r_rim, cy - r_rim, cx + r_rim, cy + r_rim),
               outline=(74, 84, 78), width=scaled(4))
    ld.arc((cx - r_rim, cy - r_rim, cx + r_rim, cy + r_rim), 185, 265,
           fill=(146, 158, 148), width=scaled(4))

    glow_mask = shade.filter(ImageFilter.GaussianBlur(scaled(11)))
    glow_mask = glow_mask.point(lambda v: v * 62 // 100)
    glow = Image.new("RGB", size, (0, 0, 0))
    glow.paste(Image.new("RGB", size, GREEN_DIM), mask=glow_mask)
    return layer, glow


def wordmark(font_key):
    """The rotated 'jRadio' as an RGBA layer, already positioned on the canvas."""
    path, size = FONTS[font_key]
    font = ImageFont.truetype(path, size * SS)
    text = "jRadio"

    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    box = probe.textbbox((0, 0), text, font=font, stroke_width=scaled(2))
    pad = scaled(26)
    tw, th = box[2] - box[0] + 2 * pad, box[3] - box[1] + 2 * pad
    layer = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    ox, oy = pad - box[0], pad - box[1]

    # Shadow first: the wordmark crosses the lit tube, and without it the
    # maroon disappears into the green.
    d.text((ox + scaled(3), oy + scaled(3)), text, font=font, fill=(0, 0, 0, 190),
           stroke_width=scaled(3), stroke_fill=(0, 0, 0, 190))
    d.text((ox, oy), text, font=font, fill=MAROON + (255,),
           stroke_width=scaled(2), stroke_fill=MAROON_DARK + (255,))
    return layer


def render(orientation, font_key, out_path):
    """Draws one orientation. The drawing code reads module-level geometry, so
    this is where it is set - once per image, before anything is drawn."""
    global W, H, TUBE, WORDMARK
    layout = LAYOUT[orientation]
    W, H = layout["size"]
    TUBE = dict(TUBE, cx=layout["tube"][0], cy=layout["tube"][1])
    WORDMARK = layout["wordmark"]
    return compose(font_key, out_path)


def compose(font_key, out_path):
    canvas = background()
    tube, glow = tube_layer()
    canvas = Image.blend(canvas, Image.new("RGB", canvas.size, (0, 0, 0)), 0.0)
    canvas = ImageMath_add(canvas, glow)
    canvas.paste(tube, mask=tube.convert("L").point(lambda v: 255 if v > 6 else 0))

    text_layer = wordmark(font_key).rotate(22, resample=Image.BICUBIC, expand=True)
    # Right and down of the tube centre, so the eye stays visible above it.
    centre = (scaled(WORDMARK[0]), scaled(WORDMARK[1]))
    pos = (centre[0] - text_layer.width // 2, centre[1] - text_layer.height // 2)
    canvas.paste(text_layer, pos, text_layer)

    final = canvas.resize((W, H), Image.LANCZOS)
    final.save(out_path)
    return final


def ImageMath_add(base, addition):
    """Screen-ish additive blend, kept simple: glow only ever brightens."""
    from PIL import ImageChops
    return ImageChops.add(base, addition)


def encode_rle(image):
    """(count, RGB565) pairs in raster order.

    Raw pixels would be 150 KB. This picture is a dark ground with smooth
    gradients, so it holds only 479 distinct colours in long runs and the same
    image comes to about a third of that - and the decoder that reads it back
    is a dozen lines, filling the band buffer the display already uses.
    """
    pairs = []
    previous = None
    count = 0
    for r, g, b in image.convert("RGB").getdata():
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        if value == previous and count < 0xFFFF:
            count += 1
            continue
        if previous is not None:
            pairs.append((count, previous))
        previous, count = value, 1
    pairs.append((count, previous))
    return pairs


def rle_lines(image):
    pairs = encode_rle(image)
    lines = []
    for index in range(0, len(pairs), 4):
        chunk = pairs[index:index + 4]
        lines.append("    " + " ".join(f"0x{c:04X}, 0x{v:04X}," for c, v in chunk))
    return pairs, lines


def write_c_source(images, path):
    landscape_pairs, landscape_lines = rle_lines(images["landscape"])
    portrait_pairs, portrait_lines = rle_lines(images["portrait"])
    newline = chr(10)
    with open(path, "w") as out:
        out.write(f"""/* Generated by tools/gen_boot_splash.py - do not edit by hand.
 *
 * The boot splash, run-length encoded as (count, RGB565) pairs in raster
 * order. See boot_splash.h for why it is encoded at all, and img/splash for
 * the pictures these came from.
 *
 * Both orientations are here and the board option picks one. The other is
 * inside a false #if, so it never becomes data and costs no flash - which is
 * why turning the panel on its end does not mean rerunning the generator.
 */

#include "boot_splash.h"

#if BOARD_DISPLAY_PORTRAIT

const uint16_t boot_splash_rle[] = {{
{newline.join(portrait_lines)}
}};

const size_t boot_splash_rle_pairs = {len(portrait_pairs)}U;

#else

const uint16_t boot_splash_rle[] = {{
{newline.join(landscape_lines)}
}};

const size_t boot_splash_rle_pairs = {len(landscape_pairs)}U;

#endif
""")
    return landscape_pairs, portrait_pairs


def main():
    project = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    root = os.path.join(project, "img", "splash")
    os.makedirs(root, exist_ok=True)

    images = {}
    for orientation in ("landscape", "portrait"):
        path = os.path.join(root, f"splash_{orientation}.png")
        images[orientation] = render(orientation, CHOSEN, path)
        print("wrote", path)

    source = os.path.join(project, "components", "board", "boot_splash_image.c")
    landscape_pairs, portrait_pairs = write_c_source(images, source)
    print(f"wrote {source}: "
          f"landscape {len(landscape_pairs)} runs ({len(landscape_pairs) * 4} bytes), "
          f"portrait {len(portrait_pairs)} runs ({len(portrait_pairs) * 4} bytes)")


if __name__ == "__main__":
    main()
