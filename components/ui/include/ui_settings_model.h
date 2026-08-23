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
    UI_SETTINGS_ROW_BRIGHTNESS_FIELD,
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
    /* Armed by a click on a number field: the encoder then changes the value
     * instead of moving the cursor. One knob does both jobs, so which job it
     * is doing has to be a state, and one the screen can show. */
    bool editing;
} ui_settings_model_t;

/* Brightness runs 10..90 rather than 0..100: the panel is unreadable below
 * about 10, and 0 looks like the device died. The step is what one detent
 * changes - fine enough to settle on a level, coarse enough to cross the range
 * without grinding the knob. */
#define UI_SETTINGS_BRIGHTNESS_MIN 10
#define UI_SETTINGS_BRIGHTNESS_MAX 90
#define UI_SETTINGS_BRIGHTNESS_STEP 5

void ui_settings_model_init(ui_settings_model_t *model);
ui_settings_model_result_t ui_settings_model_move(ui_settings_model_t *model, int direction);
ui_settings_model_result_t ui_settings_model_activate(ui_settings_model_t *model);
ui_settings_row_id_t ui_settings_model_selected(const ui_settings_model_t *model);
size_t ui_settings_model_row_count(const ui_settings_model_t *model);
ui_settings_row_t ui_settings_model_row_at(const ui_settings_model_t *model, size_t index);
bool ui_settings_model_is_expanded(const ui_settings_model_t *model, ui_settings_group_t group);

/* True for the fields the encoder edits in place instead of toggling. */
bool ui_settings_row_is_number(ui_settings_row_id_t id);
bool ui_settings_model_is_editing(const ui_settings_model_t *model);
/* Arms or disarms editing of the field under the cursor. NO_CHANGE on a row
 * that holds no number, which is what keeps the click a toggle everywhere
 * else. */
ui_settings_model_result_t ui_settings_model_toggle_edit(ui_settings_model_t *model);

/* One detent, clamped to the range. Separate from the model because the value
 * lives in settings.csv, not here: the screen owns how it is reached, the
 * settings own what it is. */
int ui_settings_brightness_step(int value, int direction);
int ui_settings_brightness_clamp(int value);
