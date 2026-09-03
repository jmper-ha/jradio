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
        .id = UI_SETTINGS_ROW_SCROLL_FIELD,
        .group = UI_SETTINGS_GROUP_GENERAL,
        .kind = UI_SETTINGS_ROW_FIELD,
    },
    {
        .id = UI_SETTINGS_ROW_BUFFER_FIELD,
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

#define UI_SETTINGS_FIELD_ROW_COUNT (sizeof(s_field_rows) / sizeof(s_field_rows[0]))

/* Not in either table above, because nothing that walks the groups should ever
 * meet it: the band belongs to no group and is drawn nowhere in the list. It
 * is one cursor position past the last row, and that is all it is. */
static const ui_settings_row_t s_band_row = {
    .id = UI_SETTINGS_ROW_ADDRESS_BAND,
    .group = UI_SETTINGS_GROUP_COUNT,
    .kind = UI_SETTINGS_ROW_BAND,
};

/* Which of the rows above this particular screen offers. The build already
 * took its rows out of the table; this is the one that also depends on how the
 * device is running - with no home screen there is nothing for "Главный экран"
 * to choose between, so the row would set a value nobody could ever see. */
static bool field_is_present(const ui_settings_model_t *model, ui_settings_row_id_t id)
{
    return id != UI_SETTINGS_ROW_HOME_SCREEN_FIELD || model->home_screen;
}

/* Counted from the table rather than written down as a number: a row added or
 * built out has to change the count, and a second place to say so is a second
 * place to forget. */
static size_t field_count(const ui_settings_model_t *model, ui_settings_group_t group)
{
    size_t count = 0U;
    for (size_t field = 0U; field < UI_SETTINGS_FIELD_ROW_COUNT; ++field) {
        if (s_field_rows[field].group != group) continue;
        if (!field_is_present(model, s_field_rows[field].id)) continue;
        count++;
    }
    return count;
}

static size_t row_count_for_group(const ui_settings_model_t *model, ui_settings_group_t group)
{
    return 1U + (model->expanded_group == (int)group ? field_count(model, group) : 0U);
}

static size_t total_row_count(const ui_settings_model_t *model)
{
    size_t count = 0U;
    for (ui_settings_group_t group = UI_SETTINGS_GROUP_LANGUAGE;
         group < UI_SETTINGS_GROUP_COUNT; ++group) {
        count += row_count_for_group(model, group);
    }
    return count;
}

static void movement_bounds(const ui_settings_model_t *model, size_t *first, size_t *last)
{
    *first = 0U;
    /* One past the last row rather than the last row: that position is the
     * address band. Only with every group collapsed - the expanded case below
     * pens the cursor into its group, and the band is not in one. */
    *last = total_row_count(model);
    if (model->expanded_group < 0) return;
    size_t offset = 0U;
    for (ui_settings_group_t group = UI_SETTINGS_GROUP_LANGUAGE;
         group < UI_SETTINGS_GROUP_COUNT; ++group) {
        if (group == (ui_settings_group_t)model->expanded_group) {
            *first = offset;
            *last = offset + field_count(model, group);
            return;
        }
        offset += row_count_for_group(model, group);
    }
}

static bool valid_model(const ui_settings_model_t *model)
{
    return model != NULL && model->expanded_group >= -1 &&
           model->expanded_group < (int)UI_SETTINGS_GROUP_COUNT;
}

void ui_settings_model_init(ui_settings_model_t *model, bool home_screen)
{
    if (model == NULL) return;
    model->expanded_group = -1;
    model->cursor = 0U;
    model->window_top = 0U;
    model->editing = false;
    /* Fixed for as long as the screen is open, not re-read per frame: the
     * Yandex switch on this very screen can take the last optional source
     * away, and a row set that changed under the cursor mid-edit would move
     * everything below it. The next visit picks the new answer up. */
    model->home_screen = home_screen;
}

size_t ui_settings_model_window_top(ui_settings_model_t *model, size_t visible_count)
{
    if (!valid_model(model) || visible_count == 0U) return 0U;
    const size_t count = total_row_count(model);
    /* Clamped first: expanding a group above the window, or collapsing one,
     * changes the row count under a window that was valid a moment ago. */
    size_t top = model->window_top;
    /* The band is below the list and always on screen, so a cursor that has
     * reached it must leave the rows where they are rather than scroll to
     * reveal a row that does not exist. */
    const size_t cursor = model->cursor < count ? model->cursor : count - 1U;
    if (count <= visible_count) {
        top = 0U;
    } else {
        if (top > count - visible_count) top = count - visible_count;
        if (cursor < top) top = cursor;
        if (cursor >= top + visible_count) top = cursor - visible_count + 1U;
    }
    model->window_top = top;
    return top;
}

bool ui_settings_model_has_rows_above(const ui_settings_model_t *model)
{
    return valid_model(model) && model->window_top > 0U;
}

bool ui_settings_model_has_rows_below(const ui_settings_model_t *model, size_t visible_count)
{
    if (!valid_model(model) || visible_count == 0U) return false;
    return model->window_top + visible_count < total_row_count(model);
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
    return valid_model(model) ? total_row_count(model) : 0U;
}

ui_settings_row_t ui_settings_model_row_at(const ui_settings_model_t *model, size_t index)
{
    const ui_settings_row_t invalid = {
        .id = UI_SETTINGS_ROW_LANGUAGE_GROUP,
        .group = UI_SETTINGS_GROUP_LANGUAGE,
        .kind = UI_SETTINGS_ROW_GROUP,
    };
    if (!valid_model(model)) return invalid;
    if (index == total_row_count(model)) return s_band_row;
    if (index > total_row_count(model)) return invalid;

    size_t offset = 0U;
    for (ui_settings_group_t group = UI_SETTINGS_GROUP_LANGUAGE;
         group < UI_SETTINGS_GROUP_COUNT; ++group) {
        if (index == offset) return s_group_rows[group];
        offset++;
        if (model->expanded_group == (int)group) {
            const size_t count = field_count(model, group);
            size_t field_offset = 0U;
            for (size_t field = 0U; field < UI_SETTINGS_FIELD_ROW_COUNT; ++field) {
                if (s_field_rows[field].group != group) continue;
                if (!field_is_present(model, s_field_rows[field].id)) continue;
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
    const size_t count = total_row_count(model);
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
    if (model->cursor >= total_row_count(model)) {
        model->cursor = total_row_count(model) - 1U;
    }
    return UI_SETTINGS_MODEL_CHANGED;
}

bool ui_settings_model_is_expanded(const ui_settings_model_t *model, ui_settings_group_t group)
{
    return valid_model(model) && group < UI_SETTINGS_GROUP_COUNT &&
           model->expanded_group == (int)group;
}
