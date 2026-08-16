#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "board_input.h"

int main(void)
{
    board_input_debouncer_t debouncer;
    board_input_debouncer_t released_debouncer;
    board_encoder_decoder_t encoder;

    assert(board_input_action_from_gpio(BUTTON_F1_GPIO, 0) == BOARD_INPUT_ACTION_F1);
    assert(board_input_action_from_gpio(BUTTON_F1_GPIO, 1) == BOARD_INPUT_ACTION_NONE);
    assert(board_input_action_from_gpio(ENCODER_LEFT_GPIO, 0) == BOARD_INPUT_ACTION_ENCODER_LEFT);
    assert(board_input_action_from_gpio(ENCODER_RIGHT_GPIO, 0) == BOARD_INPUT_ACTION_ENCODER_RIGHT);
    assert(board_input_action_from_gpio(ENCODER_BUTTON_GPIO, 0) == BOARD_INPUT_ACTION_ENCODER_BUTTON);
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

    puts("board_input tests passed");
    return 0;
}
