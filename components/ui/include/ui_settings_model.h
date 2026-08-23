#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "board_features.h"

typedef enum {
    UI_SETTINGS_GROUP_LANGUAGE = 0,
    UI_SETTINGS_GROUP_GENERAL,
    UI_SETTINGS_GROUP_DISPLAY,
    UI_SETTINGS_GROUP_COUNT,
} ui_settings_group_t;

typedef enum {
    UI_SETTINGS_ROW_LANGUAGE_GROUP = 0,
    UI_SETTINGS_ROW_LANGUAGE_FIELD,
    UI_SETTINGS_ROW_GENERAL_GROUP,
    UI_SETTINGS_ROW_HOME_SCREEN_FIELD,
    UI_SETTINGS_ROW_AUTOPLAY_FIELD,
    /* Present in the enum whether or not the feature is built, so the row ids
     * do not shift with a build option; the model simply never hands it out.
     * A build without Yandex Music has no switch for it in General. */
    UI_SETTINGS_ROW_YANDEX_FIELD,
    UI_SETTINGS_ROW_DISPLAY_GROUP,
    UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD,
    UI_SETTINGS_ROW_FLIP_HORIZONTAL_FIELD,
} ui_settings_row_id_t;

typedef enum {
    UI_SETTINGS_ROW_GROUP = 0,
    UI_SETTINGS_ROW_FIELD,
} ui_settings_row_kind_t;

typedef struct {
    ui_settings_row_id_t id;
    ui_settings_group_t group;
    ui_settings_row_kind_t kind;
} ui_settings_row_t;

typedef enum {
    UI_SETTINGS_MODEL_NO_CHANGE = 0,
    UI_SETTINGS_MODEL_CHANGED,
} ui_settings_model_result_t;

typedef struct {
    int expanded_group;
    size_t cursor;
} ui_settings_model_t;

void ui_settings_model_init(ui_settings_model_t *model);
ui_settings_model_result_t ui_settings_model_move(ui_settings_model_t *model, int direction);
ui_settings_model_result_t ui_settings_model_activate(ui_settings_model_t *model);
ui_settings_row_id_t ui_settings_model_selected(const ui_settings_model_t *model);
size_t ui_settings_model_row_count(const ui_settings_model_t *model);
ui_settings_row_t ui_settings_model_row_at(const ui_settings_model_t *model, size_t index);
bool ui_settings_model_is_expanded(const ui_settings_model_t *model, ui_settings_group_t group);
