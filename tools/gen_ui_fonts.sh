#!/usr/bin/env bash
# Generate a Cyrillic text face for the UI at a given pixel size.
#
# The UI is in Russian and LVGL's built-in Montserrat is Latin only, so the two
# text faces are DejaVu Sans converted to LVGL's own format. A panel of another
# size wants larger ones - the size is part of a layout, not of the firmware -
# and this is how they are made.
#
# Usage:
#     bash tools/gen_ui_fonts.sh <size> [unicode-ranges]
#
# The default range is the UI's own language plus what the device only reads
# and never wrote: file names off a drive, ICY titles, tags. Those arrive in
# whatever alphabet the album was released in, so the accented Latin blocks and
# the punctuation a tagger reaches for are as necessary as the Cyrillic - a
# character with no glyph is drawn as a box, which is how the German album on
# the test drive read as damaged rather than as unsupported.
#
# 0x0300-0x030F earns its place for a subtler reason. A name can arrive
# decomposed - "u" followed by a combining diaeresis rather than the single
# character - and FATFS hands it over exactly as it was written. The converter
# gives those marks zero advance and a negative offset, so LVGL draws them back
# over the letter before and the pair reads correctly with no composition step.
#
# The default deliberately leaves the Cyrillic bound out: the two existing
# faces do not agree on it, and neither can be changed without moving rows on
# every screen. 0x0460-0x048F alone raises the 20 px line height from 23 to 26.
# Pass the range in full when regenerating one of them - each generated file
# records in its own header what it was built with.
#
# lv_font_conv comes from npm and npx will fetch it on first use, so the very
# first run needs the network.
set -euo pipefail

if [ $# -lt 1 ]; then
    sed -n '2,32p' "$0" >&2
    exit 2
fi

size=$1
ranges=${2:-0x20-0x7F,0xA0-0x17F,0x0300-0x030F,0x2010-0x2027,0x2116,0x0400-0x052F}
project_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
font=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
out="${project_dir}/components/ui/ui_font_cyrillic_${size}.c"

[ -f "${font}" ] || { echo "missing ${font} - install fonts-dejavu-core" >&2; exit 1; }

npx --yes lv_font_conv --no-compress --font "${font}" --size "${size}" --bpp 4 \
    --format lvgl -r "${ranges}" -o "${out}"

# The converter writes an include for a stand-alone LVGL checkout. This build
# gets LVGL as a managed component, whose headers sit on the include path
# directly, so the path with the directory in it does not resolve. The two
# faces that were generated before this script existed carry the same edit.
sed -i 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|' "${out}"

line_height=$(sed -n 's/.*\.line_height = \([0-9]*\).*/\1/p' "${out}" | head -1)
echo
echo "wrote ${out} ($(wc -c < "${out}") bytes, line height ${line_height})"
cat <<TEXT

Three lines make it usable. Nothing finds them for you, and the first of the
three is the one that matters: the layout is compiled against that number, and
a wrong copy moves every row on every screen by a pixel or two.

  components/ui/include/ui_font_metrics.h
      #define UI_FONT_CYRILLIC_${size}_LINE_H ${line_height}

  components/ui/include/ui_fonts.h
      extern const lv_font_t ui_font_cyrillic_${size};

  components/ui/CMakeLists.txt, in SRCS
      "ui_font_cyrillic_${size}.c"

Then point a layout at it with UI_FONT_BODY_PX or UI_FONT_TITLE_PX. A face no
layout selects is dropped by the linker and costs no flash.
TEXT
