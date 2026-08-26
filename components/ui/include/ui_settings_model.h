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
    UI_SETTINGS_ROW_SCROLL_FIELD,
    UI_SETTINGS_ROW_AUTOPLAY_FIELD,
    /* Present in the enum whether or not the feature is built, so the row ids
     * do not shift with a build option; the model simply never hands it out.
     * A build without Yandex Music has no switch for it in General. */
    UI_SETTINGS_ROW_YANDEX_FIELD,
    UI_SETTINGS_ROW_DISPLAY_GROUP,
    UI_SETTINGS_ROW_BRIGHTNESS_FIELD,
    UI_SETTINGS_ROW_FLIP_VERTICAL_FIELD,
    UI_SETTINGS_ROW_FLIP_HORIZONTAL_FIELD,
    /* The band along the bottom of the screen, which shows the address of the
     * web UI. It is not a setting and it is not in the list - it is drawn
     * where it always is - but it is the last thing the cursor reaches, so it
     * is a row here. Reachable only with every group collapsed, like the
     * headings it follows. */
    UI_SETTINGS_ROW_ADDRESS_BAND,
} ui_settings_row_id_t;

typedef enum {
    UI_SETTINGS_ROW_GROUP = 0,
    UI_SETTINGS_ROW_FIELD,
    UI_SETTINGS_ROW_BAND,
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
    /* First row drawn, so the list can be longer than the screen. Adjusted by
     * ui_settings_model_window_top() rather than set directly. */
    size_t window_top;
    /* Armed by a click on a number field: the encoder then changes the value
     * instead of moving the cursor. One knob does both jobs, so which job it
     * is doing has to be a state, and one the screen can show. */
    bool editing;
    /* Whether this device has a home screen at all, taken at init. Decides
     * one row: the choice between the list and the carousel means nothing on
     * a device that shows neither. */
    bool home_screen;
} ui_settings_model_t;

/* Brightness runs 10..90 rather than 0..100: the panel is unreadable below
 * about 10, and 0 looks like the device died. The step is what one detent
 * changes - fine enough to settle on a level, coarse enough to cross the range
 * without grinding the knob. */
#define UI_SETTINGS_BRIGHTNESS_MIN 10
#define UI_SETTINGS_BRIGHTNESS_MAX 90
#define UI_SETTINGS_BRIGHTNESS_STEP 5

/* `home_screen` is what ui_menu_home_screen_needed() says for the device as it
 * is running right now - see the field it sets. */
void ui_settings_model_init(ui_settings_model_t *model, bool home_screen);
ui_settings_model_result_t ui_settings_model_move(ui_settings_model_t *model, int direction);
ui_settings_model_result_t ui_settings_model_activate(ui_settings_model_t *model);
ui_settings_row_id_t ui_settings_model_selected(const ui_settings_model_t *model);
/* How many rows the list holds - what the screen draws. The address band is
 * one past that: it is a cursor position, not a row to draw, so counting it
 * here would scroll the list to make room for something already on screen. */
size_t ui_settings_model_row_count(const ui_settings_model_t *model);
/* Answers the band for `index == ui_settings_model_row_count()`, which is how
 * the cursor's position past the end of the list is read back. */
ui_settings_row_t ui_settings_model_row_at(const ui_settings_model_t *model, size_t index);
bool ui_settings_model_is_expanded(const ui_settings_model_t *model, ui_settings_group_t group);

/* True for the fields the encoder edits in place instead of toggling. */
bool ui_settings_row_is_number(ui_settings_row_id_t id);
bool ui_settings_model_is_editing(const ui_settings_model_t *model);
/* Arms or disarms editing of the field under the cursor. NO_CHANGE on a row
 * that holds no number, which is what keeps the click a toggle everywhere
 * else. */
ui_settings_model_result_t ui_settings_model_toggle_edit(ui_settings_model_t *model);

/* First model row to draw, given how many rows fit. Moves as little as it can:
 * the window follows the cursor off either edge and otherwise stays put, so
 * the list does not jump under a cursor that is already visible. Takes a
 * mutable model because scrolling is a change to remember, not a view derived
 * fresh each frame. */
size_t ui_settings_model_window_top(ui_settings_model_t *model, size_t visible_count);
/* True when there are rows above or below the window - what the screen needs
 * to say the list continues. */
bool ui_settings_model_has_rows_above(const ui_settings_model_t *model);
bool ui_settings_model_has_rows_below(const ui_settings_model_t *model, size_t visible_count);

/* One detent, clamped to the range. Separate from the model because the value
 * lives in settings.csv, not here: the screen owns how it is reached, the
 * settings own what it is. */
int ui_settings_brightness_step(int value, int direction);
int ui_settings_brightness_clamp(int value);
