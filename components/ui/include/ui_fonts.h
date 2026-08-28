#pragma once

#include "lvgl.h"

#include "ui_font_metrics.h"
#include "ui_layout.h"

/* The four faces every screen draws with, named by the job they do rather than
 * by their size. A screen asks for the title face; which face that is comes
 * from the panel's layout, so a bigger panel can carry a bigger one without a
 * single call site changing.
 *
 * Two families, for a reason that is not a preference:
 *
 * - the text faces are DejaVu Sans converted by tools/gen_ui_fonts.sh, because
 *   the UI is in Russian and LVGL's built-in Montserrat is Latin only;
 * - the icon faces are LVGL's Montserrat, because what is drawn with them is
 *   not text at all but the FontAwesome symbol block LVGL bundles into it.
 *
 * Adding a size to the text family is one run of the generator plus a line in
 * ui_font_metrics.h and an extern here. Adding one to the icon family is a
 * CONFIG_LV_FONT_MONTSERRAT_<size> line in sdkconfig.defaults - and forgetting
 * it is caught below rather than at the link. */

extern const lv_font_t ui_font_cyrillic_14;
extern const lv_font_t ui_font_cyrillic_20;

/* Two levels so the size macro is expanded before it is pasted. */
#define UI_FONT_TEXT_(px) ui_font_cyrillic_##px
#define UI_FONT_TEXT(px) UI_FONT_TEXT_(px)
#define UI_FONT_ICON_(px) lv_font_montserrat_##px
#define UI_FONT_ICON_FACE(px) UI_FONT_ICON_(px)
#define UI_FONT_MONT_ENABLED_(px) LV_FONT_MONTSERRAT_##px
#define UI_FONT_MONT_ENABLED(px) UI_FONT_MONT_ENABLED_(px)

/* Body text: notices, secondary lines, the settings fields. */
#define UI_FONT_BODY (&UI_FONT_TEXT(UI_FONT_BODY_PX))
/* Titles: list rows, group headings, the name of what is playing. */
#define UI_FONT_TITLE (&UI_FONT_TEXT(UI_FONT_TITLE_PX))
/* The symbol next to a row - a folder mark, a drive, a link. */
#define UI_FONT_ICON (&UI_FONT_ICON_FACE(UI_FONT_ICON_PX))
/* The one big glyph on a screen: the note standing in for missing cover art,
 * and the pairing code that has to be read off the panel from a distance. */
#define UI_FONT_DISPLAY (&UI_FONT_ICON_FACE(UI_FONT_DISPLAY_PX))

#if !UI_FONT_MONT_ENABLED(UI_FONT_ICON_PX)
#error "this layout's icon face is not built into LVGL - add the matching CONFIG_LV_FONT_MONTSERRAT_<size> to sdkconfig.defaults and to sdkconfig"
#endif
#if !UI_FONT_MONT_ENABLED(UI_FONT_DISPLAY_PX)
#error "this layout's display face is not built into LVGL - add the matching CONFIG_LV_FONT_MONTSERRAT_<size> to sdkconfig.defaults and to sdkconfig"
#endif
