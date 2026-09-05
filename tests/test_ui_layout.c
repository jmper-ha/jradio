#include <assert.h>
#include <stdio.h>

#include "board_display_profile.h"
#include "ui_layout.h"

/* The layout is arithmetic over the panel's size, which is why it lives in a
 * header of its own: none of it needs a device to check.
 *
 * Including the header has already done half the work - it carries the
 * _Static_asserts that say nothing runs off an edge or over its neighbour, and
 * those fire at compile time for whichever panel board_options.h selects. What
 * is left for here is the part a static assert cannot state: that the derived
 * counts come out at the numbers that were measured on the two panels this
 * firmware has actually been run on. A formula that quietly gives four list
 * rows instead of five would satisfy every assertion in the header. */

static void test_the_derived_counts_match_the_measured_panels(void)
{
#if TFT_WIDTH == 320 && TFT_HEIGHT == 240
    /* Five rows of 20 px text: at 14 px six fitted, but a name read from
     * across the room did not. */
    assert(UI_STATION_LIST_MAX_ROWS == 5U);
    /* Three headings plus the deepest group, above the web band. */
    assert(UI_SETTINGS_MAX_ROWS == 6U);
    /* 20*11 + 19*3 = 277 px from x=30, ending at 307 on a 320 px panel. */
    assert(UI_VU_SEGMENT_W == 11 && UI_VU_SEGMENT_GAP == 3);
    assert(UI_FEED_SLOTS == 5);
#elif TFT_WIDTH == 240 && TFT_HEIGHT == 320
    /* 80 px more at the same pitch is two more rows, with the rule and the
     * position bar still clearing the bottom edge. */
    assert(UI_STATION_LIST_MAX_ROWS == 7U);
    assert(UI_SETTINGS_MAX_ROWS == 9U);
    assert(UI_VU_SEGMENT_W == 8 && UI_VU_SEGMENT_GAP == 2);
    /* The outer pair of the carousel sits 132 px from the centre, which is off
     * a 240 px panel on both sides. */
    assert(UI_FEED_SLOTS == 3);
#elif TFT_WIDTH == 480 && TFT_HEIGHT == 320
    /* Bigger faces, so more panel does not mean more rows: at a 35 px title
     * line this holds what the 320x240 panel held at 24, and that is the trade
     * layout_480x320.h states. A count that came out at seven here would mean
     * the shape file's faces had been reverted without its comment. */
    assert(UI_STATION_LIST_MAX_ROWS == 5U);
    assert(UI_SETTINGS_MAX_ROWS == 6U);
    /* 20*17 + 20*5 = 440 px from x=30, ending at 465 on a 480 px panel. */
    assert(UI_VU_SEGMENT_W == 17 && UI_VU_SEGMENT_GAP == 5);
    assert(UI_FEED_SLOTS == 5);
#elif TFT_WIDTH == 320 && TFT_HEIGHT == 480
    /* Derived, then seen on the panel 2026-09-05. These are what the
     * arithmetic gives on the numbers layout_320x480.h places, and they are
     * pinned for the same reason as the others: a shape file edited without
     * looking at what it costs is exactly what this file is for. Nine rows and
     * ten settings rows is what the extra 160 px of height buys at the bigger
     * faces - the landscape panel of the same glass gets five and six. */
    assert(UI_STATION_LIST_MAX_ROWS == 9U);
    assert(UI_SETTINGS_MAX_ROWS == 10U);
    /* 20*11 + 19*3 = 277 px from x=30, ending at 307 on a 320 px panel - the
     * same meter the 320x240 panel draws, since it is the same width. */
    assert(UI_VU_SEGMENT_W == 11 && UI_VU_SEGMENT_GAP == 3);
    /* The ring is the landscape ILI9488's, and at 320 px wide its outer pair
     * falls off both edges - which is the whole of what UI_FEED_SLOTS decides. */
    assert(UI_FEED_SLOTS == 3);
#endif
}

static void test_the_layout_measures_in_the_faces_it_declares(void)
{
    /* A row is spaced by the face it carries. Stating it this way rather than
     * as two literals that happen to agree is what lets a shape ask for bigger
     * text without every row pitch having to be found and edited. */
    assert(UI_SRC_LINE_H == UI_FONT_BODY_LINE_H);
    assert(UI_SRC_TRACK_H == UI_FONT_TITLE_LINE_H);
    /* A face has to be one the tree actually carries; a size with no generated
     * source is a link error, but a size with no line height in
     * ui_font_metrics.h is a silent expansion to nothing. */
    assert(UI_FONT_BODY_LINE_H > 0 && UI_FONT_TITLE_LINE_H > 0);
    assert(UI_FONT_TITLE_LINE_H >= UI_FONT_BODY_LINE_H);

#if (TFT_WIDTH == 320 && TFT_HEIGHT == 240) || (TFT_WIDTH == 240 && TFT_HEIGHT == 320)
    /* Both panels so far are laid out for the same four faces, and these are
     * the generated fonts' own line heights. A face regenerated at another
     * size has to come through here - and so does one regenerated at the same
     * size over a wider set of characters, which is how the 20 px face went
     * from 23 to 24: a capital carrying an accent is taller than any letter
     * the face held before. */
    assert(UI_FONT_BODY_PX == 14 && UI_FONT_TITLE_PX == 20);
    assert(UI_FONT_ICON_PX == 24 && UI_FONT_DISPLAY_PX == 48);
    assert(UI_SRC_LINE_H == 19 && UI_SRC_TRACK_H == 24);
    assert(UI_SRC_ART_SIZE == 96);
    assert(UI_LIST_ICON_W == 30);
    assert(UI_LIST_NUMBER_W == 34);
    assert(UI_FEED_ICON_SMALL_PX == 24 && UI_FEED_ICON_MEDIUM_PX == 32 &&
           UI_FEED_ICON_LARGE_PX == 48);
    assert(UI_FEED_TILE == 84);
#elif TFT_WIDTH == 480 && TFT_HEIGHT == 320
    /* The 480x320 panel asks for its own pair, and these are those faces' own
     * line heights. 18 and 20 share a height for the reason ui_font_metrics.h
     * records - the 20 px face covers fewer letters - so a shape that meant to
     * grow the body text from 14 and typed 20 would get no more height at all;
     * pinning the pair here is what catches that. */
    assert(UI_FONT_BODY_PX == 18 && UI_FONT_TITLE_PX == 26);
    /* The icon face steps up with the text; the display face cannot, 48 being
     * the largest Montserrat LVGL carries. Both have to be enabled in
     * sdkconfig - ui_fonts.h turns a missing one into a build error, and this
     * is the reminder of which sizes that means. */
    assert(UI_FONT_ICON_PX == 32 && UI_FONT_DISPLAY_PX == 48);
    assert(UI_SRC_LINE_H == 24 && UI_SRC_TRACK_H == 35);
    /* The tile the layout keeps for the cover is also the square covers are
     * decoded into, so a change here is a change to the picture and not only
     * to its frame. Pinned for the same reason the row counts are. */
    assert(UI_SRC_ART_SIZE == 160);
    /* And the column the mark stands in follows the face it is drawn with. */
    assert(UI_LIST_ICON_W == 38);
    /* 33 px of digits and 11 of gap, against 25 and 9 on the smaller panels. */
    assert(UI_LIST_NUMBER_W == 44);
    assert(UI_SET_CHEVRON_X == 438);
    /* Every row stops eight pixels short of the arrows, whatever it carries. */
    assert(UI_SET_ROW_RIGHT == 430);
    /* The ring is drawn at sizes the bitmap generator has to have emitted. */
    assert(UI_FEED_ICON_SMALL_PX == 32 && UI_FEED_ICON_MEDIUM_PX == 44 &&
           UI_FEED_ICON_LARGE_PX == 64);
    assert(UI_FEED_TILE == 120);
#elif TFT_WIDTH == 320 && TFT_HEIGHT == 480
    /* The same four faces as the landscape panel of the same glass, for the
     * reason layout_320x480.h gives: it is the same pitch, and the share of
     * the screen a face takes is what the eye reads. */
    assert(UI_FONT_BODY_PX == 18 && UI_FONT_TITLE_PX == 26);
    assert(UI_FONT_ICON_PX == 32 && UI_FONT_DISPLAY_PX == 48);
    assert(UI_SRC_LINE_H == 24 && UI_SRC_TRACK_H == 35);
    /* The tile covers are decoded into, which a portrait panel can afford at
     * the landscape figure because it is height it is spending, not width. */
    assert(UI_SRC_ART_SIZE == 160);
    assert(UI_LIST_ICON_W == 38);
    assert(UI_LIST_NUMBER_W == 44);
    /* The chevron column, on a panel 160 px narrower than the landscape one. */
    assert(UI_SET_CHEVRON_X == 278);
    assert(UI_SET_ROW_RIGHT == 270);
    assert(UI_FEED_ICON_SMALL_PX == 32 && UI_FEED_ICON_MEDIUM_PX == 44 &&
           UI_FEED_ICON_LARGE_PX == 64);
    assert(UI_FEED_TILE == 120);
#endif

    /* The note on an empty cover tile is a bitmap sized by the shape, and the
     * whole point of that is that it follows the tile. Two thirds of the
     * square on every panel: the glyph it replaced was 48 px of ink in a 96 px
     * tile, and 0.75 of a 64 px box is that same 48. A shape that grew its
     * tile and left the note behind - which is what the display face forced
     * for a while - would pass every assertion in the header. */
    assert(UI_SRC_ART_NOTE_PX == (UI_SRC_ART_SIZE * 2 + 1) / 3);
}

static void test_the_slow_repaint_threshold_follows_the_panel(void)
{
    /* Above a full repaint on every panel, or the warning fires on an
     * ordinary screen change - which is what it did on the 480x320 panel
     * while it was a constant. */
    assert(UI_REPAINT_WARN_MS > UI_FRAME_WIRE_MS + UI_FRAME_RENDER_MS);
    /* And not so far above it that a pass which never ends looks normal. Ten
     * seconds is the frozen screen this exists to name. */
    assert(UI_REPAINT_WARN_MS < 10000);
#if TFT_WIDTH == 320 && TFT_HEIGHT == 240
    /* The panel the number was measured on, and the reason it is written as a
     * derivation rather than a table: it reproduces what was there. */
    assert(UI_FRAME_WIRE_MS == 61);
    assert(UI_REPAINT_WARN_MS == 400);
#endif
}

static void test_the_windows_hold_together(void)
{
    /* Rows start below the strip and the last one leaves room for what closes
     * the screen. Stated as a relation rather than as coordinates, so it holds
     * for a panel of any height. */
    assert(UI_LIST_ROW_Y >= UI_STRIP_H);
    assert(UI_LIST_RULE_Y > UI_LIST_ROW_Y +
                                (int)(UI_STATION_LIST_MAX_ROWS - 1U) * UI_LIST_ROW_PITCH +
                                UI_LIST_ROW_H - UI_LIST_ROW_PITCH);
    assert(UI_LIST_PROGRESS_Y + UI_SRC_PROGRESS_H <= TFT_HEIGHT);

    /* One more settings row than fits would be invisible rather than clipped:
     * the notice line is opaque and painted after the rows. The count is a
     * division precisely so that cannot happen, and this is that division
     * read back. */
    assert(UI_SET_ROW_Y + (int)(UI_SETTINGS_MAX_ROWS - 1U) * UI_SET_ROW_PITCH +
               UI_SET_ROW_H <=
           UI_SET_BAND_Y - UI_SET_NOTICE_H);
    assert(UI_SET_ROW_Y + (int)UI_SETTINGS_MAX_ROWS * UI_SET_ROW_PITCH + UI_SET_ROW_H >
           UI_SET_BAND_Y - UI_SET_NOTICE_H);

    /* The band is the bottom of the screen and nothing is under it. */
    assert(UI_SET_BAND_Y + UI_SET_BAND_H <= TFT_HEIGHT);
    assert(UI_SET_BAND_ADDRESS_W < UI_CONTENT_W);
}

static void test_the_strip_slots_stay_in_their_lanes(void)
{
    /* Left to right and no overlap, on a panel of any width. */
    assert(UI_STRIP_CONTEXT_X + UI_STRIP_CONTEXT_W <= UI_STRIP_CLOCK_X);
    assert(UI_STRIP_CLOCK_X + UI_STRIP_CLOCK_W <= UI_STRIP_BARS_X);
    assert(UI_STRIP_BARS_X < UI_STRIP_RSSI_X);
    assert(UI_STRIP_RSSI_X < TFT_WIDTH);
    assert(UI_STRIP_CONTEXT_W > 0);
}

static void test_the_player_stacks_downwards(void)
{
    /* The header's static assertions already refuse a transposed pair; these
     * are the two the shape file cannot get wrong on its own, because they
     * involve numbers derived from it. */
    assert(UI_SRC_VU_GAP > 0);
    assert(UI_SRC_RULE_BOTTOM > UI_SRC_VU_BOTTOM);
    assert(UI_SRC_PROGRESS_Y > UI_SRC_RULE_BOTTOM);
    assert(UI_SRC_PROGRESS_Y + UI_SRC_PROGRESS_SEEK_H <= UI_SRC_FOOT_Y);
    /* The cover art is on the panel whichever way the screen is arranged. */
    assert(UI_SRC_ART_X + UI_SRC_ART_SIZE <= TFT_WIDTH);
    assert(UI_SRC_ART_Y + UI_SRC_ART_SIZE <= TFT_HEIGHT);
    /* The volume group runs left to right and ends before the reading. */
    assert(UI_SRC_LIKE_X < UI_SRC_VOLUME_ICON_X);
    assert(UI_SRC_VOLUME_ICON_X < UI_SRC_VOLUME_BAR_X);
    assert(UI_SRC_VOLUME_BAR_X + UI_SRC_VOLUME_BAR_W <= UI_SRC_VOLUME_TEXT_X);
}

int main(void)
{
    test_the_derived_counts_match_the_measured_panels();
    test_the_layout_measures_in_the_faces_it_declares();
    test_the_slow_repaint_threshold_follows_the_panel();
    test_the_windows_hold_together();
    test_the_strip_slots_stay_in_their_lanes();
    test_the_player_stacks_downwards();
    puts("ui_layout tests passed");
    return 0;
}
