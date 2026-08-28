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
    test_the_windows_hold_together();
    test_the_strip_slots_stay_in_their_lanes();
    test_the_player_stacks_downwards();
    puts("ui_layout tests passed");
    return 0;
}
