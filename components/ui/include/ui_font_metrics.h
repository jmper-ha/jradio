#pragma once

/* What each Cyrillic face measures, stated here rather than read from the font
 * at run time.
 *
 * The layout needs a line height before there is a program: rows are spaced by
 * the face they carry, and those spacings are compile-time constants that
 * assertions check against the panel. lv_font_t knows its own line_height, but
 * only once LVGL is running, which is far too late to refuse to build.
 *
 * So the number is copied here from the generated font's own header, and ui.c
 * checks the copy against the font at start-up - a mistyped line here would
 * otherwise show up as rows that overlap by a pixel or two on one screen.
 *
 * Kept apart from ui_fonts.h because ui_layout.h includes this and must not
 * drag in LVGL: the layout is arithmetic, and the host tests compile it. */

#define UI_FONT_CYRILLIC_14_LINE_H 19
#define UI_FONT_CYRILLIC_20_LINE_H 23

/* Two levels, so the argument is expanded before it is pasted: the roles below
 * are given a size macro, not a literal. */
#define UI_FONT_LINE_H_(px) UI_FONT_CYRILLIC_##px##_LINE_H
#define UI_FONT_LINE_H(px) UI_FONT_LINE_H_(px)

/* The two roles the layout measures with. The faces themselves are named in
 * ui_fonts.h, which needs LVGL; a line height does not. */
#define UI_FONT_BODY_LINE_H UI_FONT_LINE_H(UI_FONT_BODY_PX)
#define UI_FONT_TITLE_LINE_H UI_FONT_LINE_H(UI_FONT_TITLE_PX)
