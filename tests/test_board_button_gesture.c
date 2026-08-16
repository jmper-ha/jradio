#include "board_input.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    board_button_gesture_t gesture;
    board_button_gesture_init(&gesture);
    assert(board_button_gesture_update(&gesture, true, 5) == BOARD_INPUT_ACTION_NONE);
    assert(board_button_gesture_update(&gesture, true, 795) == BOARD_INPUT_ACTION_NONE);
    assert(board_button_gesture_update(&gesture, true, 5) == BOARD_INPUT_ACTION_ENCODER_LONG);
    assert(board_button_gesture_update(&gesture, true, 100) == BOARD_INPUT_ACTION_NONE);
    assert(board_button_gesture_update(&gesture, false, 5) == BOARD_INPUT_ACTION_NONE);

    assert(board_button_gesture_update(&gesture, true, 5) == BOARD_INPUT_ACTION_NONE);
    assert(board_button_gesture_update(&gesture, false, 5) == BOARD_INPUT_ACTION_ENCODER_BUTTON);
    puts("board_button_gesture tests passed");
    return 0;
}
