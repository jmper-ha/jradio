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
# The default range is what the 14 px face carries: ASCII plus Cyrillic
# through the historic block. The 20 px face was built with the narrower
# 0x0400-0x045F, which covers Russian and its neighbours and nothing more -
# each generated file records in its own header what it was built with.
#
# lv_font_conv comes from npm and npx will fetch it on first use, so the very
# first run needs the network.
set -euo pipefail

if [ $# -lt 1 ]; then
    sed -n '2,20p' "$0" >&2
    exit 2
fi

size=$1
ranges=${2:-0x20-0x7F,0x0400-0x052F}
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
