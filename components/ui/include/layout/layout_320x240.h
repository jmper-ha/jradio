#pragma once

/* Landscape 320x240: the shape revision 1 ships with, and the one every screen
 * was designed on. Measured on the panel, not calculated - see the note at the
 * top of ui_layout.h for why these are a file rather than a formula.
 *
 * A new panel shape copies this file, renames it to its own size, and works
 * through the blocks below. ui_layout.h's assertions say when a number is
 * wrong; nothing here can be left out. */

/* Home screen carousel: the axis the ring of icons is centred on. It sits
 * between the title under the strip and the row of dots, closer to the dots
 * than centring would put it. */
#define UI_FEED_AXIS_Y 138

/* The empty-browser screen: a drive above the sentence that says what is wrong
 * with it, together in the middle of the space the rows leave behind. */
#define UI_LIST_NOTICE_ICON_Y 84
#define UI_LIST_NOTICE_TEXT_Y 148

/* What the address keeps for itself in the band along the bottom, dotted past
 * that. The hint on the right is sized to its own text and pinned to the edge
 * instead of taking a share: a fixed width would either clip the words or steal
 * room from the address, and the one case that overruns 190 px - the setup AP,
 * whose band names a network as well as an address - is exactly the case the QR
 * itself answers. */
#define UI_SET_BAND_ADDRESS_W 190
#define UI_SET_BAND_SHOW_SCHEME true

/* Player screen: the cover on the left, the names in a column beside it.
 *
 * The title shares the tile's top edge, which is what ties the two columns
 * together on a wide panel. */
#define UI_SRC_ART_X 10
#define UI_SRC_ART_Y 36
#define UI_SRC_TEXT_X 118
#define UI_SRC_TEXT_W 192
#define UI_SRC_ROW_TITLE UI_SRC_ART_Y
#define UI_SRC_ROW_TRACK 58
#define UI_SRC_ROW_ARTIST 86
/* One line across the text column, under the performer. */
#define UI_SRC_STREAM_X UI_SRC_TEXT_X
#define UI_SRC_STREAM_W UI_SRC_TEXT_W
#define UI_SRC_STREAM_H UI_SRC_LINE_H
#define UI_SRC_STREAM_Y 108
#define UI_SRC_RULE_TOP 144
#define UI_SRC_VU_Y 155

/* The footer, left to right: the buffer or the time on the left margin, the
 * like mark in the gap neither reading reaches, then the volume.
 *
 * The volume sits twelve pixels further right than it used to, which is where
 * the mark came from: it is a control, so it belongs beside the other one
 * rather than pushed out to an edge on its own. The numbers still end clear of
 * the edge - the bar runs to 268 and three digits to about 295. */
#define UI_SRC_FOOT_Y 214
#define UI_SRC_LIKE_X 150
#define UI_SRC_VOLUME_ICON_X 190
#define UI_SRC_VOLUME_BAR_X 208
#define UI_SRC_VOLUME_BAR_W 60
#define UI_SRC_VOLUME_TEXT_X 274

/* How the player screen is arranged, asked as two plain questions so ui.c
 * never has to know which panel it was built for. Both are no here: the
 * readings run as one line across the text column, and the name rows are read
 * down their left edge beside the tile. */
#define UI_SRC_STREAM_LINES 0
#define UI_SRC_TEXT_CENTRED 0
