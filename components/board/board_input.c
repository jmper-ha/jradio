#include <stddef.h>

#include "board_input.h"

#define BOARD_INPUT_LONG_PRESS_MS 800U

void board_button_gesture_init(board_button_gesture_t *gesture)
{
    if (gesture != NULL) *gesture = (board_button_gesture_t){0};
}

board_input_action_t board_button_gesture_update(board_button_gesture_t *gesture, bool pressed,
                                                 uint32_t elapsed_ms)
{
    if (gesture == NULL) return BOARD_INPUT_ACTION_NONE;
    if (!pressed) {
        const board_input_action_t action = gesture->pressed && !gesture->long_sent
                                                ? BOARD_INPUT_ACTION_ENCODER_BUTTON
                                                : BOARD_INPUT_ACTION_NONE;
        *gesture = (board_button_gesture_t){0};
        return action;
    }
    if (!gesture->pressed) {
        gesture->pressed = true;
        gesture->held_ms = 0U;
        gesture->long_sent = false;
        return BOARD_INPUT_ACTION_NONE;
    }
    if (gesture->long_sent) return BOARD_INPUT_ACTION_NONE;
    gesture->held_ms += elapsed_ms;
    if (gesture->held_ms >= BOARD_INPUT_LONG_PRESS_MS) {
        gesture->long_sent = true;
        return BOARD_INPUT_ACTION_ENCODER_LONG;
    }
    return BOARD_INPUT_ACTION_NONE;
}

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* Poll interval and debounce window come from board_options.h - they describe
 * the physical switches, not this driver. Only the queue depth is a firmware
 * choice. */
#define INPUT_DEBOUNCE_SAMPLES (INPUT_DEBOUNCE_MS / INPUT_POLL_MS)
#define INPUT_QUEUE_LENGTH 16

typedef struct {
    int gpio_num;
    board_input_action_t action;
    board_input_debouncer_t debouncer;
    board_button_gesture_t gesture;
} board_input_channel_t;

static const char *TAG = "input";
static QueueHandle_t s_event_queue;
static board_input_channel_t s_channels[] = {
    {.gpio_num = ENCODER_BUTTON_GPIO, .action = BOARD_INPUT_ACTION_ENCODER_BUTTON},
    {.gpio_num = BUTTON_F1_GPIO, .action = BOARD_INPUT_ACTION_F1},
    {.gpio_num = BUTTON_F2_GPIO, .action = BOARD_INPUT_ACTION_F2},
    {.gpio_num = BUTTON_F3_GPIO, .action = BOARD_INPUT_ACTION_F3},
    {.gpio_num = BUTTON_F4_GPIO, .action = BOARD_INPUT_ACTION_F4},
};
static board_encoder_decoder_t s_encoder_decoder;

static void board_input_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        const board_input_action_t encoder_action =
            board_encoder_decoder_update(&s_encoder_decoder, gpio_get_level(ENCODER_LEFT_GPIO),
                                         gpio_get_level(ENCODER_RIGHT_GPIO));
        if (encoder_action != BOARD_INPUT_ACTION_NONE &&
            xQueueSend(s_event_queue, &encoder_action, 0) != pdTRUE) {
            ESP_LOGW(TAG, "input queue full; action=%d dropped", (int)encoder_action);
        }
        for (size_t index = 0; index < sizeof(s_channels) / sizeof(s_channels[0]); ++index) {
            board_input_channel_t *channel = &s_channels[index];
            const int raw_level = gpio_get_level(channel->gpio_num);
            const bool pressed = raw_level == 0;
            // The debouncer reports a confirmed *press* and nothing else, so
            // a true return already means "just pressed" - testing the raw
            // level again would only suggest releases were handled here.
            const bool press_confirmed =
                board_input_debouncer_update(&channel->debouncer, pressed);
            // The encoder button has to tell a click from a hold, so it reads
            // the debounced level rather than the edge.
            const board_input_action_t generated = channel->gpio_num == ENCODER_BUTTON_GPIO
                ? board_button_gesture_update(&channel->gesture,
                                              channel->debouncer.stable_pressed, INPUT_POLL_MS)
                : (press_confirmed ? channel->action : BOARD_INPUT_ACTION_NONE);
            if (generated != BOARD_INPUT_ACTION_NONE &&
                xQueueSend(s_event_queue, &generated, 0) != pdTRUE) {
                ESP_LOGW(TAG, "input queue full; action=%d dropped", (int)generated);
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(INPUT_POLL_MS));
    }
}
#endif

board_input_action_t board_input_action_from_gpio(int gpio_num, int level)
{
    if (level != 0) {
        return BOARD_INPUT_ACTION_NONE;
    }

    switch (gpio_num) {
    case ENCODER_LEFT_GPIO:
        return BOARD_INPUT_ACTION_ENCODER_LEFT;
    case ENCODER_RIGHT_GPIO:
        return BOARD_INPUT_ACTION_ENCODER_RIGHT;
    case ENCODER_BUTTON_GPIO:
        return BOARD_INPUT_ACTION_ENCODER_BUTTON;
    case BUTTON_F1_GPIO:
        return BOARD_INPUT_ACTION_F1;
    case BUTTON_F2_GPIO:
        return BOARD_INPUT_ACTION_F2;
    case BUTTON_F3_GPIO:
        return BOARD_INPUT_ACTION_F3;
    case BUTTON_F4_GPIO:
        return BOARD_INPUT_ACTION_F4;
    default:
        return BOARD_INPUT_ACTION_NONE;
    }
}

void board_input_debouncer_init(board_input_debouncer_t *debouncer, uint8_t required_samples)
{
    if (debouncer == NULL) {
        return;
    }

    debouncer->stable_pressed = false;
    debouncer->candidate_pressed = false;
    debouncer->candidate_samples = 0;
    debouncer->required_samples = required_samples == 0 ? 1 : required_samples;
}

void board_input_debouncer_init_from_level(board_input_debouncer_t *debouncer, int level,
                                           uint8_t required_samples)
{
    board_input_debouncer_init(debouncer, required_samples);
    if (debouncer != NULL) {
        debouncer->stable_pressed = level == 0;
        debouncer->candidate_pressed = debouncer->stable_pressed;
    }
}

bool board_input_debouncer_update(board_input_debouncer_t *debouncer, bool sampled_pressed)
{
    if (debouncer == NULL || sampled_pressed == debouncer->stable_pressed) {
        if (debouncer != NULL) {
            debouncer->candidate_samples = 0;
            debouncer->candidate_pressed = sampled_pressed;
        }
        return false;
    }

    if (sampled_pressed != debouncer->candidate_pressed) {
        debouncer->candidate_pressed = sampled_pressed;
        debouncer->candidate_samples = 1;
    } else if (debouncer->candidate_samples < debouncer->required_samples) {
        ++debouncer->candidate_samples;
    }

    if (debouncer->candidate_samples < debouncer->required_samples) {
        return false;
    }

    debouncer->stable_pressed = sampled_pressed;
    debouncer->candidate_samples = 0;
    return sampled_pressed;
}

static uint8_t board_encoder_state_from_levels(int left_level, int right_level)
{
    return (uint8_t)(((left_level != 0) << 1) | (right_level != 0));
}

void board_encoder_decoder_init(board_encoder_decoder_t *decoder, int left_level, int right_level)
{
    if (decoder == NULL) {
        return;
    }
    decoder->last_state = board_encoder_state_from_levels(left_level, right_level);
    decoder->transition_sum = 0;
}

board_input_action_t board_encoder_decoder_update(board_encoder_decoder_t *decoder, int left_level,
                                                  int right_level)
{
    static const int8_t transition_delta[16] = {
        0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0,
    };

    if (decoder == NULL) {
        return BOARD_INPUT_ACTION_NONE;
    }

    const uint8_t current_state = board_encoder_state_from_levels(left_level, right_level);
    const uint8_t transition = (uint8_t)((decoder->last_state << 2) | current_state);
    const int8_t delta = transition_delta[transition];
    decoder->last_state = current_state;

    if (delta == 0) {
        if ((transition & 0x03U) != (transition >> 2)) {
            decoder->transition_sum = 0;
        }
        return BOARD_INPUT_ACTION_NONE;
    }

    decoder->transition_sum += delta;
    if (decoder->transition_sum >= 4) {
        decoder->transition_sum = 0;
        return BOARD_INPUT_ACTION_ENCODER_RIGHT;
    }
    if (decoder->transition_sum <= -4) {
        decoder->transition_sum = 0;
        return BOARD_INPUT_ACTION_ENCODER_LEFT;
    }
    return BOARD_INPUT_ACTION_NONE;
}

#ifdef ESP_PLATFORM
esp_err_t board_input_init(void)
{
    /* Two calls rather than one: the encoder and the buttons want different
     * pull-up settings, and gpio_config() applies one setting to every pin in
     * its mask. The encoder button belongs to the encoder here - it is the
     * same part and the same external pull-up. */
    const gpio_config_t encoder_config = {
        .pin_bit_mask = (1ULL << ENCODER_RIGHT_GPIO) | (1ULL << ENCODER_LEFT_GPIO) |
                        (1ULL << ENCODER_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = ENCODER_USE_INTERNAL_PULLUPS ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BUTTON_F1_GPIO) | (1ULL << BUTTON_F2_GPIO) |
                        (1ULL << BUTTON_F3_GPIO) | (1ULL << BUTTON_F4_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = BUTTONS_USE_INTERNAL_PULLUPS ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t result = gpio_config(&encoder_config);
    if (result != ESP_OK) {
        return result;
    }
    result = gpio_config(&button_config);
    if (result != ESP_OK) {
        return result;
    }
    if (s_event_queue != NULL) {
        return ESP_OK;
    }

    s_event_queue = xQueueCreate(INPUT_QUEUE_LENGTH, sizeof(board_input_action_t));
    if (s_event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t index = 0; index < sizeof(s_channels) / sizeof(s_channels[0]); ++index) {
        const int level = gpio_get_level(s_channels[index].gpio_num);
        board_input_debouncer_init_from_level(&s_channels[index].debouncer, level,
                                              INPUT_DEBOUNCE_SAMPLES);
        board_button_gesture_init(&s_channels[index].gesture);
    }
    board_encoder_decoder_init(&s_encoder_decoder, gpio_get_level(ENCODER_LEFT_GPIO),
                               gpio_get_level(ENCODER_RIGHT_GPIO));
    if (xTaskCreate(board_input_task, "board_input", 3072, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "input task started; poll=%d ms debounce=%d ms", INPUT_POLL_MS,
             INPUT_DEBOUNCE_MS);
    return ESP_OK;
}

bool board_input_read(board_input_action_t *action, TickType_t timeout)
{
    return s_event_queue != NULL && action != NULL &&
           xQueueReceive(s_event_queue, action, timeout) == pdTRUE;
}
#endif
