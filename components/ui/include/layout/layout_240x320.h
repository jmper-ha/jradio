#pragma once

/* Portrait 240x320: the same panel stood on end. Measured on the panel - see
 * the note at the top of ui_layout.h for why these are a file rather than a
 * formula. */

/* The ring drops to the middle of the taller screen. */
#define UI_FEED_AXIS_Y 168

/* The empty-browser screen: a drive above the sentence that says what is wrong
 * with it, together in the middle of the space the rows leave behind. */
#define UI_LIST_NOTICE_ICON_Y 124
#define UI_LIST_NOTICE_TEXT_Y 188

/* The same share of the band as on the wider panel, so the hint on the right
 * keeps the room its words need. The scheme is dropped because there is no room
 * for one and nothing is lost - see ui_web_address.h. */
#define UI_SET_BAND_ADDRESS_W 130
#define UI_SET_BAND_SHOW_SCHEME false

/* Player screen: the cover moves above the text instead of beside it. It has
 * to - at 240 px wide, a 96 px tile alongside would leave 130 for the name, and
 * the name is the one thing on this screen worth reading from across the room.
 * Stacking costs about a hundred pixels of height, which is exactly what the
 * taller panel gives back, so the cover keeps its 96 px and album_art is
 * untouched.
 *
 * The tile sits on the left - inset from the margin rather than against it, so
 * it does not read as having fallen off the edge - and the three stream
 * readings take the column it leaves, stacked one per line and centred against
 * its height. That is what pays for the rows below: the readings used to have a
 * line of their own under the performer, and giving it up is what lets the
 * name, the album and the performer breathe instead of being packed four deep.
 *
 * Those three rows are centred on the panel, not on the column beside the cover
 * - they are the width of the screen, and anything else reads as a landscape
 * screen that fell over. */
/* The cover tile, the same square the landscape panel uses: this one is
 * narrower still, and the readings sit beside it. */
#define UI_SRC_ART_SIZE 96
#define UI_SRC_ART_X (UI_CONTENT_X + 10)
#define UI_SRC_ART_Y 32
#define UI_SRC_TEXT_X UI_CONTENT_X
#define UI_SRC_TEXT_W UI_CONTENT_W
/* Starts 10 px past the tile and runs to the right margin. */
#define UI_SRC_STREAM_X (UI_SRC_ART_X + UI_SRC_ART_SIZE + 10)
#define UI_SRC_STREAM_W (UI_CONTENT_X + UI_CONTENT_W - UI_SRC_STREAM_X)
#define UI_SRC_STREAM_H (3 * UI_SRC_LINE_H)
#define UI_SRC_STREAM_Y (UI_SRC_ART_Y + (UI_SRC_ART_SIZE - UI_SRC_STREAM_H) / 2)
#define UI_SRC_ROW_TITLE 138
#define UI_SRC_ROW_TRACK 162
#define UI_SRC_ROW_ARTIST 192
#define UI_SRC_RULE_TOP 226
#define UI_SRC_VU_Y 234

/* The same five things as the wider panel in 80 px less. Four of them are as
 * short as they can be - "01:24 / 03:52" is the longest reading the left slot
 * ever shows, and the mark, the icon and the number are fixed - so the bar is
 * the one that gives, at 44 px instead of 60. */
#define UI_SRC_FOOT_Y 290
#define UI_SRC_LIKE_X 116
#define UI_SRC_VOLUME_ICON_X 136
#define UI_SRC_VOLUME_BAR_X 156
#define UI_SRC_VOLUME_BAR_W 44
#define UI_SRC_VOLUME_TEXT_X 206

/* How the player screen is arranged - see layout_320x240.h. Both are yes here:
 * the readings stack one per line in the column beside the cover, and the three
 * name rows are centred under it, because they are the width of the screen and
 * anything else reads as a landscape screen that fell over. */
#define UI_SRC_STREAM_LINES 1
#define UI_SRC_TEXT_CENTRED 1

/* The faces this shape is laid out for. Every row pitch and every line height
 * below follows from them, so a panel that wants bigger text states it here
 * and the layout moves with it. The text sizes have to exist in
 * ui_font_metrics.h; the icon sizes have to be enabled in sdkconfig. */
#define UI_FONT_BODY_PX 14
#define UI_FONT_TITLE_PX 20
#define UI_FONT_ICON_PX 24
#define UI_FONT_DISPLAY_PX 48
