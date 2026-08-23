#include "ui_settings_model.h"

#include <assert.h>
#include <stdio.h>

static void test_collapsed_groups_and_cursor(void)
{
    ui_settings_model_t model;
    ui_settings_model_init(&model);
    assert(ui_settings_model_row_count(&model) == 3U);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_LANGUAGE_GROUP);

    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_GENERAL_GROUP);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_DISPLAY_GROUP);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_NO_CHANGE);
}

static void test_expand_limits_cursor_to_group(void)
{
    ui_settings_model_t model;
    ui_settings_model_init(&model);
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
    ui_settings_model_init(&model);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_activate(&model) == UI_SETTINGS_MODEL_CHANGED);
    /* General holds home screen and autoplay, plus the Yandex Music switch in
     * a build that has the feature - the one row here that a board option can
     * take away. */
    assert(ui_settings_model_row_count(&model) == 5U + BOARD_HAS_YANDEX_MUSIC);
    assert(ui_settings_model_row_at(&model, 2U).id == UI_SETTINGS_ROW_HOME_SCREEN_FIELD);
    assert(ui_settings_model_row_at(&model, 3U).id == UI_SETTINGS_ROW_AUTOPLAY_FIELD);

    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_HOME_SCREEN_FIELD);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_AUTOPLAY_FIELD);
#if BOARD_HAS_YANDEX_MUSIC
    assert(ui_settings_model_row_at(&model, 4U).id == UI_SETTINGS_ROW_YANDEX_FIELD);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_YANDEX_FIELD);
#endif
    /* And the cursor still cannot walk out of the expanded group. */
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_NO_CHANGE);
}

static void test_the_display_group_holds_a_number_the_knob_edits(void)
{
    ui_settings_model_t model;
    ui_settings_model_init(&model);
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

int main(void)
{
    test_collapsed_groups_and_cursor();
    test_expand_limits_cursor_to_group();
    test_each_group_has_expected_fields();
    test_the_display_group_holds_a_number_the_knob_edits();
    test_brightness_steps_and_stops_at_the_ends();
    puts("ui_settings_model tests passed");
    return 0;
}
