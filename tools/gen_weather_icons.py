#!/usr/bin/env python3
"""Draw the weather screensaver's colour pictures.

Why colour bitmaps and not the strip's A8 icons: the strip's weather is one
colour, recoloured to the theme, and at 16 px nobody wants more. The
screensaver is a weather station's face - a yellow sun, white and grey
clouds, blue rain - and a picture a metre away has to be read by its colours
before its shapes. So these are RGB with alpha, drawn here once and emitted
at the sizes the screen layouts ask for.

Every picture is drawn on a 96-unit grid at SS times the target size and
boxed down, which keeps a curve smooth at 20 px.

Usage:
    python3 tools/gen_weather_icons.py                     # write the C files
    python3 tools/gen_weather_icons.py --preview out.png   # a sheet to look at

Regenerate whenever a picture or a size changes.
"""

import argparse
import math
import os

from PIL import Image, ImageDraw

SS = 8
GRID = 96.0

# The palette. Drawn on a near-black ground, so nothing needs an outline:
# light shapes on dark read by their fill.
SUN_CORE = (255, 214, 79)
SUN_EDGE = (255, 160, 0)
SUN_RAY = (255, 193, 7)
MOON = (245, 230, 168)
MOON_SHADE = (214, 196, 128)
CLOUD_TOP = (255, 255, 255)
CLOUD_BOTTOM = (200, 212, 220)
DARK_TOP = (176, 190, 197)
DARK_BOTTOM = (120, 140, 152)
RAIN = (79, 195, 247)
SNOW = (236, 246, 255)
BOLT = (255, 202, 40)
FOG = (176, 190, 197)
GAUGE = (176, 190, 197)
NEEDLE = (242, 163, 60)
ARROW = (220, 230, 236)
HORIZON = (144, 164, 174)


class Pen:
    """One picture being drawn: an RGBA canvas at SS x size, in grid units."""

    def __init__(self, size):
        self.size = size
        self.px = size * SS
        self.scale = self.px / GRID
        self.image = Image.new("RGBA", (self.px, self.px), (0, 0, 0, 0))

    def u(self, value):
        return value * self.scale

    def layer(self):
        return Image.new("RGBA", (self.px, self.px), (0, 0, 0, 0))

    def put(self, layer):
        self.image = Image.alpha_composite(self.image, layer)

    def done(self):
        return self.image.resize((self.size, self.size), Image.LANCZOS)


def vertical_fill(pen, mask, top, bottom, y0, y1):
    """`mask` (L) filled with a top-to-bottom gradient between grid rows."""
    layer = pen.layer()
    span = max(1.0, pen.u(y1) - pen.u(y0))
    gradient = Image.new("RGBA", (1, pen.px))
    for y in range(pen.px):
        t = min(1.0, max(0.0, (y - pen.u(y0)) / span))
        gradient.putpixel((0, y), tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,))
    layer.paste(gradient.resize((pen.px, pen.px)), (0, 0), mask)
    return layer


def disc_mask(pen, cx, cy, r):
    mask = Image.new("L", (pen.px, pen.px), 0)
    ImageDraw.Draw(mask).ellipse([pen.u(cx - r), pen.u(cy - r), pen.u(cx + r), pen.u(cy + r)],
                                 fill=255)
    return mask


def sun(pen, cx, cy, r):
    rays = pen.layer()
    d = ImageDraw.Draw(rays)
    for i in range(8):
        a = i * math.pi / 4 + math.pi / 8
        x0, y0 = cx + math.cos(a) * r * 1.38, cy + math.sin(a) * r * 1.38
        x1, y1 = cx + math.cos(a) * r * 1.78, cy + math.sin(a) * r * 1.78
        w = r * 0.22
        d.line([(pen.u(x0), pen.u(y0)), (pen.u(x1), pen.u(y1))], fill=SUN_RAY + (255,),
               width=int(pen.u(w)))
        for x, y in ((x0, y0), (x1, y1)):
            d.ellipse([pen.u(x - w / 2), pen.u(y - w / 2), pen.u(x + w / 2), pen.u(y + w / 2)],
                      fill=SUN_RAY + (255,))
    pen.put(rays)
    core = pen.layer()
    # A radial glow: lighter in the middle, the way a sun is drawn on every
    # station's face.
    steps = 24
    for i in range(steps):
        t = i / (steps - 1)
        rr = r * (1.0 - 0.55 * t)
        colour = tuple(int(e + (c - e) * t) for c, e in zip(SUN_CORE, SUN_EDGE)) + (255,)
        ImageDraw.Draw(core).ellipse([pen.u(cx - rr), pen.u(cy - rr), pen.u(cx + rr),
                                      pen.u(cy + rr)], fill=colour)
    pen.put(core)


def moon(pen, cx, cy, r):
    full = disc_mask(pen, cx, cy, r)
    bite = disc_mask(pen, cx + r * 0.55, cy - r * 0.38, r * 0.86)
    crescent = Image.composite(Image.new("L", full.size, 0), full, bite)
    pen.put(vertical_fill(pen, crescent, MOON, MOON_SHADE, cy - r, cy + r))


def cloud_mask(pen, cx, cy, w):
    """A cumulus `w` units wide whose flat base sits at `cy + w * 0.22`."""
    mask = Image.new("L", (pen.px, pen.px), 0)
    d = ImageDraw.Draw(mask)
    base = cy + w * 0.22
    for ox, oy, rr in ((-0.28, 0.04, 0.20), (-0.04, -0.12, 0.27), (0.24, 0.0, 0.21)):
        x, y, r = cx + ox * w, cy + oy * w, rr * w
        d.ellipse([pen.u(x - r), pen.u(y - r), pen.u(x + r), pen.u(y + r)], fill=255)
    d.rounded_rectangle([pen.u(cx - w * 0.48), pen.u(cy - w * 0.02), pen.u(cx + w * 0.45),
                         pen.u(base)], radius=pen.u(w * 0.2), fill=255)
    return mask, cy - w * 0.39, base


def cloud(pen, cx, cy, w, dark=False):
    mask, top, base = cloud_mask(pen, cx, cy, w)
    colours = (DARK_TOP, DARK_BOTTOM) if dark else (CLOUD_TOP, CLOUD_BOTTOM)
    pen.put(vertical_fill(pen, mask, colours[0], colours[1], top, base))
    return base


def drop(pen, x, y, h, colour=RAIN):
    """A raindrop `h` tall with its tip at (x, y), leaning with the rain."""
    layer = pen.layer()
    d = ImageDraw.Draw(layer)
    r = h * 0.32
    bx, by = x - h * 0.18, y + h - r
    d.ellipse([pen.u(bx - r), pen.u(by - r), pen.u(bx + r), pen.u(by + r)], fill=colour + (255,))
    d.polygon([(pen.u(x), pen.u(y)), (pen.u(bx - r * 0.92), pen.u(by - r * 0.3)),
               (pen.u(bx + r * 0.92), pen.u(by + r * 0.3))], fill=colour + (255,))
    pen.put(layer)


def flake(pen, cx, cy, r, colour=SNOW):
    layer = pen.layer()
    d = ImageDraw.Draw(layer)
    w = int(pen.u(r * 0.24))
    for i in range(3):
        a = i * math.pi / 3 + math.pi / 2
        dx, dy = math.cos(a) * r, math.sin(a) * r
        d.line([(pen.u(cx - dx), pen.u(cy - dy)), (pen.u(cx + dx), pen.u(cy + dy))],
               fill=colour + (255,), width=w)
    d.ellipse([pen.u(cx - r * 0.26), pen.u(cy - r * 0.26), pen.u(cx + r * 0.26),
               pen.u(cy + r * 0.26)], fill=colour + (255,))
    pen.put(layer)


def bolt(pen, x, y, h):
    layer = pen.layer()
    pts = [(0.30, 0.0), (0.62, 0.0), (0.46, 0.38), (0.72, 0.38), (0.22, 1.0), (0.36, 0.52),
           (0.10, 0.52)]
    ImageDraw.Draw(layer).polygon([(pen.u(x + px * h * 0.7), pen.u(y + py * h)) for px, py in pts],
                                  fill=BOLT + (255,))
    pen.put(layer)


# --- the weather pictures, one per weather_icon_t ---------------------------

def clear_day(size):
    pen = Pen(size)
    sun(pen, 48, 48, 23)
    return pen.done()


def clear_night(size):
    pen = Pen(size)
    moon(pen, 46, 50, 30)
    return pen.done()


def partly_cloudy_day(size):
    pen = Pen(size)
    sun(pen, 36, 36, 17)
    cloud(pen, 54, 58, 66)
    return pen.done()


def partly_cloudy_night(size):
    pen = Pen(size)
    moon(pen, 36, 34, 22)
    cloud(pen, 54, 58, 66)
    return pen.done()


def cloudy(size):
    pen = Pen(size)
    cloud(pen, 60, 38, 56, dark=True)
    cloud(pen, 44, 56, 74)
    return pen.done()


def fog(size):
    pen = Pen(size)
    cloud(pen, 48, 36, 74)
    layer = pen.layer()
    d = ImageDraw.Draw(layer)
    for y, x0, x1 in ((62, 14, 82), (73, 22, 74), (84, 16, 66)):
        d.line([(pen.u(x0), pen.u(y)), (pen.u(x1), pen.u(y))], fill=FOG + (255,),
               width=int(pen.u(6)))
        for x in (x0, x1):
            d.ellipse([pen.u(x - 3), pen.u(y - 3), pen.u(x + 3), pen.u(y + 3)], fill=FOG + (255,))
    pen.put(layer)
    return pen.done()


def rain(size):
    pen = Pen(size)
    cloud(pen, 48, 34, 76, dark=True)
    for x, y in ((34, 62), (52, 66), (70, 62)):
        drop(pen, x, y, 22)
    return pen.done()


def snow(size):
    pen = Pen(size)
    cloud(pen, 48, 34, 76)
    for x, y in ((30, 72), (50, 78), (70, 72)):
        flake(pen, x, y, 10)
    return pen.done()


def sleet(size):
    pen = Pen(size)
    cloud(pen, 48, 34, 76, dark=True)
    drop(pen, 34, 62, 22)
    flake(pen, 52, 78, 10)
    drop(pen, 72, 62, 22)
    return pen.done()


def thunderstorm(size):
    pen = Pen(size)
    cloud(pen, 48, 32, 76, dark=True)
    bolt(pen, 36, 52, 40)
    drop(pen, 28, 64, 18)
    drop(pen, 72, 64, 18)
    return pen.done()


WEATHER = (("clear_day", clear_day), ("clear_night", clear_night),
           ("partly_cloudy_day", partly_cloudy_day), ("partly_cloudy_night", partly_cloudy_night),
           ("cloudy", cloudy), ("fog", fog), ("rain", rain), ("snow", snow), ("sleet", sleet),
           ("thunderstorm", thunderstorm))


# --- the readings' pictograms ---------------------------------------------

def humidity(size):
    pen = Pen(size)
    layer = pen.layer()
    d = ImageDraw.Draw(layer)
    d.ellipse([pen.u(22), pen.u(38), pen.u(74), pen.u(90)], fill=RAIN + (255,))
    d.polygon([(pen.u(48), pen.u(6)), (pen.u(23.5), pen.u(56)), (pen.u(72.5), pen.u(56))],
              fill=RAIN + (255,))
    pen.put(layer)
    return pen.done()


def pressure(size):
    pen = Pen(size)
    layer = pen.layer()
    d = ImageDraw.Draw(layer)
    d.arc([pen.u(10), pen.u(14), pen.u(86), pen.u(90)], 135, 45, fill=GAUGE + (255,),
          width=int(pen.u(9)))
    for a in (135, 180, 225, 270, 315, 360, 405):
        rad = math.radians(a)
        d.line([(pen.u(48 + math.cos(rad) * 26), pen.u(52 + math.sin(rad) * 26)),
                (pen.u(48 + math.cos(rad) * 31), pen.u(52 + math.sin(rad) * 31))],
               fill=GAUGE + (255,), width=int(pen.u(4)))
    rad = math.radians(300)
    d.line([(pen.u(48), pen.u(52)), (pen.u(48 + math.cos(rad) * 30), pen.u(52 + math.sin(rad) * 30))],
           fill=NEEDLE + (255,), width=int(pen.u(7)))
    d.ellipse([pen.u(41), pen.u(45), pen.u(55), pen.u(59)], fill=NEEDLE + (255,))
    pen.put(layer)
    return pen.done()


def wind(size, from_deg):
    """An arrow pointing where the wind goes: from_deg 0 (a north wind)
    points down the screen. Eight of them, rather than one turned at run
    time: LVGL's rotation of a 20 px picture smears it."""
    pen = Pen(size)
    layer = pen.layer()
    d = ImageDraw.Draw(layer)
    # Screen angle of the direction the air moves: north wind blows south.
    a = math.radians(from_deg + 90)
    dx, dy = math.cos(a), math.sin(a)
    tip = (48 + dx * 38, 48 + dy * 38)
    tail = (48 - dx * 34, 48 - dy * 34)
    d.line([(pen.u(tail[0]), pen.u(tail[1])), (pen.u(48 + dx * 14), pen.u(48 + dy * 14))],
           fill=ARROW + (255,), width=int(pen.u(11)))
    side = (-dy, dx)
    d.polygon([(pen.u(tip[0]), pen.u(tip[1])),
               (pen.u(48 + dx * 6 + side[0] * 24), pen.u(48 + dy * 6 + side[1] * 24)),
               (pen.u(48 + dx * 6 - side[0] * 24), pen.u(48 + dy * 6 - side[1] * 24))],
              fill=ARROW + (255,))
    pen.put(layer)
    return pen.done()


def sun_on_horizon(size, rising):
    pen = Pen(size)
    layer = pen.layer()
    d = ImageDraw.Draw(layer)
    d.pieslice([pen.u(22), pen.u(42), pen.u(74), pen.u(94)], 180, 360, fill=SUN_CORE + (255,))
    for a in (200, 240, 270, 300, 340):
        rad = math.radians(a)
        d.line([(pen.u(48 + math.cos(rad) * 32), pen.u(68 + math.sin(rad) * 32)),
                (pen.u(48 + math.cos(rad) * 41), pen.u(68 + math.sin(rad) * 41))],
               fill=SUN_RAY + (255,), width=int(pen.u(6)))
    d.line([(pen.u(6), pen.u(70)), (pen.u(90), pen.u(70))], fill=HORIZON + (255,),
           width=int(pen.u(6)))
    # The arrow under the horizon says which way the sun is going.
    if rising:
        d.polygon([(pen.u(48), pen.u(76)), (pen.u(36), pen.u(92)), (pen.u(60), pen.u(92))],
                  fill=NEEDLE + (255,))
    else:
        d.polygon([(pen.u(48), pen.u(94)), (pen.u(36), pen.u(78)), (pen.u(60), pen.u(78))],
                  fill=NEEDLE + (255,))
    pen.put(layer)
    return pen.done()


PICTOGRAMS = (("humidity", humidity), ("pressure", pressure),
              ("sunrise", lambda s: sun_on_horizon(s, True)),
              ("sunset", lambda s: sun_on_horizon(s, False))) + tuple(
                  ("wind_%d" % deg, (lambda d: lambda s: wind(s, d))(deg))
                  for deg in range(0, 360, 45))


def preview(path):
    sizes = (96, 64, 48, 32, 24, 20)
    cell = 104
    rows = len(WEATHER) + 1
    sheet = Image.new("RGB", (cell * len(sizes) + 16, rows * cell + 16), (10, 10, 11))
    for row, (name, draw) in enumerate(WEATHER):
        for col, size in enumerate(sizes):
            icon = draw(size)
            x = 8 + col * cell + (cell - size) // 2
            y = 8 + row * cell + (cell - size) // 2
            sheet.paste(icon, (x, y), icon)
    row = len(WEATHER)
    for col, (name, draw) in enumerate(PICTOGRAMS[:len(sizes)]):
        icon = draw(40)
        sheet.paste(icon, (8 + col * cell + 32, 8 + row * cell + 32), icon)
    wide = Image.new("RGB", (sheet.width, sheet.height + cell), (10, 10, 11))
    wide.paste(sheet, (0, 0))
    for col, (name, draw) in enumerate(PICTOGRAMS[len(sizes):]):
        icon = draw(40)
        wide.paste(icon, (8 + col * 80 + 20, sheet.height + 20), icon)
    wide.save(path)


# The sizes the screen layouts use: 80 and 48 on the 480x320 panels (the
# current weather and a forecast day), 56 and 32 on the 320x240 ones, the
# readings' pictograms at 20 and 16. Only what a layout references is linked
# (--gc-sections drops the rest), but every size listed here is source text in
# the repository - colour pictures are 3 bytes a pixel - so the list holds
# only what a shape file asks for.
WEATHER_SIZES = (32, 48, 56, 80)
PICTOGRAM_SIZES = (16, 20)

HEADER_NOTE = """/* Generated by tools/gen_weather_icons.py - do not edit by hand.
 *
 * The weather screensaver's colour pictures, RGB565 with an alpha plane
 * (LV_COLOR_FORMAT_RGB565A8): the colour plane, two bytes a pixel, then one
 * byte of alpha a pixel. Not recoloured to the theme - a sun is yellow.
 */
"""


def rgb565a8(image):
    colour = bytearray()
    alpha = bytearray()
    for r, g, b, a in image.getdata():
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        colour += bytes((value & 0xFF, value >> 8))
        alpha.append(a)
    return bytes(colour + alpha)


def rows():
    for name, draw in WEATHER:
        for size in WEATHER_SIZES:
            yield name, size, draw(size)
    for name, draw in PICTOGRAMS:
        for size in PICTOGRAM_SIZES:
            yield name, size, draw(size)


def emit(out_c, out_h):
    made = list(rows())
    with open(out_h, "w") as handle:
        handle.write(HEADER_NOTE)
        handle.write("\n#pragma once\n\n#include \"lvgl.h\"\n\n")
        for name, size, _ in made:
            handle.write(f"extern const lv_image_dsc_t ui_wx_{name}_{size};\n")
        handle.write("\n// The picture `name` at `size` px, as a shape file names its sizes.\n")
        handle.write("#define UI_WX_BITMAP_(name, size) ui_wx_##name##_##size\n")
        handle.write("#define UI_WX_BITMAP(name, size) UI_WX_BITMAP_(name, size)\n")
    with open(out_c, "w") as handle:
        handle.write(HEADER_NOTE)
        handle.write("\n#include \"ui_weather_icon_bitmaps.h\"\n")
        for name, size, image in made:
            data = rgb565a8(image)
            handle.write(f"\nstatic const uint8_t s_{name}_{size}[] = {{\n")
            for offset in range(0, len(data), 24):
                # Decimal: a third shorter than hex for the same bytes.
                chunk = ",".join(str(byte) for byte in data[offset:offset + 24])
                handle.write(f"    {chunk},\n")
            handle.write("};\n")
            handle.write(f"const lv_image_dsc_t ui_wx_{name}_{size} = {{\n")
            handle.write("    .header = {.magic = LV_IMAGE_HEADER_MAGIC, "
                         ".cf = LV_COLOR_FORMAT_RGB565A8,\n")
            handle.write(f"               .w = {size}, .h = {size}, .stride = {size * 2}}},\n")
            handle.write(f"    .data_size = sizeof(s_{name}_{size}),\n")
            handle.write(f"    .data = s_{name}_{size},\n")
            handle.write("};\n")


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", help="write a sheet of every picture here instead")
    args = parser.parse_args()
    if args.preview:
        preview(args.preview)
        return
    emit(os.path.join(here, "components", "ui", "ui_weather_icon_bitmaps.c"),
         os.path.join(here, "components", "ui", "include", "ui_weather_icon_bitmaps.h"))


if __name__ == "__main__":
    main()
