#pragma once

/* Landscape 480x320: the ILI9488 panel, added 2026-09-04.
 *
 * These are a first placement, not measurements. The 320x240 file beside this
 * one records numbers that were moved on a screen until they looked right;
 * this one starts from that layout carried onto a panel half again as wide and
 * a third taller, and every number here is a candidate to be moved once
 * somebody has looked at the result. Where a number is doing something other
 * than scaling, it says so.
 *
 * What is *not* a guess is that it fits: ui_layout.h's thirteen assertions all
 * hold on these values, so nothing here runs off an edge or collides with its
 * neighbour. That is a different claim from looking good.
 *
 * The faces stay at 14 and 20 px, which is worth stating because it looks like
 * an oversight. This panel is physically bigger but not much finer - roughly
 * 165 dpi against the 143 of the 2.8" it replaces - so the same face is nearly
 * the same size to the eye, and the extra room buys rows instead: seven in a
 * list where there were five, nine settings rows where there were six. Larger
 * faces are one run of tools/gen_ui_fonts.sh plus a line in ui_font_metrics.h
 * if that turns out to be the wrong call. */

/* Home screen carousel: the axis the ring of icons is centred on. Held at the
 * same fraction of the panel's height as the 320x240 layout puts it, which is
 * below the middle - closer to the row of dots than centring would be. */
#define UI_FEED_AXIS_Y 184

/* The empty-browser screen: a drive above the sentence that says what is wrong
 * with it, together in the middle of the space the rows leave behind. Seven
 * rows leave a taller gap than five did, so both move down with it. */
#define UI_LIST_NOTICE_ICON_Y 118
#define UI_LIST_NOTICE_TEXT_Y 182

/* What the address keeps for itself in the band along the bottom, dotted past
 * that. The hint on the right is sized to its own text and pinned to the edge,
 * so the extra 160 px of panel all go to the address - which is the half that
 * was being dotted, and on this panel the setup AP's longer band fits whole. */
#define UI_SET_BAND_ADDRESS_W 300
#define UI_SET_BAND_SHOW_SCHEME true

/* Player screen: the cover on the left, the names in a column beside it.
 *
 * The tile stays 96 px - UI_SRC_ART_SIZE is not the shape file's to set - so
 * the width this panel adds goes to the text column, which is 352 px against
 * the 192 it had. That is the change most worth looking at on the screen: a
 * station name that used to be dotted now fits, and the tile may look small
 * beside it. */
#define UI_SRC_ART_X 10
#define UI_SRC_ART_Y 40
#define UI_SRC_TEXT_X 118
#define UI_SRC_TEXT_W 352
#define UI_SRC_ROW_TITLE UI_SRC_ART_Y
#define UI_SRC_ROW_TRACK 76
#define UI_SRC_ROW_ARTIST 110
/* One line across the text column, under the performer. It clears the bottom
 * of the tile here, where on the 320x240 panel it sat beside it - either is
 * allowed, and the assertion in ui_layout.h checks both ways. */
#define UI_SRC_STREAM_X UI_SRC_TEXT_X
#define UI_SRC_STREAM_W UI_SRC_TEXT_W
#define UI_SRC_STREAM_H UI_SRC_LINE_H
#define UI_SRC_STREAM_Y 142
#define UI_SRC_RULE_TOP 192
#define UI_SRC_VU_Y 207

/* The footer, left to right: the buffer or the time on the left margin, the
 * like mark in the gap neither reading reaches, then the volume. The volume
 * bar takes 100 px here against 60: it is a control, and this is the one place
 * the extra width is worth spending on the same element rather than on more
 * of them. */
#define UI_SRC_FOOT_Y 285
#define UI_SRC_LIKE_X 200
#define UI_SRC_VOLUME_ICON_X 280
#define UI_SRC_VOLUME_BAR_X 306
#define UI_SRC_VOLUME_BAR_W 100
#define UI_SRC_VOLUME_TEXT_X 420

/* How the player screen is arranged, asked as two plain questions so ui.c
 * never has to know which panel it was built for. Both are no here, as on the
 * other landscape shape: the readings run as one line across the text column,
 * and the name rows are read down their left edge beside the tile. */
#define UI_SRC_STREAM_LINES 0
#define UI_SRC_TEXT_CENTRED 0

/* The faces this shape is laid out for. Every row pitch and every line height
 * below follows from them. The text sizes have to exist in ui_font_metrics.h;
 * the icon sizes have to be enabled in sdkconfig. */
#define UI_FONT_BODY_PX 14
#define UI_FONT_TITLE_PX 20
#define UI_FONT_ICON_PX 24
#define UI_FONT_DISPLAY_PX 48
