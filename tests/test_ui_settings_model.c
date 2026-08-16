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
    assert(ui_settings_model_row_count(&model) == 4U);
    assert(ui_settings_model_row_at(&model, 2U).id == UI_SETTINGS_ROW_HOME_SCREEN_FIELD);

    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_CHANGED);
    assert(ui_settings_model_selected(&model) == UI_SETTINGS_ROW_HOME_SCREEN_FIELD);
    assert(ui_settings_model_move(&model, 1) == UI_SETTINGS_MODEL_NO_CHANGE);
}

int main(void)
{
    test_collapsed_groups_and_cursor();
    test_expand_limits_cursor_to_group();
    test_each_group_has_expected_fields();
    puts("ui_settings_model tests passed");
    return 0;
}
