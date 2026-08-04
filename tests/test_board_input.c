#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "board_input.h"

int main(void)
{
    board_input_debouncer_t debouncer;
    board_input_debouncer_t released_debouncer;
    board_encoder_decoder_t encoder;
    board_input_hold_t f1_hold;

    assert(board_input_action_from_gpio(BOARD_BUTTON_F1_GPIO, 0) == BOARD_INPUT_ACTION_F1);
    assert(board_input_action_from_gpio(BOARD_BUTTON_F1_GPIO, 1) == BOARD_INPUT_ACTION_NONE);
    assert(board_input_action_from_gpio(BOARD_ENCODER_LEFT_GPIO, 0) == BOARD_INPUT_ACTION_ENCODER_LEFT);
    assert(board_input_action_from_gpio(BOARD_ENCODER_RIGHT_GPIO, 0) == BOARD_INPUT_ACTION_ENCODER_RIGHT);
    assert(board_input_action_from_gpio(BOARD_ENCODER_BUTTON_GPIO, 0) == BOARD_INPUT_ACTION_ENCODER_BUTTON);
    assert(board_input_action_from_gpio(99, 0) == BOARD_INPUT_ACTION_NONE);

    board_input_debouncer_init(&debouncer, 5);
    assert(!board_input_debouncer_update(&debouncer, true));
    assert(!board_input_debouncer_update(&debouncer, false));
    assert(!board_input_debouncer_update(&debouncer, true));
    assert(!board_input_debouncer_update(&debouncer, true));
    assert(!board_input_debouncer_update(&debouncer, true));
    assert(!board_input_debouncer_update(&debouncer, true));
    assert(board_input_debouncer_update(&debouncer, true));
    assert(!board_input_debouncer_update(&debouncer, true));
    assert(!board_input_debouncer_update(&debouncer, false));
    assert(!board_input_debouncer_update(&debouncer, false));
    assert(!board_input_debouncer_update(&debouncer, false));
    assert(!board_input_debouncer_update(&debouncer, false));
    assert(!board_input_debouncer_update(&debouncer, false));
    assert(!board_input_debouncer_update(&debouncer, true));
    assert(!board_input_debouncer_update(&debouncer, true));
    assert(!board_input_debouncer_update(&debouncer, true));
    assert(!board_input_debouncer_update(&debouncer, true));
    assert(board_input_debouncer_update(&debouncer, true));
    assert(!board_input_debouncer_update(&debouncer, true));

    board_input_debouncer_init_from_level(&released_debouncer, 1, 5);
    for (int index = 0; index < 5; ++index) {
        assert(!board_input_debouncer_update(&released_debouncer, false));
    }

    board_encoder_decoder_init(&encoder, 1, 1);
    assert(board_encoder_decoder_update(&encoder, 1, 0) == BOARD_INPUT_ACTION_NONE);
    assert(board_encoder_decoder_update(&encoder, 0, 0) == BOARD_INPUT_ACTION_NONE);
    assert(board_encoder_decoder_update(&encoder, 0, 1) == BOARD_INPUT_ACTION_NONE);
    assert(board_encoder_decoder_update(&encoder, 1, 1) == BOARD_INPUT_ACTION_ENCODER_RIGHT);

    assert(board_encoder_decoder_update(&encoder, 0, 1) == BOARD_INPUT_ACTION_NONE);
    assert(board_encoder_decoder_update(&encoder, 0, 0) == BOARD_INPUT_ACTION_NONE);
    assert(board_encoder_decoder_update(&encoder, 1, 0) == BOARD_INPUT_ACTION_NONE);
    assert(board_encoder_decoder_update(&encoder, 1, 1) == BOARD_INPUT_ACTION_ENCODER_LEFT);

    board_input_hold_init(&f1_hold, 5000, 5);
    for (int index = 0; index < 999; ++index) {
        assert(!board_input_hold_update(&f1_hold, true));
    }
    assert(board_input_hold_update(&f1_hold, true));
    assert(!board_input_hold_update(&f1_hold, true));
    assert(!board_input_hold_update(&f1_hold, false));
    for (int index = 0; index < 999; ++index) {
        assert(!board_input_hold_update(&f1_hold, true));
    }
    assert(board_input_hold_update(&f1_hold, true));

    puts("board_input tests passed");
    return 0;
}
