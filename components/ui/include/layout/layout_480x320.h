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
 * The rows below have been spaced twice since: once after the faces grew - at
 * 26 px a title line is 35 px tall, and the first placement had the track row
 * starting before the one above it had finished - and again after the cover
 * did. */

/* Home screen carousel. Half again as wide a panel is half again as much room
 * for the ring, and at the 320 px sizes it sat in the middle of a lot of
 * nothing: the icons are 32/44/64 against 24/32/48, the tile 120 against 84,
 * and the neighbours stand further out to match - 100 and 190 against 68 and
 * 132. The outer pair still lands on the panel with 34 px to spare, so this
 * shape keeps all five slots.
 *
 * The sizes have to be ones tools/gen_feed_icons.py draws; it is told the
 * union of what every shape asks for. */
#define UI_FEED_INNER_DX 100
#define UI_FEED_OUTER_DX 190
#define UI_FEED_TILE 120
#define UI_FEED_ICON_LARGE_PX 64
#define UI_FEED_ICON_MEDIUM_PX 44
#define UI_FEED_ICON_SMALL_PX 32

/* The axis the ring is centred on. Six pixels above where it was, which is the
 * middle of what the title above and the dots below leave once the tile is
 * 120 tall rather than 84. */
#define UI_FEED_AXIS_Y 178

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
 * The tile is 160 px here, not the 96 the smaller panels use. It was 96 in the
 * first two builds of this shape and read as a stamp beside the text - which
 * it was: the panel grew by half in width and a third in height while the
 * cover did not move at all. 160 is a half of this panel's height, and it is
 * the largest square that still finishes above the rule.
 *
 * The number is not only the tile. Covers are *decoded* into this square -
 * see album_art_set_square() - so a bigger tile is a bigger picture and not
 * the same picture in a wider frame. The station icons the playlist editor
 * uploads are scaled in the browser to the same figure.
 *
 * What the text column loses to it, 288 px against 352, is worth having: a
 * name that fits in 288 was already fitting, and one that does not was being
 * dotted either way. */
#define UI_SRC_ART_SIZE 160
#define UI_SRC_ART_X 10
#define UI_SRC_ART_Y 36
#define UI_SRC_TEXT_X 182
#define UI_SRC_TEXT_W 288
#define UI_SRC_ROW_TITLE UI_SRC_ART_Y
#define UI_SRC_ROW_TRACK 76
#define UI_SRC_ROW_ARTIST 116
/* One line across the text column, under the performer. It clears the bottom
 * of the tile here, where on the 320x240 panel it sat beside it - either is
 * allowed, and the assertion in ui_layout.h checks both ways. */
#define UI_SRC_STREAM_X UI_SRC_TEXT_X
#define UI_SRC_STREAM_W UI_SRC_TEXT_W
#define UI_SRC_STREAM_H UI_SRC_LINE_H
#define UI_SRC_STREAM_Y 148
#define UI_SRC_RULE_TOP 202
#define UI_SRC_VU_Y 217

/* The footer, left to right: the buffer or the time on the left margin, the
 * like mark in the gap neither reading reaches, then the volume. The volume
 * bar takes 100 px here against 60: it is a control, and this is the one place
 * the extra width is worth spending on the same element rather than on more
 * of them. */
#define UI_SRC_FOOT_Y 288
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
 * the icon sizes have to be enabled in sdkconfig.
 *
 * 18 and 26, not the 14 and 20 the smaller panels use. The first build of this
 * shape kept those, on the reasoning that the panel is bigger but not much
 * finer - 167 dpi against 143 - so the same face would be nearly the same size
 * to the eye. On the screen it read as small, and the arithmetic says why:
 * against the panel rather than against a ruler, 14 px of a 320 px-tall screen
 * became 14 px of a 480x320 one, which is a quarter smaller as a share of what
 * the eye takes in at once. These two restore that share, and a little more.
 *
 * They cost rows, and that is the trade: five list rows where 14 px fitted
 * seven, six settings rows where it fitted nine. Both are what the panel this
 * board shipped with holds, at text half again the size. */
#define UI_FONT_BODY_PX 18
#define UI_FONT_TITLE_PX 26
/* 32, a step up like the text faces. */
#define UI_FONT_ICON_PX 32
/* The display face cannot take that step - 48 is the largest Montserrat LVGL
 * ships - and for a while that made the note standing in for a missing cover
 * the one glyph on this panel still sized for the smaller one, with room
 * inside a 160 px tile it could not use. The note is a bitmap now and this
 * face is left with the one job that is genuinely text: the Yandex pairing
 * code, read off the panel from a distance. */
#define UI_FONT_DISPLAY_PX 48

/* The note itself, at the same share of the tile the smaller panels give it -
 * two thirds of the square, which is a note half its height. A size
 * tools/gen_feed_icons.py has to have drawn; one it never drew is a link
 * error rather than a blank tile. */
#define UI_SRC_ART_NOTE_PX 107
