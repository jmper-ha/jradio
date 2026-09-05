#pragma once

/* Portrait 320x480: the ILI9488 stood on end, added 2026-09-05.
 *
 * Placed, not measured. No portrait ILI9488 has been built - display/ili9488.h
 * says the same about its MADCTL baseline - so every number here is a first
 * placement and a candidate to be moved once somebody has looked at the
 * result. What is not a guess is that it fits: ui_layout.h's assertions and
 * tests/test_ui_layout.c hold on these values.
 *
 * It is the 240x320 shape's arrangement - the cover above the names, the
 * readings in the column beside it - carried onto a panel a third wider and
 * half again taller, with the 480x320 panel's faces, because it is the same
 * glass at the same pitch and 14 px read as small on it. */

/* The same ring the landscape ILI9488 draws. At 320 px wide the outer pair
 * falls off the edge and UI_FEED_SLOTS comes out at three, exactly as it does
 * on the 240x320 panel - the sizes themselves have never needed to differ.
 * UI_FEED_OUTER_DX is kept at the landscape figure rather than trimmed to fit:
 * it is what decides the count, and a value that squeezed the outer pair on
 * would put it 6 px from the edge. */
#define UI_FEED_INNER_DX 100
#define UI_FEED_OUTER_DX 190
#define UI_FEED_TILE 120
#define UI_FEED_ICON_LARGE_PX 64
#define UI_FEED_ICON_MEDIUM_PX 44
#define UI_FEED_ICON_SMALL_PX 32

/* The ring drops down the taller screen, keeping the share of the band
 * between the strip and the dots that the landscape shape settled on - a
 * little below the middle, because the dots and the notice below them read as
 * weight the eye subtracts. */
#define UI_FEED_AXIS_Y 270

/* The empty-browser screen: a drive above the sentence that says what is wrong
 * with it, together in the middle of the space the rows leave behind. Nine
 * rows leave a taller gap than five, so both move down with it. */
#define UI_LIST_NOTICE_ICON_Y 200
#define UI_LIST_NOTICE_TEXT_Y 280

/* What the address keeps for itself in the band along the bottom, dotted past
 * that. The hint on the right is sized to its own text and pinned to the edge,
 * and at the 18 px face those words take most of what a 300 px content width
 * leaves. The scheme is dropped for the same reason the 240x320 panel drops
 * it - see ui_web_address.h. */
#define UI_SET_BAND_ADDRESS_W 150
#define UI_SET_BAND_SHOW_SCHEME false

/* Player screen: the cover above the text, as on the other portrait shape. It
 * has to be - at 320 px wide, a 160 px tile alongside would leave 140 for the
 * name, and the name is the one thing on this screen worth reading from across
 * the room.
 *
 * The tile is the landscape panel's 160 and not the 96 the small portrait
 * panel uses: covers are decoded into this square - see album_art_set_square()
 * - so it is the picture and not only its frame, and the height a portrait
 * panel has spare is exactly what pays for it. The three readings take the
 * column it leaves, stacked one per line and centred against its height. */
#define UI_SRC_ART_SIZE 160
#define UI_SRC_ART_X (UI_CONTENT_X + 10)
#define UI_SRC_ART_Y 44
#define UI_SRC_TEXT_X UI_CONTENT_X
#define UI_SRC_TEXT_W UI_CONTENT_W
/* Starts 10 px past the tile and runs to the right margin. */
#define UI_SRC_STREAM_X (UI_SRC_ART_X + UI_SRC_ART_SIZE + 10)
#define UI_SRC_STREAM_W (UI_CONTENT_X + UI_CONTENT_W - UI_SRC_STREAM_X)
#define UI_SRC_STREAM_H (3 * UI_SRC_LINE_H)
#define UI_SRC_STREAM_Y (UI_SRC_ART_Y + (UI_SRC_ART_SIZE - UI_SRC_STREAM_H) / 2)
/* Each row clears the one above it by its own face's line height and a little
 * air: 24 + 8 under the album line, 35 + 7 under the track. */
#define UI_SRC_ROW_TITLE 226
#define UI_SRC_ROW_TRACK 258
#define UI_SRC_ROW_ARTIST 300
#define UI_SRC_RULE_TOP 348
#define UI_SRC_VU_Y 360

/* The footer, left to right: the buffer or the time on the left margin, the
 * like mark in the gap neither reading reaches, then the volume. "01:24 /
 * 03:52" is the longest the left slot ever shows and at the 18 px face it
 * reaches about x=140, which is what puts the mark at 150. */
#define UI_SRC_FOOT_Y 440
#define UI_SRC_LIKE_X 150
#define UI_SRC_VOLUME_ICON_X 174
#define UI_SRC_VOLUME_BAR_X 196
#define UI_SRC_VOLUME_BAR_W 72
#define UI_SRC_VOLUME_TEXT_X 276

/* How the player screen is arranged - see layout_320x240.h. Both are yes here,
 * as on the other portrait shape: the readings stack one per line in the
 * column beside the cover, and the three name rows are centred under it,
 * because they are the width of the screen and anything else reads as a
 * landscape screen that fell over. */
#define UI_SRC_STREAM_LINES 1
#define UI_SRC_TEXT_CENTRED 1

/* The faces this shape is laid out for: the landscape ILI9488's, for the
 * reason that file gives at length - it is the same glass, and 14 px of a
 * 480 px-tall screen is a quarter smaller a share of what the eye takes in
 * than 14 px of a 320 px one. They cost rows, and this panel has rows to
 * spend: nine in a list and ten in Settings, where the landscape shape gets
 * five and six. */
#define UI_FONT_BODY_PX 18
#define UI_FONT_TITLE_PX 26
#define UI_FONT_ICON_PX 32
/* 48 is the largest Montserrat LVGL ships. It bounds nothing on this shape any
 * more - the note standing in for a missing cover is a bitmap sized by
 * UI_SRC_ART_NOTE_PX below - and is left as the face the Yandex pairing code
 * is read off the panel with. */
#define UI_FONT_DISPLAY_PX 48

/* The note drawn on an empty cover tile, at the same share of it the smaller
 * panels' note takes: two thirds of the tile, which is a note half its height.
 * A size the generator has to have drawn - see tools/gen_feed_icons.py. */
#define UI_SRC_ART_NOTE_PX 107
