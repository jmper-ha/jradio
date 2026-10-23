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
extern const lv_font_t ui_font_cyrillic_18;
extern const lv_font_t ui_font_cyrillic_20;
extern const lv_font_t ui_font_cyrillic_26;
/* The levelling's reading over the volume figure, and nothing else: digits,
 * signs (the en dash is the minus, as in the 14 px face), an ellipsis and the letters of "дБ", "dB", "макс" and "max" - a
 * face a step below the 14 px body, which on a 320 px panel is the volume
 * figure's own. See UI_SRC_LEVEL_PX. */
extern const lv_font_t ui_font_level_12;
/* DSEG7 Classic Bold (SIL OFL), digits, colon, dash and period only: the
 * screensaver's clock and the FM frequency. The period has no width of its
 * own - it sits in the gap after the digit before it, as on a real display.
 * Five sizes, chosen by the shape file; only the ones a shape names are
 * linked in. */
extern const lv_font_t ui_font_seven_segment_40;
extern const lv_font_t ui_font_seven_segment_48;
extern const lv_font_t ui_font_seven_segment_56;
extern const lv_font_t ui_font_seven_segment_72;
extern const lv_font_t ui_font_seven_segment_112;
/* The weather screensaver's clock and temperature: DejaVu Sans Bold, digits
 * and the signs between them only (0-9 : + - – ° …), which is what keeps a
 * 56 px face at 56 KB of source. Made with lv_font_conv like
 * tools/gen_ui_fonts.sh, with -r 0x20,0x2B,0x2D,0x30-0x3A,0xB0,0x2013,0x2026. */
extern const lv_font_t ui_font_bold_digits_40;
extern const lv_font_t ui_font_bold_digits_56;
#define UI_FONT_BOLD_DIGITS_(px) ui_font_bold_digits_##px
#define UI_FONT_BOLD_DIGITS(px) UI_FONT_BOLD_DIGITS_(px)

/* Two levels so the size macro is expanded before it is pasted. */
#define UI_FONT_TEXT_(px) ui_font_cyrillic_##px
#define UI_FONT_TEXT(px) UI_FONT_TEXT_(px)
#define UI_FONT_ICON_(px) lv_font_montserrat_##px
#define UI_FONT_ICON_FACE(px) UI_FONT_ICON_(px)
#define UI_FONT_MONT_ENABLED_(px) LV_FONT_MONTSERRAT_##px
#define UI_FONT_MONT_ENABLED(px) UI_FONT_MONT_ENABLED_(px)

#define UI_FONT_SEGMENT_(px) ui_font_seven_segment_##px
#define UI_FONT_SEGMENT(px) UI_FONT_SEGMENT_(px)
/* The screensaver's clock, and nothing else: a face with eleven glyphs. */
#define UI_FONT_SAVER_CLOCK (&UI_FONT_SEGMENT(UI_SAVER_CLOCK_PX))
#define UI_FONT_FM_DIGITS (&UI_FONT_SEGMENT(UI_SRC_FM_DIGITS_PX))
/* The two lines under it. */
#define UI_FONT_SAVER_TEXT (&UI_FONT_TEXT(UI_SAVER_TEXT_PX))

/* Body text: notices, secondary lines, the settings fields. */
#define UI_FONT_BODY (&UI_FONT_TEXT(UI_FONT_BODY_PX))
/* A step below the volume figure it stands over: the 12 px face where the
 * figure is 14, the 14 px one where it is larger. */
#if UI_SRC_LEVEL_PX == 12
#define UI_FONT_LEVEL (&ui_font_level_12)
#else
#define UI_FONT_LEVEL (&UI_FONT_TEXT(UI_SRC_LEVEL_PX))
#endif
/* Titles: list rows, group headings, the name of what is playing. */
#define UI_FONT_TITLE (&UI_FONT_TEXT(UI_FONT_TITLE_PX))
/* The list home screen's rows: the title face, unless the shape has no room
 * for it - see UI_MENU_FONT_PX in ui_layout.h. */
#define UI_FONT_MENU (&UI_FONT_TEXT(UI_MENU_FONT_PX))
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
