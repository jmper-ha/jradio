#include "ui_remote_keys.h"

ui_remote_step_t ui_remote_keys_translate(board_input_action_t action, ui_remote_screen_t screen)
{
    ui_remote_step_t step = {.action = action, .open_list = false};
    const bool player = screen == UI_REMOTE_SCREEN_PLAYER;
    switch (action) {
    case BOARD_INPUT_ACTION_REMOTE_UP:
    case BOARD_INPUT_ACTION_REMOTE_DOWN:
        step.action = action == BOARD_INPUT_ACTION_REMOTE_UP ? BOARD_INPUT_ACTION_ENCODER_LEFT
                                                             : BOARD_INPUT_ACTION_ENCODER_RIGHT;
        step.open_list = player;
        break;
    case BOARD_INPUT_ACTION_REMOTE_PREV:
        step.action = player ? BOARD_INPUT_ACTION_BTN_PREV : BOARD_INPUT_ACTION_ENCODER_LEFT;
        break;
    case BOARD_INPUT_ACTION_REMOTE_NEXT:
        step.action = player ? BOARD_INPUT_ACTION_BTN_NEXT : BOARD_INPUT_ACTION_ENCODER_RIGHT;
        break;
    case BOARD_INPUT_ACTION_REMOTE_OK:
        // On the player the list is the whole of what OK does.
        step.action = player ? BOARD_INPUT_ACTION_NONE : BOARD_INPUT_ACTION_ENCODER_BUTTON;
        step.open_list = player;
        break;
    case BOARD_INPUT_ACTION_PLAY_PAUSE:
        if (screen == UI_REMOTE_SCREEN_CHOOSER) step.action = BOARD_INPUT_ACTION_ENCODER_BUTTON;
        break;
    default:
        break;
    }
    return step;
}
