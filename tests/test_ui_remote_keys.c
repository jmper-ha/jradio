#include <assert.h>
#include <stdio.h>

#include "ui_remote_keys.h"

static void expect(board_input_action_t action, ui_remote_screen_t screen,
                   board_input_action_t want, bool open_list)
{
    const ui_remote_step_t step = ui_remote_keys_translate(action, screen);
    assert(step.action == want);
    assert(step.open_list == open_list);
}

static void test_on_the_player(void)
{
    const ui_remote_screen_t s = UI_REMOTE_SCREEN_PLAYER;
    // Up and Down open the list and move in it - not the volume any more.
    expect(BOARD_INPUT_ACTION_REMOTE_UP, s, BOARD_INPUT_ACTION_ENCODER_LEFT, true);
    expect(BOARD_INPUT_ACTION_REMOTE_DOWN, s, BOARD_INPUT_ACTION_ENCODER_RIGHT, true);
    // OK opens the list and does nothing else - not a second Play/Pause.
    expect(BOARD_INPUT_ACTION_REMOTE_OK, s, BOARD_INPUT_ACTION_NONE, true);
    // Prev and Next step along it as they always did.
    expect(BOARD_INPUT_ACTION_REMOTE_PREV, s, BOARD_INPUT_ACTION_BTN_PREV, false);
    expect(BOARD_INPUT_ACTION_REMOTE_NEXT, s, BOARD_INPUT_ACTION_BTN_NEXT, false);
    expect(BOARD_INPUT_ACTION_PLAY_PAUSE, s, BOARD_INPUT_ACTION_PLAY_PAUSE, false);
}

static void test_in_a_menu_or_a_list(void)
{
    const ui_remote_screen_t s = UI_REMOTE_SCREEN_CHOOSER;
    expect(BOARD_INPUT_ACTION_REMOTE_UP, s, BOARD_INPUT_ACTION_ENCODER_LEFT, false);
    expect(BOARD_INPUT_ACTION_REMOTE_DOWN, s, BOARD_INPUT_ACTION_ENCODER_RIGHT, false);
    // Prev and Next move the cursor too.
    expect(BOARD_INPUT_ACTION_REMOTE_PREV, s, BOARD_INPUT_ACTION_ENCODER_LEFT, false);
    expect(BOARD_INPUT_ACTION_REMOTE_NEXT, s, BOARD_INPUT_ACTION_ENCODER_RIGHT, false);
    expect(BOARD_INPUT_ACTION_REMOTE_OK, s, BOARD_INPUT_ACTION_ENCODER_BUTTON, false);
    // Play starts what is highlighted rather than always the radio.
    expect(BOARD_INPUT_ACTION_PLAY_PAUSE, s, BOARD_INPUT_ACTION_ENCODER_BUTTON, false);
}

static void test_elsewhere_the_keys_are_the_knob(void)
{
    const ui_remote_screen_t s = UI_REMOTE_SCREEN_OTHER;
    expect(BOARD_INPUT_ACTION_REMOTE_UP, s, BOARD_INPUT_ACTION_ENCODER_LEFT, false);
    expect(BOARD_INPUT_ACTION_REMOTE_NEXT, s, BOARD_INPUT_ACTION_ENCODER_RIGHT, false);
    expect(BOARD_INPUT_ACTION_REMOTE_OK, s, BOARD_INPUT_ACTION_ENCODER_BUTTON, false);
    expect(BOARD_INPUT_ACTION_PLAY_PAUSE, s, BOARD_INPUT_ACTION_PLAY_PAUSE, false);
    // And every other action passes untouched.
    expect(BOARD_INPUT_ACTION_VOLUME_UP, UI_REMOTE_SCREEN_PLAYER, BOARD_INPUT_ACTION_VOLUME_UP,
           false);
    expect(BOARD_INPUT_ACTION_ENCODER_LEFT, UI_REMOTE_SCREEN_PLAYER, BOARD_INPUT_ACTION_ENCODER_LEFT,
           false);
}

int main(void)
{
    test_on_the_player();
    test_in_a_menu_or_a_list();
    test_elsewhere_the_keys_are_the_knob();
    puts("ui_remote_keys tests passed");
    return 0;
}
