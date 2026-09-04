#pragma once

#include <stdbool.h>

#include "board_display_profile.h"
#include "ui_buffer_graph.h"
#include "ui_font_metrics.h"

/* Where every screen's geometry lives.
 *
 * Split out of ui.c so it can be compiled on the host: a layout is arithmetic
 * over the panel's size, and arithmetic is exactly what a test can check
 * without a device in front of it.
 *
 * Two kinds of number live here, and the difference is the whole point of the
 * file. Most are consequences - a row count is the height left over divided by
 * a pitch, a right-hand column is the width less a margin - and those are
 * written as the division, so a panel nobody has held yet gets them right.
 *
 * The rest were placed by eye on a screen and cannot be recovered by
 * arithmetic. They live in layout/<width>x<height>.h, one file per panel shape,
 * and a shape with no file is a compile error that names what has to be filled
 * in. Deriving them was tried: centring the carousel between the title and the
 * dot row, for instance, misses the two measured positions by 7 px and 3 px in
 * opposite directions. A formula that reproduces neither would silently move a
 * layout that was already signed off on both panels, which is a worse outcome
 * than asking for a file. */

/* Height of the status strip every screen carries. It holds one line of the
 * body face - the screen's name, the clock, the signal reading - so it follows
 * that face rather than standing as the 26 the first panel needed. Seven
 * pixels of air is what 26 was on the 14 px face. */
#define UI_STRIP_H (UI_FONT_BODY_LINE_H + 7)

/* The margin every full-width block keeps, and what is left of the panel
 * between two of them. Written against TFT_WIDTH so one layout serves both
 * orientations; on a landscape board it is the 300 that used to be typed at
 * each of the eleven use sites. */
#define UI_CONTENT_X 10
#define UI_CONTENT_W (TFT_WIDTH - 2 * UI_CONTENT_X)

/* The strip's three slots, placed from the panel's own width rather than from
 * numbers measured once on a 320 px panel: the name on the left margin, the
 * clock centred, the signal group pinned to the right with its reading beside
 * it. Landscape works out to 10 / 116 wide, 130, 252 and 275 - exactly what
 * was there before - so nothing on this board moves. */
#define UI_STRIP_CONTEXT_X 10
#define UI_STRIP_CLOCK_W 60
#define UI_STRIP_CLOCK_X ((TFT_WIDTH - UI_STRIP_CLOCK_W) / 2)
/* Room for three digits of RSSI and a minus, clear of the right edge. */
#define UI_STRIP_RSSI_X (TFT_WIDTH - 45)
/* Four bars at a pitch of 5, ending just before the reading. */
#define UI_STRIP_BARS_X (UI_STRIP_RSSI_X - 23)
/* Stops short of the clock instead of running under it - which is what a
 * fixed 116 would do once the panel is only 240 wide. */
#define UI_STRIP_CONTEXT_W (UI_STRIP_CLOCK_X - UI_STRIP_CONTEXT_X - 4)

/* Home screen carousel. Every icon is a 24x24 design scaled to one of three
 * sizes, so a single axis is enough - the old row needed a per-glyph vertical
 * inset because each FontAwesome symbol had its own height, and the inset only
 * ever fitted one of them. */
#define UI_FEED_CENTER_X (TFT_WIDTH / 2)
#define UI_FEED_INNER_DX 68
#define UI_FEED_OUTER_DX 132
#define UI_FEED_TILE 84
#define UI_FEED_ICON_LARGE_PX 48
#define UI_FEED_ICON_MEDIUM_PX 32
#define UI_FEED_ICON_SMALL_PX 24
/* Five slots or three, decided by whether the outer pair fits rather than by
 * which panel this is. Only the centre slot is tile-sized; the neighbours are
 * exactly icon-sized, which is why the edge is measured against the icon and
 * not against UI_FEED_TILE. A 320 px panel gets 5 and a 240 px one gets 3,
 * which is what each was set to by hand.
 *
 * The ring itself does not change with the count - the same items in the same
 * order, wrapping the same way - only how many of them are in view. */
#define UI_FEED_SLOTS \
    ((UI_FEED_CENTER_X - UI_FEED_OUTER_DX - UI_FEED_ICON_SMALL_PX / 2 >= 4) ? 5 : 3)
/* Three is the narrowest the ring goes, so a panel where even that runs off
 * the edge has no carousel at all and has to be caught here. */
_Static_assert(UI_FEED_CENTER_X - UI_FEED_INNER_DX - UI_FEED_ICON_MEDIUM_PX / 2 >= 4,
               "the carousel does not fit this panel even at three slots");

/* One dot per item, so a ring of eight does not feel endless. */
#define UI_FEED_DOT 6
#define UI_FEED_DOT_PITCH 10
#define UI_FEED_DOT_Y (TFT_HEIGHT - 40)
/* The last line of the screen, under the dots. */
#define UI_FEED_NOTICE_Y (TFT_HEIGHT - 22)

/* The other home screen: the same items as a list. Eight rows of 24 px start
 * right under the strip and end at 222, which is every pixel a 240 px screen
 * has - the previous 27 px pitch fitted seven items and ran off the bottom as
 * soon as the SD card row appeared. The icon column is the carousel's 24 px
 * bitmap, so both home screens name an item the same way. */
#define UI_MENU_ROW_Y (UI_STRIP_H + 4)
/* A row is one line of the title face, and the rows are flush: the pitch and
 * the height are the same number because the face's own line height already
 * carries the space between lines. Written as the face rather than as 24, so a
 * shape that asks for bigger text gets rows to match instead of clipped
 * letters. */
#define UI_MENU_ROW_PITCH UI_FONT_TITLE_LINE_H
#define UI_MENU_ROW_H UI_FONT_TITLE_LINE_H
#define UI_MENU_ROW_X 6
#define UI_MENU_ROW_W (TFT_WIDTH - 12)
#define UI_MENU_ICON_X 12
#define UI_MENU_TEXT_PAD 38

/* Settings screen. Group headings carry the 20 px face and the fields under
 * them stay at 14, so the three groups read as the structure of the screen
 * rather than as rows of equal weight.
 *
 * The pitch has to clear the heading, which is one line of the title face -
 * the same height the home screen's rows use for the same face, plus a pixel
 * so two groups do not touch. The first row sits ten pixels under the strip,
 * and the strip moves with the body face, so this follows it. */
#define UI_SET_ROW_Y (UI_STRIP_H + 10)
#define UI_SET_ROW_PITCH (UI_FONT_TITLE_LINE_H + 1)
#define UI_SET_ROW_H UI_FONT_TITLE_LINE_H
/* The scroll chevrons sit in the right margin, and a row that carries a switch
 * stops short of them. A row that does not is full width and runs past: its
 * text is dotted long before it reaches the column, and giving it the same
 * stop would waste the width the longest field names need.
 *
 * This is where the glyph *starts*, and a label is drawn to the right of its
 * position, so the face's width has to come out of it or the arrow hangs off
 * the panel. UI_CONTENT_W alone did exactly that - by four pixels at 24 px and
 * by twelve at 32 - and nobody saw it, because until the chevrons were given
 * a font that has arrows in it they were drawing LVGL's placeholder box. */
#define UI_SET_CHEVRON_X (UI_CONTENT_X + UI_CONTENT_W - UI_FONT_ICON_PX)
/* And where every row stops, so the column the arrows stand in is a column.
 *
 * Only the rows carrying a switch used to stop here; the rest ran the full
 * content width and passed under the arrow. The reasoning was about text - a
 * field name is dotted long before it reaches this far, so the extra width was
 * free - and it was wrong about the tile: a row's background is opaque and
 * reaches wherever the row does, so the right edge came out ragged, with the
 * upper arrow sitting inside a rectangle and the lower one flush against the
 * end of a shorter one. Eight pixels of air, and both arrows have a column to
 * themselves that nothing else enters. */
#define UI_SET_ROW_RIGHT (UI_SET_CHEVRON_X - 8)
/* Text column of a row with a switch: the switch is 42 px at the left margin,
 * and the label starts after it. */
#define UI_SET_SWITCH_TEXT_X 58
/* The band along the bottom is the only place the device says how to reach its
 * web UI. It sits below everything else and never scrolls away - but it is the
 * last thing the cursor reaches, and a click there puts the address up as a QR
 * code, which is the only way a phone gets it without someone reading digits
 * out loud. */
#define UI_SET_BAND_Y (TFT_HEIGHT - 30)
#define UI_SET_BAND_H 30
#define UI_SET_BAND_PAD 12
/* What the notice line above the band keeps for itself: one line of the body
 * face and five pixels of air. */
#define UI_SET_NOTICE_H (UI_FONT_BODY_LINE_H + 5)

/* The QR code and the white card behind it. The card is the quiet zone: the
 * generator fills its canvas edge to edge whenever the modules divide into it
 * evenly, and a code that runs straight into the dark background is a code a
 * phone refuses to read. 18 px of white on every side is four modules at the
 * smallest scale either payload produces. */
#define UI_QR_SIZE 144
#define UI_QR_CARD 180
#define UI_QR_CARD_Y 4

/* List screens: rows start straight under the strip, and a rule and the
 * position bar close the screen the way they close the player's. */
#define UI_LIST_ROW_Y (UI_STRIP_H + 6)
/* A list row carries the name in the title face, and unlike the home screen's
 * rows it is spaced apart: six pixels inside the row and six more between
 * them, so a highlighted row reads as a block and not as one line of a
 * paragraph. */
#define UI_LIST_ROW_PITCH (UI_FONT_TITLE_LINE_H + 12)
#define UI_LIST_ROW_H (UI_FONT_TITLE_LINE_H + 6)
/* Width the folder glyph reserves at the left of a row: the icon face's own
 * size, which is what a Montserrat symbol advances by, plus the gap before the
 * name. Written as the face rather than as the 30 it came to on the first
 * panel, so a shape that asks for a bigger icon gets a column to put it in
 * instead of one the glyph overhangs. */
#define UI_LIST_ICON_W (UI_FONT_ICON_PX + 6)
/* Width the index reserves on a station row: two digits of the title face and
 * the gap before the name. A station row is never a directory, so this and the
 * folder mark never both apply; a file row has no index and keeps the left
 * edge it always had.
 *
 * Both halves are measured off the face rather than typed. A DejaVu digit
 * advances by 0.637 of the face's size - 12.75 px at 20, 16.56 at 26, read out
 * of the generated fonts themselves - and the gap is a little under half a
 * size, which is the 9 px the 20 px face had. Written as 34 it was a column
 * the 26 px face filled to within a pixel, and the number sat against the
 * name. */
#define UI_LIST_NUMBER_W (UI_FONT_TITLE_PX * 1274 / 1000 + UI_FONT_TITLE_PX * 45 / 100)
/* What the rule and the position bar need below the last row. */
#define UI_LIST_TAIL_H 18
#define UI_LIST_NOTICE_ICON 48

/* Player screen, in device pixels. A status strip carries what is not about
 * the music - clock, signal - and everything below it is the playing item. The
 * bottom row pairs the two things that answer "is it going to keep playing,
 * and how loud": buffer on the left, volume on the right. */
#define UI_SRC_STATUS_H 26
/* One line of each face, from the generated fonts' own line_height. Rows are
 * spaced by these rather than by eye, so nothing can overlap its neighbour -
 * and now that the face is the shape's to choose, they follow it rather than
 * standing as the 19 and 23 the two known panels happen to need. */
#define UI_SRC_LINE_H UI_FONT_BODY_LINE_H
/* The buffer reading drawn as a strip of bars instead of a number, standing
 * where the text stands. Its width belongs to the strip - see
 * ui_buffer_graph.h, where the bar count decides how much history it holds -
 * and only its height is the layout's, one body line less the space the face
 * leaves under its baseline, so the two forms occupy the same row. */
#define UI_SRC_BUFFER_GRAPH_H (UI_SRC_LINE_H - 4)
#define UI_SRC_BUFFER_GRAPH_Y (UI_SRC_FOOT_Y + 2)
#define UI_SRC_TRACK_H UI_FONT_TITLE_LINE_H
#define UI_SRC_VU_BLOCK_H 10
#define UI_SRC_VU_PITCH 18
/* Where the meter starts, just past the L and R marks on the left margin. */
#define UI_SRC_VU_X (UI_CONTENT_X + 20)
#define UI_SRC_PROGRESS_H 4
/* A hairline while it only reports, twice that while it is being aimed: the
 * bar is the control in scrubbing mode, and a 4 px target is not one. It grows
 * about its own centre line, so the row it lives on does not shift. */
#define UI_SRC_PROGRESS_SEEK_H 8
#define UI_SRC_PAUSE_SIZE 76
#define UI_SRC_PAUSE_BAR_W 10
#define UI_SRC_PAUSE_BAR_H 34
#define UI_SRC_PAUSE_GAP 10

/* The numbers that were placed by looking at a screen. One file per panel
 * shape; see the note at the top of this header for why they are not derived. */
#if TFT_WIDTH == 320 && TFT_HEIGHT == 240
#include "layout/layout_320x240.h"
#elif TFT_WIDTH == 240 && TFT_HEIGHT == 320
#include "layout/layout_240x320.h"
#elif TFT_WIDTH == 480 && TFT_HEIGHT == 320
#include "layout/layout_480x320.h"
#else
#error "no layout for this panel shape - copy components/ui/include/layout/layout_320x240.h to layout_<TFT_WIDTH>x<TFT_HEIGHT>.h, place the blocks it lists on the screen, and add an arm above"
#endif

/* Everything below follows from the panel and from the shape file, and is the
 * same arithmetic for every panel. */

/* Rows fit or they do not, and the count is the space left over divided by the
 * pitch rather than a number per panel. Landscape comes out at 5 list rows and
 * 6 settings rows, portrait at 7 and 9 - the four literals these replace.
 *
 * A row past the end used to be invisible rather than clipped, because the
 * notice line is opaque and painted after the rows: the mistake had to be
 * caught by an assertion or not at all. Dividing instead of counting is what
 * makes it impossible. */
#define UI_STATION_LIST_MAX_ROWS \
    ((unsigned)((TFT_HEIGHT - UI_LIST_ROW_Y - UI_LIST_TAIL_H) / UI_LIST_ROW_PITCH))
#define UI_SETTINGS_MAX_ROWS                                                    \
    (1U + (unsigned)((UI_SET_BAND_Y - UI_SET_NOTICE_H - UI_SET_ROW_Y - UI_SET_ROW_H) / \
                     UI_SET_ROW_PITCH))
/* The chevron is drawn from its position rightwards, so the face's own width
 * is what decides whether it lands on the panel. */
_Static_assert(UI_SET_CHEVRON_X + UI_FONT_ICON_PX <= TFT_WIDTH - UI_CONTENT_X,
               "the settings screen's scroll chevron runs off the right edge");
/* And it stands level with a row, in that row's own band - there is nowhere
 * else for it to go, with ten pixels between the strip and the first row and
 * the notice line and the address band under the last. So it has to fit the
 * row's height as well as the margin's width. */
_Static_assert(UI_FONT_ICON_PX <= UI_SET_ROW_H,
               "the settings screen's scroll chevron is taller than the row it stands beside");
/* A row with a switch starts past the switch and ends at that column; what is
 * left has to be enough to read a field name in. */
_Static_assert(UI_SET_ROW_RIGHT - UI_SET_SWITCH_TEXT_X >= 120,
               "the settings screen's switch rows have no room left for their text");
/* And the index has to be wider than the digits that go in it, or the number
 * is printed against the name it labels. */
_Static_assert(UI_LIST_NUMBER_W > UI_FONT_TITLE_PX * 1274 / 1000,
               "the station index leaves no gap before the name");
_Static_assert(UI_STATION_LIST_MAX_ROWS >= 3U,
               "fewer than three list rows fit this panel");
_Static_assert(UI_SETTINGS_MAX_ROWS >= 3U,
               "fewer than three settings rows fit above the web band");

#define UI_LIST_RULE_Y (UI_LIST_ROW_Y + (int)UI_STATION_LIST_MAX_ROWS * UI_LIST_ROW_PITCH + 6)
#define UI_LIST_PROGRESS_Y (UI_LIST_RULE_Y + 8)
_Static_assert(UI_LIST_PROGRESS_Y + UI_SRC_PROGRESS_H <= TFT_HEIGHT,
               "the list's position bar runs off the bottom of the panel");

/* Twenty blocks between the L/R marks and the right margin. The block and the
 * gap both come out of the width instead of being set per panel: a 320 px
 * panel gives a pitch of 14 and a 240 px one 10, which split into 11 + 3 and
 * 8 + 2 - exactly the pairs that were written by hand.
 *
 * The count is the meter's resolution and stays. Twenty is also the number
 * LVGL can afford to see change at once - see the invalidation note in
 * ui_update_vu_meter(). */
#define UI_VU_SEGMENTS 20U
#define UI_VU_SEGMENT_PITCH ((TFT_WIDTH - UI_CONTENT_X - UI_SRC_VU_X) / (int)UI_VU_SEGMENTS)
#define UI_VU_SEGMENT_GAP (UI_VU_SEGMENT_PITCH / 4)
#define UI_VU_SEGMENT_W (UI_VU_SEGMENT_PITCH - UI_VU_SEGMENT_GAP)
_Static_assert(UI_VU_SEGMENT_W >= 4, "the level meter's blocks are too narrow to read");
/* Twenty blocks and nineteen gaps, and the right margin. */
_Static_assert(UI_SRC_VU_X + (int)UI_VU_SEGMENTS * UI_VU_SEGMENT_PITCH - UI_VU_SEGMENT_GAP <=
                   TFT_WIDTH - UI_CONTENT_X,
               "the level meter runs off the panel");

/* The two rules frame the meter, so the gap below is derived from the gap
 * above instead of being typed in again. Written out by hand they drifted to
 * 10 and 17 px, which read as the meter having slipped upwards. */
#define UI_SRC_VU_GAP (UI_SRC_VU_Y - (UI_SRC_RULE_TOP + 1))
#define UI_SRC_VU_BOTTOM (UI_SRC_VU_Y + UI_SRC_VU_PITCH + UI_SRC_VU_BLOCK_H)
#define UI_SRC_RULE_BOTTOM (UI_SRC_VU_BOTTOM + UI_SRC_VU_GAP)
/* Equalising the frame freed seven pixels; they go into the space around the
 * progress bar, which was pressed against the rule above it and the times
 * below. */
#define UI_SRC_PROGRESS_Y (UI_SRC_RULE_BOTTOM + 8)

/* The text block must clear the tile above it whichever way the two are
 * arranged - side by side it always does, stacked it is the thing to check. */
_Static_assert(UI_SRC_ROW_TITLE >= UI_SRC_ART_Y + UI_SRC_ART_SIZE ||
                   UI_SRC_TEXT_X >= UI_SRC_ART_X + UI_SRC_ART_SIZE,
               "the player's text runs over its cover art");
/* And so must the stream readings, which are beside the tile on one panel and
 * on the other side of it on the other. */
_Static_assert(UI_SRC_STREAM_X >= UI_SRC_ART_X + UI_SRC_ART_SIZE ||
                   UI_SRC_STREAM_X + UI_SRC_STREAM_W <= UI_SRC_ART_X ||
                   UI_SRC_STREAM_Y >= UI_SRC_ART_Y + UI_SRC_ART_SIZE,
               "the player's stream readings run over its cover art");
/* The strip stands in the footer's left margin, where the reading's text
 * stands, and must stop short of the like mark that follows it. */
_Static_assert(UI_CONTENT_X + UI_BUFFER_GRAPH_W <= UI_SRC_LIKE_X,
               "the buffer graph runs into the like mark");
_Static_assert(UI_SRC_BUFFER_GRAPH_Y + UI_SRC_BUFFER_GRAPH_H <= TFT_HEIGHT,
               "the buffer graph runs off the bottom of the panel");
/* Three digits of volume, and the right margin every other block keeps. */
_Static_assert(UI_SRC_VOLUME_TEXT_X + 26 <= TFT_WIDTH,
               "the player's volume reading runs off the panel");
_Static_assert(UI_SRC_FOOT_Y + UI_SRC_LINE_H <= TFT_HEIGHT,
               "the player's footer runs off the bottom of the panel");
/* The cover is drawn as a square tile and everything below it runs the full
 * width, so the tile has to finish above the first of those. Nothing checked
 * this while every panel used the same 96 px square; the moment the size
 * became the shape's to choose, a tile grown one step too far would have been
 * a rule drawn across the album art. */
_Static_assert(UI_SRC_ART_Y + UI_SRC_ART_SIZE <= UI_SRC_RULE_TOP,
               "the player's cover art runs into the rule below it");
/* The rows a shape file places have to stay in order and clear the footer;
 * a transposed pair reads as a plausible layout right up to the screen. */
_Static_assert(UI_SRC_ROW_TITLE <= UI_SRC_ROW_TRACK &&
                   UI_SRC_ROW_TRACK < UI_SRC_ROW_ARTIST &&
                   UI_SRC_ROW_ARTIST < UI_SRC_RULE_TOP &&
                   UI_SRC_RULE_TOP < UI_SRC_VU_Y,
               "the player's rows are out of order");
_Static_assert(UI_SRC_RULE_BOTTOM < UI_SRC_FOOT_Y,
               "the player's footer collides with the meter above it");
