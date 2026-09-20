#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_options.h"

/* The four function buttons are optional: a board with only the encoder
 * leaves their lines out of board_options.h, and each missing one becomes
 * "not wired" here rather than a build error in board_input.c. The channel
 * is then never configured, never polled and never matched, so the action
 * simply never arrives - the same as a button nobody presses. The encoder is
 * not optional: without it the UI cannot be driven at all.
 *
 * -1 is what esp_lcd uses for a pin that is not there (TFT_RESET_GPIO), and
 * it is not a GPIO number, so it cannot collide with a real pin. */
#define BOARD_GPIO_NOT_WIRED -1
#ifndef BUTTON_SLEEP_GPIO
#define BUTTON_SLEEP_GPIO BOARD_GPIO_NOT_WIRED
#endif
/* The old name for the same pin. A board file written before the button
 * was given its job still says F2, and this is a header nobody's local
 * copy is in git - dropping the name would leave that button silently
 * unwired, which is the one failure the fallback below cannot show. */
#ifdef BUTTON_F2_GPIO
#ifndef BUTTON_QUICK_MENU_GPIO
#define BUTTON_QUICK_MENU_GPIO BUTTON_F2_GPIO
#endif
#endif
#ifndef BUTTON_QUICK_MENU_GPIO
#define BUTTON_QUICK_MENU_GPIO BOARD_GPIO_NOT_WIRED
#endif
#ifndef BUTTON_PREV_GPIO
#define BUTTON_PREV_GPIO BOARD_GPIO_NOT_WIRED
#endif
#ifndef BUTTON_NEXT_GPIO
#define BUTTON_NEXT_GPIO BOARD_GPIO_NOT_WIRED
#endif
/* Goes with the buttons and is usually dropped with them; on its own the
 * internal pull-up is the safe default - the buttons on revision 1 were
 * unstable without it, and a floating input costs nothing with it. */
#ifndef BUTTONS_USE_INTERNAL_PULLUPS
#define BUTTONS_USE_INTERNAL_PULLUPS 1
#endif

typedef enum {
    BOARD_INPUT_ACTION_NONE = 0,
    BOARD_INPUT_ACTION_ENCODER_LEFT,
    BOARD_INPUT_ACTION_ENCODER_RIGHT,
    BOARD_INPUT_ACTION_ENCODER_BUTTON,
    /* The sleep button (F1 on the silkscreen) held is what puts the board to
     * sleep; its short press is wired, debounced and delivered with nothing
     * acting on it yet. The second button opens the quick panel and is named
     * for that rather than for the silkscreen - it used to mean "back" and
     * lost the job to the encoder's long press, which already did the same
     * thing everywhere F2 did. */
    BOARD_INPUT_ACTION_SLEEP_BUTTON,
    BOARD_INPUT_ACTION_SLEEP_LONG,
    BOARD_INPUT_ACTION_QUICK_MENU,
    BOARD_INPUT_ACTION_BTN_PREV,
    BOARD_INPUT_ACTION_BTN_NEXT,
    BOARD_INPUT_ACTION_ENCODER_LONG,
    /* What a remote control adds. None of these has a key on the case: the
     * knob's turn is volume on one screen and the cursor on another, and a
     * remote's Vol+ has to be volume wherever the device is. They travel the
     * same queue as the keys, so every screen handles them in one place. */
    BOARD_INPUT_ACTION_VOLUME_UP,
    BOARD_INPUT_ACTION_VOLUME_DOWN,
    BOARD_INPUT_ACTION_MUTE,
    BOARD_INPUT_ACTION_PLAY_PAUSE,
    BOARD_INPUT_ACTION_LIST,
    BOARD_INPUT_ACTION_SLEEP_CYCLE,
    BOARD_INPUT_ACTION_LIKE,
    BOARD_INPUT_ACTION_DISLIKE,
    /* Ten in a row, so a digit is the action less the first. */
    BOARD_INPUT_ACTION_DIGIT_0,
    BOARD_INPUT_ACTION_DIGIT_1,
    BOARD_INPUT_ACTION_DIGIT_2,
    BOARD_INPUT_ACTION_DIGIT_3,
    BOARD_INPUT_ACTION_DIGIT_4,
    BOARD_INPUT_ACTION_DIGIT_5,
    BOARD_INPUT_ACTION_DIGIT_6,
    BOARD_INPUT_ACTION_DIGIT_7,
    BOARD_INPUT_ACTION_DIGIT_8,
    BOARD_INPUT_ACTION_DIGIT_9,
    BOARD_INPUT_ACTION_SOURCE_RADIO,
    BOARD_INPUT_ACTION_SOURCE_USB,
    BOARD_INPUT_ACTION_SOURCE_SD,
    BOARD_INPUT_ACTION_SOURCE_BLUETOOTH,
    BOARD_INPUT_ACTION_SOURCE_YANDEX,
    BOARD_INPUT_ACTION_SOURCE_DLNA,
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
/* One button's click-or-hold, told apart. `click` is reported on release and
 * only when the hold never fired, `hold` the moment the press passes the
 * threshold - so a long press never also delivers a short one. The two
 * actions are arguments because more than one button needs this: the encoder
 * and the sleep button (F1) each have their own pair. */
board_input_action_t board_button_gesture_update(board_button_gesture_t *gesture, bool pressed,
                                                 uint32_t elapsed_ms,
                                                 board_input_action_t click,
                                                 board_input_action_t hold);

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t board_input_init(void);
bool board_input_read(board_input_action_t *action, TickType_t timeout);
/* An action from somewhere other than the pins - the remote control - put on
 * the same queue, so it reaches the screen the way a key does. False when the
 * queue is full or not yet there. */
bool board_input_inject(board_input_action_t action);
#endif
