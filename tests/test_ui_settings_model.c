#include "ui_settings_model.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/* Every model here is built with a home screen, which is the ordinary device;
 * the one without is what test_a_device_with_no_home_screen_drops_that_row()
 * covers. */

static void test_collapsed_groups_and_cursor(void)
{
    ui_settings_model_t model;
    ui_settings_model_init(&model, true);
    assert(ui_settings_model_row_count(&model) == 3U);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_LANGUAGE_GROUP);

    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_GENERAL_GROUP);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_DISPLAY_GROUP);
    /* One more stop past the last heading: the address band along the bottom.
     * It is where the cursor ends up, and only then does the screen stop. */
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_ADDRESS_BAND);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_NO_CHANGE);
    assert(ui_settings_model_move(&model, -1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_DISPLAY_GROUP);
}

static void test_the_address_band_is_a_stop_not_a_row(void)
{
    ui_settings_model_t model;
    ui_settings_model_init(&model, true);
    const size_t count = ui_settings_model_row_count(&model);
    /* The band is not counted among the rows the screen draws: it is painted
     * along the bottom whatever the list is doing, and counting it would make
     * the window scroll to reveal something already on screen. */
    assert(count == 3U);
    const ui_settings_row_t band = ui_settings_model_row_at(&model, count);
    assert(band.id == UI_SETTINGS_ROW_ADDRESS_BAND);
    assert(band.kind == UI_SETTINGS_ROW_BAND);
    /* And it holds no number, so a click there is never the knob being armed. */
    assert(!ui_settings_row_is_number(UI_SETTINGS_ROW_ADDRESS_BAND));

    model.cursor = count;
    assert(ui_settings_model_toggle_edit(&model) == UI_SETTINGS_MODEL_NO_CHANGE);
    assert(!ui_settings_model_is_editing(&model));
    /* Nor is it a group to open. */
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_NO_CHANGE);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_ADDRESS_BAND);

    /* With a group open the cursor stays in it, so the band cannot be reached
     * from inside one - the same rule the headings follow. */
    ui_settings_model_init(&model, true);
    (void)ui_settings_model_move(&model, 1);
    (void)ui_settings_model_move(&model, 1);
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_CHANGED);
    while (ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED) {
        assert(ui_settings_model_selected(&model) != UI_SETTINGS_ROW_ADDRESS_BAND);
    }
}

static void test_the_band_does_not_scroll_the_list(void)
{
    /* The list is short enough to fit today, but the window is what would move
     * if the band were ever counted as a row - so the check is written against
     * a window too small for the list rather than against today's numbers. */
    const size_t visible = 2U;
    ui_settings_model_t model;
    ui_settings_model_init(&model, true);
    const size_t count = ui_settings_model_row_count(&model);
    model.cursor = count - 1U;
    const size_t top = ui_settings_model_window_top(&model, visible);
    assert(top == count - visible);
    /* Onto the band: the rows stay exactly where the last row left them. */
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_ADDRESS_BAND);
    assert(ui_settings_model_window_top(&model, visible) == top);
    assert(!ui_settings_model_has_rows_below(&model, visible));
}

static void test_expand_limits_cursor_to_group(void)
{
    ui_settings_model_t model;
    ui_settings_model_init(&model, true);
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_row_count(&model) == 4U);
    assert(ui_settings_model_row_at(&model, 0U).id == UI_SETTINGS_ROW_LANGUAGE_GROUP);
    assert(ui_settings_model_row_at(&model, 1U).id == UI_SETTINGS_ROW_LANGUAGE_FIELD);
    assert(ui_settings_model_row_at(&model, 1U).kind == UI_SETTINGS_ROW_FIELD);

    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_LANGUAGE_FIELD);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_NO_CHANGE);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_LANGUAGE_FIELD);
    assert(ui_settings_model_move(&model, -1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_LANGUAGE_GROUP);
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_row_count(&model) == 3U);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_LANGUAGE_GROUP);
}

static void test_each_group_has_expected_fields(void)
{
    ui_settings_model_t model;
    ui_settings_model_init(&model, true);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_CHANGED);
    /* General holds home screen, scrolling and autoplay, plus the Yandex Music
     * switch in a build that has the feature - the one row here that a board
     * option can take away. */
    assert(ui_settings_model_row_count(&model) == 6U + BOARD_HAS_YANDEX_MUSIC);
    assert(ui_settings_model_row_at(&model, 2U).id == UI_SETTINGS_ROW_HOME_SCREEN_FIELD);
    assert(ui_settings_model_row_at(&model, 3U).id == UI_SETTINGS_ROW_SCROLL_FIELD);
    assert(ui_settings_model_row_at(&model, 4U).id == UI_SETTINGS_ROW_AUTOPLAY_FIELD);

    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_HOME_SCREEN_FIELD);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_SCROLL_FIELD);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_AUTOPLAY_FIELD);
#if BOARD_HAS_YANDEX_MUSIC
    assert(ui_settings_model_row_at(&model, 5U).id == UI_SETTINGS_ROW_YANDEX_FIELD);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_YANDEX_FIELD);
#endif
    /* And the cursor still cannot walk out of the expanded group. */
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_NO_CHANGE);
}

static void test_the_display_group_holds_a_number_the_knob_edits(void)
{
    ui_settings_model_t model;
    ui_settings_model_init(&model, true);
    assert(!ui_settings_model_is_editing(&model));
    /* Down to Display and open it: brightness first, then the two flips. */
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_DISPLAY_GROUP);
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_row_count(&model) == 6U);
    assert(ui_settings_model_row_at(&model, 3U).id == UI_SETTINGS_ROW_BRIGHTNESS_FIELD);
    assert(ui_settings_model_row_at(&model, 4U).id == UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD);

    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_BRIGHTNESS_FIELD);
    assert(ui_settings_row_is_number(UI_SETTINGS_ROW_BRIGHTNESS_FIELD));
    assert(!ui_settings_row_is_number(UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD));

    /* Armed, the knob stops moving the cursor - otherwise the screen would be
     * editing one row while highlighting another. */
    assert(ui_settings_model_toggle_edit(&model) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_is_editing(&model));
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_NO_CHANGE);
    assert(ui_settings_model_move(&model, -1) == UI_SETTINGS_MODEL_NO_CHANGE);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_BRIGHTNESS_FIELD);
    /* And a group cannot be collapsed out from under the value being edited. */
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_NO_CHANGE);

    assert(ui_settings_model_toggle_edit(&model) == UI_SETTINGS_MODEL_CHANGED);
    assert(!ui_settings_model_is_editing(&model));
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD);
    /* A row with no number never captures the knob, which is what keeps the
     * click a toggle on every other field. */
    assert(ui_settings_model_toggle_edit(&model) == UI_SETTINGS_MODEL_NO_CHANGE);
    assert(!ui_settings_model_is_editing(&model));
}

static void test_brightness_steps_and_stops_at_the_ends(void)
{
    assert(ui_settings_brightness_step(50, 1) == 50 + UI_SETTINGS_BRIGHTNESS_STEP);
    assert(ui_settings_brightness_step(50, -1) == 50 - UI_SETTINGS_BRIGHTNESS_STEP);
    assert(ui_settings_brightness_step(50, 0) == 50);

    /* The ends hold rather than wrap: a knob that jumps from dimmest to
     * brightest in one detent is a flash in the face at night. */
    assert(ui_settings_brightness_step(UI_SETTINGS_BRIGHTNESS_MAX, 1) ==
           UI_SETTINGS_BRIGHTNESS_MAX);
    assert(ui_settings_brightness_step(UI_SETTINGS_BRIGHTNESS_MIN, -1) ==
           UI_SETTINGS_BRIGHTNESS_MIN);

    /* A value from outside the range - an older settings.csv, or one edited by
     * hand - is pulled in rather than stepped from where it was. */
    assert(ui_settings_brightness_clamp(0) == UI_SETTINGS_BRIGHTNESS_MIN);
    assert(ui_settings_brightness_clamp(100) == UI_SETTINGS_BRIGHTNESS_MAX);
    assert(ui_settings_brightness_step(100, 1) == UI_SETTINGS_BRIGHTNESS_MAX);
    assert(ui_settings_brightness_step(1, -1) == UI_SETTINGS_BRIGHTNESS_MIN);

    /* Every step lands inside the range, from every start. */
    for (int value = -20; value <= 120; ++value) {
        for (int direction = -1; direction <= 1; ++direction) {
            const int next = ui_settings_brightness_step(value, direction);
            assert(next >= UI_SETTINGS_BRIGHTNESS_MIN && next <= UI_SETTINGS_BRIGHTNESS_MAX);
        }
    }
}

static void test_the_window_follows_the_cursor_and_otherwise_holds_still(void)
{
    /* The list outgrew the screen, and before the window existed the rows past
     * the fifth were drawn underneath an opaque label - present, invisible,
     * and unreachable. */
    /* Deliberately one short of what the screen now shows: the point of the
     * window is the case where the list does not fit, and the real screen has
     * grown out of it for the moment. */
    const size_t visible = 5U;
    ui_settings_model_t model;
    ui_settings_model_init(&model, true);

    /* Collapsed, everything fits: no window movement and nothing to indicate. */
    assert(ui_settings_model_row_count(&model) <= visible);
    assert(ui_settings_model_window_top(&model, visible) == 0U);
    assert(!ui_settings_model_has_rows_above(&model));
    assert(!ui_settings_model_has_rows_below(&model, visible));

    /* Open Display, the group that overflows: 3 headings + 3 fields. */
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_CHANGED);
    const size_t count = ui_settings_model_row_count(&model);
    assert(count == 6U);
    /* The cursor is still on the heading at row 2, which is on screen, so the
     * window has not moved. */
    assert(ui_settings_model_window_top(&model, visible) == 0U);
    assert(ui_settings_model_has_rows_below(&model, visible));
    assert(!ui_settings_model_has_rows_above(&model));

    /* The cursor sits on the Display heading at row 2, so the two fields under
     * it are still on screen and the window holds. */
    for (size_t step = 0U; step < 2U; ++step) {
        assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
        assert(ui_settings_model_window_top(&model, visible) == 0U);
    }
    /* The last field is the one that does not fit, and it pulls the window by
     * exactly one row - enough to show the cursor, and no more. */
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(model.cursor == count - 1U);
    assert(ui_settings_model_window_top(&model, visible) == count - visible);
    assert(ui_settings_model_has_rows_above(&model));
    assert(!ui_settings_model_has_rows_below(&model, visible));

    /* Coming back up, the window holds rather than snapping back: every row
     * of the open group is still visible from where it is, and a list that
     * scrolled on each step would move under a cursor that never left the
     * screen. */
    while (ui_settings_model_move(&model, -1) == UI_SETTINGS_MODEL_CHANGED) {
        assert(ui_settings_model_window_top(&model, visible) == count - visible);
    }
    /* The cursor is back on the Display heading, which is the top of what the
     * group can reach - so this is the whole group seen without the window
     * moving again. */
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_DISPLAY_GROUP);

    /* Collapsing the group shrinks the list under a scrolled window: it has to
     * come back rather than leave the screen showing blank rows. */
    model.window_top = 1U;
    while (ui_settings_model_move(&model, -1) == UI_SETTINGS_MODEL_CHANGED) { }
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_window_top(&model, visible) == 0U);

    /* Every reachable cursor position is inside its own window - the property
     * the whole thing exists for. */
    ui_settings_model_init(&model, true);
    for (ui_settings_group_t group = UI_SETTINGS_GROUP_LANGUAGE;
         group < UI_SETTINGS_GROUP_COUNT; ++group) {
        ui_settings_model_init(&model, true);
        while (ui_settings_model_row_at(&model, model.cursor).group != group ||
               ui_settings_model_row_at(&model, model.cursor).kind != UI_SETTINGS_ROW_GROUP) {
            if (ui_settings_model_move(&model, 1) != UI_SETTINGS_MODEL_CHANGED) break;
        }
        (void)ui_settings_model_activate(&model);
        do {
            const size_t top = ui_settings_model_window_top(&model, visible);
            assert(model.cursor >= top && model.cursor < top + visible);
        } while (ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    }
}

static void test_a_device_with_no_home_screen_drops_that_row(void)
{
    /* With only the radio and Settings to switch between, the choice between
     * the list home screen and the carousel picks between two screens the
     * device never shows. The row is not disabled but absent: a setting that
     * changes nothing is worse than no setting, because it looks like it
     * should. */
    ui_settings_model_t model;
    ui_settings_model_init(&model, false);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_GENERAL_GROUP);
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_CHANGED);

    ui_settings_model_t with_home;
    ui_settings_model_init(&with_home, true);
    (void)ui_settings_model_move(&with_home, 1);
    (void)ui_settings_model_activate(&with_home);
    assert(ui_settings_model_row_count(&model) ==
           ui_settings_model_row_count(&with_home) - 1U);

    /* Scrolling is what the row above it becomes, so nothing below shifts by
     * more than the one row that went. */
    assert(ui_settings_model_row_at(&model, 2U).id == UI_SETTINGS_ROW_SCROLL_FIELD);
    assert(ui_settings_model_row_at(&model, 3U).id == UI_SETTINGS_ROW_AUTOPLAY_FIELD);

    /* And the cursor cannot reach it: walking the whole group never lands on
     * a row the model does not hand out. */
    do {
        assert(ui_settings_model_selected(&model) != UI_SETTINGS_ROW_HOME_SCREEN_FIELD);
    } while (ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
}

static void test_the_longest_list_needs_the_window(void)
{
    /* Six rows is what ui.c draws. General is deeper than that now, so the
     * screen scrolls again - which is fine, and is why the window exists, but
     * it is a fact worth failing on rather than discovering on the panel. */
    ui_settings_model_t model;
    size_t longest = 0U;
    for (ui_settings_group_t group = UI_SETTINGS_GROUP_LANGUAGE;
         group < UI_SETTINGS_GROUP_COUNT; ++group) {
        ui_settings_model_init(&model, true);
        model.expanded_group = (int)group;
        const size_t count = ui_settings_model_row_count(&model);
        if (count > longest) longest = count;
    }
    assert(longest == 6U + BOARD_HAS_YANDEX_MUSIC);
    /* Whatever the longest is, every row of it is reachable with the window. */
    ui_settings_model_init(&model, true);
    model.expanded_group = (int)UI_SETTINGS_GROUP_GENERAL;
    for (size_t cursor = 0U; cursor < longest; ++cursor) {
        model.cursor = cursor;
        const size_t top = ui_settings_model_window_top(&model, 6U);
        assert(cursor >= top && cursor < top + 6U);
    }
}

int main(void)
{
    test_collapsed_groups_and_cursor();
    test_the_address_band_is_a_stop_not_a_row();
    test_the_band_does_not_scroll_the_list();
    test_expand_limits_cursor_to_group();
    test_each_group_has_expected_fields();
    test_the_display_group_holds_a_number_the_knob_edits();
    test_brightness_steps_and_stops_at_the_ends();
    test_the_window_follows_the_cursor_and_otherwise_holds_still();
    test_a_device_with_no_home_screen_drops_that_row();
    test_the_longest_list_needs_the_window();
    puts("ui_settings_model tests passed");
    return 0;
}
