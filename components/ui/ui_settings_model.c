#include "ui_settings_model.h"

#include <limits.h>
#include <stdint.h>

static const ui_settings_row_t s_group_rows[UI_SETTINGS_GROUP_COUNT] = {
    [UI_SETTINGS_GROUP_LANGUAGE] = {
        .id = UI_SETTINGS_ROW_LANGUAGE_GROUP,
        .group = UI_SETTINGS_GROUP_LANGUAGE,
        .kind = UI_SETTINGS_ROW_GROUP,
    },
    [UI_SETTINGS_GROUP_GENERAL] = {
        .id = UI_SETTINGS_ROW_GENERAL_GROUP,
        .group = UI_SETTINGS_GROUP_GENERAL,
        .kind = UI_SETTINGS_ROW_GROUP,
    },
    [UI_SETTINGS_GROUP_DISPLAY] = {
        .id = UI_SETTINGS_ROW_DISPLAY_GROUP,
        .group = UI_SETTINGS_GROUP_DISPLAY,
        .kind = UI_SETTINGS_ROW_GROUP,
    },
};

static const ui_settings_row_t s_field_rows[] = {
    {
        .id = UI_SETTINGS_ROW_LANGUAGE_FIELD,
        .group = UI_SETTINGS_GROUP_LANGUAGE,
        .kind = UI_SETTINGS_ROW_FIELD,
    },
    {
        .id = UI_SETTINGS_ROW_HOME_SCREEN_FIELD,
        .group = UI_SETTINGS_GROUP_GENERAL,
        .kind = UI_SETTINGS_ROW_FIELD,
    },
    {
        .id = UI_SETTINGS_ROW_AUTOPLAY_FIELD,
        .group = UI_SETTINGS_GROUP_GENERAL,
        .kind = UI_SETTINGS_ROW_FIELD,
    },
#if BOARD_HAS_YANDEX_MUSIC
    {
        .id = UI_SETTINGS_ROW_YANDEX_FIELD,
        .group = UI_SETTINGS_GROUP_GENERAL,
        .kind = UI_SETTINGS_ROW_FIELD,
    },
#endif
    {
        .id = UI_SETTINGS_ROW_BRIGHTNESS_FIELD,
        .group = UI_SETTINGS_GROUP_DISPLAY,
        .kind = UI_SETTINGS_ROW_FIELD,
    },
    {
        .id = UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD,
        .group = UI_SETTINGS_GROUP_DISPLAY,
        .kind = UI_SETTINGS_ROW_FIELD,
    },
    {
        .id = UI_SETTINGS_ROW_FLIP_HORIZONTAL_FIELD,
        .group = UI_SETTINGS_GROUP_DISPLAY,
        .kind = UI_SETTINGS_ROW_FIELD,
    },
};

static size_t field_count(ui_settings_group_t group)
{
    switch (group) {
    case UI_SETTINGS_GROUP_LANGUAGE:
        return 1U;
    case UI_SETTINGS_GROUP_GENERAL:
        return 2U + (size_t)BOARD_HAS_YANDEX_MUSIC;
    case UI_SETTINGS_GROUP_DISPLAY:
        return 3U;
    default:
        return 0U;
    }
}

static size_t row_count_for_group(ui_settings_group_t group, int expanded_group)
{
    return 1U + (expanded_group == (int)group ? field_count(group) : 0U);
}

static size_t total_row_count(int expanded_group)
{
    size_t count = 0U;
    for (ui_settings_group_t group = UI_SETTINGS_GROUP_LANGUAGE;
         group < UI_SETTINGS_GROUP_COUNT; ++group) {
        count += row_count_for_group(group, expanded_group);
    }
    return count;
}

static void movement_bounds(const ui_settings_model_t *model, size_t *first, size_t *last)
{
    *first = 0U;
    *last = total_row_count(model->expanded_group) - 1U;
    if (model->expanded_group < 0) return;
    size_t offset = 0U;
    for (ui_settings_group_t group = UI_SETTINGS_GROUP_LANGUAGE;
         group < UI_SETTINGS_GROUP_COUNT; ++group) {
        if (group == (ui_settings_group_t)model->expanded_group) {
            *first = offset;
            *last = offset + field_count(group);
            return;
        }
        offset += row_count_for_group(group, model->expanded_group);
    }
}

static bool valid_model(const ui_settings_model_t *model)
{
    return model != NULL && model->expanded_group >= -1 &&
           model->expanded_group < (int)UI_SETTINGS_GROUP_COUNT;
}

void ui_settings_model_init(ui_settings_model_t *model)
{
    if (model == NULL) return;
    model->expanded_group = -1;
    model->cursor = 0U;
    model->editing = false;
}

bool ui_settings_row_is_number(ui_settings_row_id_t id)
{
    return id == UI_SETTINGS_ROW_BRIGHTNESS_FIELD;
}

bool ui_settings_model_is_editing(const ui_settings_model_t *model)
{
    return valid_model(model) && model->editing;
}

ui_settings_model_result_t ui_settings_model_toggle_edit(ui_settings_model_t *model)
{
    if (!valid_model(model)) return UI_SETTINGS_MODEL_NO_CHANGE;
    const ui_settings_row_t row = ui_settings_model_row_at(model, model->cursor);
    if (!ui_settings_row_is_number(row.id)) return UI_SETTINGS_MODEL_NO_CHANGE;
    model->editing = !model->editing;
    return UI_SETTINGS_MODEL_CHANGED;
}

int ui_settings_brightness_clamp(int value)
{
    if (value < UI_SETTINGS_BRIGHTNESS_MIN) return UI_SETTINGS_BRIGHTNESS_MIN;
    if (value > UI_SETTINGS_BRIGHTNESS_MAX) return UI_SETTINGS_BRIGHTNESS_MAX;
    return value;
}

int ui_settings_brightness_step(int value, int direction)
{
    if (direction == 0) return ui_settings_brightness_clamp(value);
    const int stepped = ui_settings_brightness_clamp(value) +
                        (direction > 0 ? UI_SETTINGS_BRIGHTNESS_STEP
                                       : -UI_SETTINGS_BRIGHTNESS_STEP);
    return ui_settings_brightness_clamp(stepped);
}

size_t ui_settings_model_row_count(const ui_settings_model_t *model)
{
    return valid_model(model) ? total_row_count(model->expanded_group) : 0U;
}

ui_settings_row_t ui_settings_model_row_at(const ui_settings_model_t *model, size_t index)
{
    const ui_settings_row_t invalid = {
        .id = UI_SETTINGS_ROW_LANGUAGE_GROUP,
        .group = UI_SETTINGS_GROUP_LANGUAGE,
        .kind = UI_SETTINGS_ROW_GROUP,
    };
    if (!valid_model(model) || index >= total_row_count(model->expanded_group)) return invalid;

    size_t offset = 0U;
    for (ui_settings_group_t group = UI_SETTINGS_GROUP_LANGUAGE;
         group < UI_SETTINGS_GROUP_COUNT; ++group) {
        if (index == offset) return s_group_rows[group];
        offset++;
        if (model->expanded_group == (int)group) {
            const size_t count = field_count(group);
            size_t field_offset = 0U;
            for (size_t field = 0U; field < sizeof(s_field_rows) / sizeof(s_field_rows[0]); ++field) {
                if (s_field_rows[field].group != group) continue;
                if (field_offset == index - offset) return s_field_rows[field];
                field_offset++;
            }
            offset += count;
        }
    }
    return invalid;
}

ui_settings_row_id_t ui_settings_model_selected(const ui_settings_model_t *model)
{
    return ui_settings_model_row_at(model, model == NULL ? SIZE_MAX : model->cursor).id;
}

ui_settings_model_result_t ui_settings_model_move(ui_settings_model_t *model, int direction)
{
    if (!valid_model(model) || direction == 0 || direction == INT_MIN) {
        return UI_SETTINGS_MODEL_NO_CHANGE;
    }
    /* The knob belongs to the value while a number is armed. Refused here as
     * well as in the caller: a cursor that crept away would leave the screen
     * editing one row and highlighting another. */
    if (model->editing) return UI_SETTINGS_MODEL_NO_CHANGE;
    const size_t count = total_row_count(model->expanded_group);
    if (count == 0U) return UI_SETTINGS_MODEL_NO_CHANGE;
    size_t first;
    size_t last;
    movement_bounds(model, &first, &last);
    if (direction > 0) {
        if (model->cursor >= last) return UI_SETTINGS_MODEL_NO_CHANGE;
        model->cursor++;
    } else {
        if (model->cursor <= first) return UI_SETTINGS_MODEL_NO_CHANGE;
        model->cursor--;
    }
    return UI_SETTINGS_MODEL_CHANGED;
}

ui_settings_model_result_t ui_settings_model_activate(ui_settings_model_t *model)
{
    if (!valid_model(model) || model->editing) return UI_SETTINGS_MODEL_NO_CHANGE;
    const ui_settings_row_t row = ui_settings_model_row_at(model, model->cursor);
    if (row.kind != UI_SETTINGS_ROW_GROUP) return UI_SETTINGS_MODEL_NO_CHANGE;
    if (model->expanded_group == (int)row.group) {
        model->expanded_group = -1;
    } else {
        model->expanded_group = (int)row.group;
    }
    if (model->cursor >= total_row_count(model->expanded_group)) {
        model->cursor = total_row_count(model->expanded_group) - 1U;
    }
    return UI_SETTINGS_MODEL_CHANGED;
}

bool ui_settings_model_is_expanded(const ui_settings_model_t *model, ui_settings_group_t group)
{
    return valid_model(model) && group < UI_SETTINGS_GROUP_COUNT &&
           model->expanded_group == (int)group;
}
