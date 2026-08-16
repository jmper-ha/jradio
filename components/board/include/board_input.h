#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_options.h"

typedef enum {
    BOARD_INPUT_ACTION_NONE = 0,
    BOARD_INPUT_ACTION_ENCODER_LEFT,
    BOARD_INPUT_ACTION_ENCODER_RIGHT,
    BOARD_INPUT_ACTION_ENCODER_BUTTON,
    BOARD_INPUT_ACTION_F1,
    BOARD_INPUT_ACTION_F2,
    BOARD_INPUT_ACTION_F3,
    BOARD_INPUT_ACTION_F4,
    BOARD_INPUT_ACTION_ENCODER_LONG,
} board_input_action_t;

typedef struct {
    bool stable_pressed;
    bool candidate_pressed;
    uint8_t candidate_samples;
    uint8_t required_samples;
} board_input_debouncer_t;

typedef struct {
    uint8_t last_state;
    int8_t transition_sum;
} board_encoder_decoder_t;

typedef struct {
    bool pressed;
    bool long_sent;
    uint32_t held_ms;
} board_button_gesture_t;

board_input_action_t board_input_action_from_gpio(int gpio_num, int level);
void board_input_debouncer_init(board_input_debouncer_t *debouncer, uint8_t required_samples);
void board_input_debouncer_init_from_level(board_input_debouncer_t *debouncer, int level,
                                           uint8_t required_samples);
bool board_input_debouncer_update(board_input_debouncer_t *debouncer, bool sampled_pressed);
void board_encoder_decoder_init(board_encoder_decoder_t *decoder, int left_level, int right_level);
board_input_action_t board_encoder_decoder_update(board_encoder_decoder_t *decoder, int left_level,
                                                  int right_level);
void board_button_gesture_init(board_button_gesture_t *gesture);
board_input_action_t board_button_gesture_update(board_button_gesture_t *gesture, bool pressed,
                                                 uint32_t elapsed_ms);

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t board_input_init(void);
bool board_input_read(board_input_action_t *action, TickType_t timeout);
#endif
