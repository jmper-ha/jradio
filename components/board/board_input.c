#include <stddef.h>

#include "board_input.h"

#define BOARD_INPUT_LONG_PRESS_MS 800U

/* The software contact filter, kept outside the ESP_PLATFORM guard below so
 * the host build checks the relation between the two: a level must hold for
 * INPUT_DEBOUNCE_MS across polls INPUT_POLL_MS apart to count, and the encoder
 * is sampled at the same interval for want of an interrupt line. */
#define INPUT_POLL_MS 5
#define INPUT_DEBOUNCE_MS 25
/* The sample count below is an integer division, so a window that is not a
 * whole number of polls would silently filter less than it claims. */
_Static_assert(INPUT_DEBOUNCE_MS % INPUT_POLL_MS == 0 &&
                   INPUT_DEBOUNCE_MS / INPUT_POLL_MS >= 2,
               "debounce window must be at least two whole poll intervals");

void board_button_gesture_init(board_button_gesture_t *gesture)
{
    if (gesture != NULL) *gesture = (board_button_gesture_t){0};
}

board_input_action_t board_button_gesture_update(board_button_gesture_t *gesture, bool pressed,
                                                 uint32_t elapsed_ms,
                                                 board_input_action_t click,
                                                 board_input_action_t hold)
{
    if (gesture == NULL) return BOARD_INPUT_ACTION_NONE;
    if (!pressed) {
        const board_input_action_t action =
            gesture->pressed && !gesture->long_sent ? click : BOARD_INPUT_ACTION_NONE;
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
        return hold;
    }
    return BOARD_INPUT_ACTION_NONE;
}

void board_button_repeat_init(board_button_repeat_t *repeat)
{
    if (repeat != NULL) *repeat = (board_button_repeat_t){0};
}

bool board_button_repeat_update(board_button_repeat_t *repeat, bool pressed, uint32_t elapsed_ms)
{
    if (repeat == NULL) return false;
    if (!pressed) {
        *repeat = (board_button_repeat_t){0};
        return false;
    }
    if (!repeat->pressed) {
        repeat->pressed = true;
        repeat->held_ms = 0U;
        repeat->next_ms = BOARD_BUTTON_REPEAT_DELAY_MS;
        return true;
    }
    repeat->held_ms += elapsed_ms;
    if (repeat->held_ms < repeat->next_ms) return false;
    repeat->next_ms += repeat->held_ms >= BOARD_BUTTON_REPEAT_FAST_AFTER_MS
                           ? BOARD_BUTTON_REPEAT_FAST_MS
                           : BOARD_BUTTON_REPEAT_SLOW_MS;
    return true;
}

#ifdef ESP_PLATFORM
#include "board_config.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define INPUT_DEBOUNCE_SAMPLES (INPUT_DEBOUNCE_MS / INPUT_POLL_MS)
#define INPUT_QUEUE_LENGTH 16

typedef struct {
    int gpio_num;
    board_input_action_t action;
    /* NONE for a button read as a plain edge; anything else makes this
     * channel tell a click from a hold and report that on the hold. */
    board_input_action_t hold_action;
    board_input_debouncer_t debouncer;
    board_button_gesture_t gesture;
    bool wait_release;
} board_input_channel_t;

static const char *TAG = "input";
static QueueHandle_t s_event_queue;
/* The pins are filled in by board_input_init() out of the wiring, which is
 * read at boot and not known to the compiler. */
static board_input_channel_t s_channels[] = {
    {.gpio_num = BOARD_GPIO_NOT_WIRED, .action = BOARD_INPUT_ACTION_ENCODER_BUTTON,
     .hold_action = BOARD_INPUT_ACTION_ENCODER_LONG},
    {.gpio_num = BOARD_GPIO_NOT_WIRED, .action = BOARD_INPUT_ACTION_SLEEP_BUTTON,
     .hold_action = BOARD_INPUT_ACTION_SLEEP_LONG},
    {.gpio_num = BOARD_GPIO_NOT_WIRED, .action = BOARD_INPUT_ACTION_QUICK_MENU},
    {.gpio_num = BOARD_GPIO_NOT_WIRED, .action = BOARD_INPUT_ACTION_BTN_PREV},
    {.gpio_num = BOARD_GPIO_NOT_WIRED, .action = BOARD_INPUT_ACTION_BTN_NEXT},
    {.gpio_num = BOARD_GPIO_NOT_WIRED, .action = BOARD_INPUT_ACTION_PANEL_MENU_BACK,
     .hold_action = BOARD_INPUT_ACTION_PANEL_QUICK_TOGGLE},
};
typedef struct {
    int left;
    int right;
    uint8_t edges;
    board_input_action_t negative;
    board_input_action_t positive;
    board_encoder_decoder_t decoder;
    pcnt_unit_handle_t unit;
    pcnt_channel_handle_t left_channel;
    pcnt_channel_handle_t right_channel;
    int32_t count_seen;
    int32_t remainder;
    int32_t queued_detents;
} board_encoder_channel_t;

static board_encoder_channel_t s_encoders[2];
/* Three buttons on the encoder's lines instead of the encoder: - on the left
 * line and + on the right, each debounced and repeating while held. */
static bool s_encoder_buttons;
static board_input_debouncer_t s_step_debouncers[2];
static board_button_repeat_t s_step_repeats[2];
/* The encoder in hardware. Polling the two lines every 5 ms lost detents on a
 * quick turn: four transitions a detent at forty detents a second is one every
 * 6 ms, two of them fell between samples, and the software decoder threw the
 * whole detent away on the jump. The pulse counter sees every edge whenever
 * this task gets to run, and contact bounce on one line while the other holds
 * counts up and down again in quadrature, so it cancels. NULL when the
 * counter could not be set up; the software decoder then does the job as
 * before. */
/* Far enough out that a count never reaches them between two reads; the unit
 * accumulates across them anyway, so the count read is continuous. */
#define ENCODER_PCNT_LIMIT 30000
/* Spikes shorter than this are not edges. The counter's filter tops out a
 * little under 13 us at the APB clock; contact bounce is longer and is dealt
 * with by the quadrature counting above. */
#define ENCODER_PCNT_GLITCH_NS 10000

static void board_encoder_sample(board_encoder_channel_t *encoder)
{
    if (encoder->left < 0 || encoder->right < 0) return;
    int32_t detents = 0;
    if (encoder->unit != NULL) {
        int count;
        if (pcnt_unit_get_count(encoder->unit, &count) == ESP_OK) {
            const int32_t delta = board_encoder_count_delta(encoder->count_seen, count);
            encoder->count_seen = count;
            detents = board_encoder_detents_take_edges(&encoder->remainder, delta, encoder->edges);
        }
    } else {
        const board_input_action_t step = board_encoder_decoder_update(
            &encoder->decoder, gpio_get_level(encoder->left), gpio_get_level(encoder->right));
        if (step == BOARD_INPUT_ACTION_ENCODER_RIGHT) detents = 1;
        if (step == BOARD_INPUT_ACTION_ENCODER_LEFT) detents = -1;
    }
    const int64_t total = (int64_t)encoder->queued_detents + detents;
    const int limit = INPUT_QUEUE_LENGTH * 4;
    if (total > limit || total < -limit) {
        if (encoder->queued_detents != limit && encoder->queued_detents != -limit) {
            ESP_LOGW(TAG, "encoder GPIO %d/%d backlog saturated", encoder->left, encoder->right);
        }
        encoder->queued_detents = total > 0 ? limit : -limit;
    } else {
        encoder->queued_detents = (int32_t)total;
    }
}

static void board_encoder_flush(void)
{
    static size_t first;
    // Alternate even when the queue has room for only one turn. Keep room for
    // button presses and injected input, and carry the rest to the next poll.
    while (uxQueueSpacesAvailable(s_event_queue) > 4U) {
        size_t index = first;
        if (s_encoders[index].queued_detents == 0) index ^= 1U;
        int32_t *pending = &s_encoders[index].queued_detents;
        if (*pending == 0) break;
        const bool right = *pending > 0;
        const board_input_action_t action = right ? s_encoders[index].positive
                                                  : s_encoders[index].negative;
        if (xQueueSend(s_event_queue, &action, 0) != pdTRUE) break;
        *pending += right ? -1 : 1;
        first = index ^ 1U;
    }
}

static void board_input_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        if (s_encoder_buttons) {
            const int lines[2] = {s_encoders[0].left, s_encoders[0].right};
            const board_input_action_t steps[2] = {BOARD_INPUT_ACTION_ENCODER_LEFT,
                                                   BOARD_INPUT_ACTION_ENCODER_RIGHT};
            for (size_t index = 0U; index < 2U; ++index) {
                (void)board_input_debouncer_update(&s_step_debouncers[index],
                                                   gpio_get_level(lines[index]) == 0);
                if (board_button_repeat_update(&s_step_repeats[index],
                                               s_step_debouncers[index].stable_pressed,
                                               INPUT_POLL_MS) &&
                    xQueueSend(s_event_queue, &steps[index], 0) != pdTRUE) {
                    ESP_LOGW(TAG, "input queue full; action=%d dropped", (int)steps[index]);
                }
            }
        }
        if (!s_encoder_buttons) board_encoder_sample(&s_encoders[0]);
        board_encoder_sample(&s_encoders[1]);
        for (size_t index = 0; index < sizeof(s_channels) / sizeof(s_channels[0]); ++index) {
            board_input_channel_t *channel = &s_channels[index];
            if (channel->gpio_num == BOARD_GPIO_NOT_WIRED) continue;
            const int raw_level = gpio_get_level(channel->gpio_num);
            const bool pressed = raw_level == 0;
            // The debouncer reports a confirmed *press* and nothing else, so
            // a true return already means "just pressed" - testing the raw
            // level again would only suggest releases were handled here.
            const bool press_confirmed =
                board_input_debouncer_update(&channel->debouncer, pressed);
            if (channel->wait_release) {
                if (!channel->debouncer.stable_pressed) channel->wait_release = false;
                continue;
            }
            // A button that has a hold of its own reads the debounced level
            // rather than the edge: that is what lets it tell a click from a
            // press that stayed down.
            const board_input_action_t generated =
                channel->hold_action != BOARD_INPUT_ACTION_NONE
                    ? board_button_gesture_update(&channel->gesture,
                                                  channel->debouncer.stable_pressed, INPUT_POLL_MS,
                                                  channel->action, channel->hold_action)
                    : (press_confirmed ? channel->action : BOARD_INPUT_ACTION_NONE);
            if (generated != BOARD_INPUT_ACTION_NONE &&
                xQueueSend(s_event_queue, &generated, 0) != pdTRUE) {
                ESP_LOGW(TAG, "input queue full; action=%d dropped", (int)generated);
            }
        }
        board_encoder_flush();
        _Static_assert(pdMS_TO_TICKS(INPUT_POLL_MS) > 0, "input poll must span at least one tick");
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(INPUT_POLL_MS));
    }
}
#endif

board_input_action_t board_input_action_from_gpio(int gpio_num, int level)
{
    if (level != 0) {
        return BOARD_INPUT_ACTION_NONE;
    }

    /* A walk rather than a switch: two buttons left unwired would both be
     * case -1, which does not compile, and a switch cannot say that a line
     * matching "not wired" is no match at all. */
    static const struct {
        int gpio_num;
        board_input_action_t action;
    } lines[] = {
        {ENCODER_LEFT_GPIO, BOARD_INPUT_ACTION_ENCODER_LEFT},
        {ENCODER_RIGHT_GPIO, BOARD_INPUT_ACTION_ENCODER_RIGHT},
        {ENCODER_BUTTON_GPIO, BOARD_INPUT_ACTION_ENCODER_BUTTON},
        {BUTTON_SLEEP_GPIO, BOARD_INPUT_ACTION_SLEEP_BUTTON},
        {BUTTON_QUICK_MENU_GPIO, BOARD_INPUT_ACTION_QUICK_MENU},
        {BUTTON_PREV_GPIO, BOARD_INPUT_ACTION_BTN_PREV},
        {BUTTON_NEXT_GPIO, BOARD_INPUT_ACTION_BTN_NEXT},
    };
    if (gpio_num == BOARD_GPIO_NOT_WIRED) return BOARD_INPUT_ACTION_NONE;
    for (size_t index = 0; index < sizeof(lines) / sizeof(lines[0]); ++index) {
        if (lines[index].gpio_num == gpio_num) return lines[index].action;
    }
    return BOARD_INPUT_ACTION_NONE;
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
    board_encoder_decoder_init_edges(decoder, left_level, right_level, 4);
}

void board_encoder_decoder_init_edges(board_encoder_decoder_t *decoder, int left_level,
                                      int right_level, uint8_t edges)
{
    if (decoder == NULL) {
        return;
    }
    decoder->last_state = board_encoder_state_from_levels(left_level, right_level);
    decoder->transition_sum = 0;
    decoder->edges = edges == 2 ? 2 : 4;
}

int32_t board_encoder_detents_take(int32_t *pending, int32_t counted)
{
    return board_encoder_detents_take_edges(pending, counted, 4);
}

int32_t board_encoder_detents_take_edges(int32_t *pending, int32_t counted, uint8_t edges)
{
    if (pending == NULL) return 0;
    const int divisor = edges == 2 ? 2 : 4;
    const int64_t total = (int64_t)*pending + counted;
    /* Toward zero, so a turn that stopped short of a detent leaves its partial
     * count to be finished by the next movement - or undone by going back -
     * rather than rounded into a step nobody made. */
    *pending = (int32_t)(total % divisor);
    return (int32_t)(total / divisor);
}

int32_t board_encoder_count_delta(int32_t previous, int32_t current)
{
    const uint32_t delta = (uint32_t)current - (uint32_t)previous;
    return delta <= INT32_MAX ? (int32_t)delta : -1 - (int32_t)(UINT32_MAX - delta);
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
    if (decoder->transition_sum >= decoder->edges) {
        decoder->transition_sum = 0;
        return BOARD_INPUT_ACTION_ENCODER_RIGHT;
    }
    if (decoder->transition_sum <= -(int)decoder->edges) {
        decoder->transition_sum = 0;
        return BOARD_INPUT_ACTION_ENCODER_LEFT;
    }
    return BOARD_INPUT_ACTION_NONE;
}

#ifdef ESP_PLATFORM
/* A bit for gpio_config's mask, or none for a line that is not wired. */
static uint64_t board_input_pin_bit(int gpio_num)
{
    return gpio_num == BOARD_GPIO_NOT_WIRED ? 0ULL : 1ULL << gpio_num;
}

/* Two channels for x4 quadrature, signed to match the software decoder's
 * table: a rise on the right line while the left is low counts up, as does a
 * fall on the right line while the left is high - with both lines idling high
 * on their pull-ups, a detent to the right is +4. */
static void board_encoder_pcnt_release(board_encoder_channel_t *encoder)
{
    if (encoder->unit == NULL) return;
    (void)pcnt_unit_stop(encoder->unit);
    (void)pcnt_unit_disable(encoder->unit);
    if (encoder->left_channel != NULL) (void)pcnt_del_channel(encoder->left_channel);
    if (encoder->right_channel != NULL) (void)pcnt_del_channel(encoder->right_channel);
    (void)pcnt_del_unit(encoder->unit);
    encoder->unit = NULL;
    encoder->left_channel = NULL;
    encoder->right_channel = NULL;
}

static esp_err_t board_encoder_pcnt_start(board_encoder_channel_t *encoder)
{
    const int left = encoder->left;
    const int right = encoder->right;
    const pcnt_unit_config_t unit_config = {
        .low_limit = -ENCODER_PCNT_LIMIT,
        .high_limit = ENCODER_PCNT_LIMIT,
        .flags.accum_count = true,
    };
    pcnt_unit_handle_t unit = NULL;
    esp_err_t err = pcnt_new_unit(&unit_config, &unit);
    if (err != ESP_OK) return err;
    encoder->unit = unit;
    const pcnt_glitch_filter_config_t filter = {.max_glitch_ns = ENCODER_PCNT_GLITCH_NS};
    const pcnt_chan_config_t on_right = {.edge_gpio_num = right, .level_gpio_num = left};
    const pcnt_chan_config_t on_left = {.edge_gpio_num = left, .level_gpio_num = right};
    if ((err = pcnt_unit_set_glitch_filter(unit, &filter)) != ESP_OK ||
        (err = pcnt_new_channel(unit, &on_right, &encoder->right_channel)) != ESP_OK ||
        (err = pcnt_new_channel(unit, &on_left, &encoder->left_channel)) != ESP_OK ||
        (err = pcnt_channel_set_edge_action(encoder->right_channel, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                            PCNT_CHANNEL_EDGE_ACTION_INCREASE)) != ESP_OK ||
        (err = pcnt_channel_set_level_action(encoder->right_channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                             PCNT_CHANNEL_LEVEL_ACTION_INVERSE)) != ESP_OK ||
        (err = pcnt_channel_set_edge_action(encoder->left_channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                            PCNT_CHANNEL_EDGE_ACTION_DECREASE)) != ESP_OK ||
        (err = pcnt_channel_set_level_action(encoder->left_channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                             PCNT_CHANNEL_LEVEL_ACTION_INVERSE)) != ESP_OK ||
        (err = pcnt_unit_add_watch_point(unit, ENCODER_PCNT_LIMIT)) != ESP_OK ||
        (err = pcnt_unit_add_watch_point(unit, -ENCODER_PCNT_LIMIT)) != ESP_OK ||
        (err = pcnt_unit_enable(unit)) != ESP_OK ||
        (err = pcnt_unit_clear_count(unit)) != ESP_OK ||
        (err = pcnt_unit_start(unit)) != ESP_OK) {
        board_encoder_pcnt_release(encoder);
        return err;
    }
    return ESP_OK;
}

esp_err_t board_input_init(void)
{
    if (s_event_queue != NULL) return ESP_OK;
    const board_config_t *wiring = board_config_get();
    const bool dual_encoder = wiring->controls == CONTROLS_DUAL_ENCODER;
    s_encoders[0] = (board_encoder_channel_t){
        .left = wiring->encoder_left, .right = wiring->encoder_right,
        .edges = dual_encoder ? wiring->encoder_edges : 4,
        .negative = dual_encoder ? BOARD_INPUT_ACTION_STATION_ENCODER_LEFT : BOARD_INPUT_ACTION_ENCODER_LEFT,
        .positive = dual_encoder ? BOARD_INPUT_ACTION_STATION_ENCODER_RIGHT : BOARD_INPUT_ACTION_ENCODER_RIGHT,
    };
    s_encoders[1] = (board_encoder_channel_t){
        .left = wiring->volume_encoder_a, .right = wiring->volume_encoder_b,
        .edges = wiring->volume_encoder_edges,
        .negative = BOARD_INPUT_ACTION_VOLUME_ENCODER_DOWN,
        .positive = BOARD_INPUT_ACTION_VOLUME_ENCODER_UP,
    };
    s_encoder_buttons = wiring->encoder_buttons != 0U;
    s_channels[0].action = dual_encoder ? BOARD_INPUT_ACTION_PANEL_OK : BOARD_INPUT_ACTION_ENCODER_BUTTON;
    s_channels[0].hold_action = dual_encoder ? BOARD_INPUT_ACTION_PANEL_MUTE : BOARD_INPUT_ACTION_ENCODER_LONG;
    // In s_channels' order.
    const int8_t buttons[] = {wiring->encoder_button, wiring->button_sleep,
                              wiring->button_quick_menu, wiring->button_prev,
                              wiring->button_next, dual_encoder ? wiring->button_menu : BOARD_PIN_NONE};
    uint64_t pins = 0;
    for (size_t index = 0; index < sizeof(s_channels) / sizeof(s_channels[0]); ++index) {
        s_channels[index].gpio_num = buttons[index] >= 0 ? buttons[index] : BOARD_GPIO_NOT_WIRED;
        pins |= board_input_pin_bit(s_channels[index].gpio_num);
    }
    for (size_t index = 0; index < 2; ++index) {
        if (s_encoders[index].left < 0 || s_encoders[index].right < 0) continue;
        pins |= board_input_pin_bit(s_encoders[index].left) |
                board_input_pin_bit(s_encoders[index].right);
    }
    /* Every input with its internal pull-up, always (board_options.h): on
     * top of an external one it changes nothing that matters, and without
     * one - a bare button, a bare encoder - it is what keeps the pin from
     * floating. It was a choice per group until 2026-10-20, and a Sleep
     * button wired with the choice off pressed itself on noise. */
    const gpio_config_t input_config = {
        .pin_bit_mask = pins,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t result = gpio_config(&input_config);
    if (result != ESP_OK) {
        return result;
    }
    s_event_queue = xQueueCreate(INPUT_QUEUE_LENGTH, sizeof(board_input_action_t));
    if (s_event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t index = 0; index < sizeof(s_channels) / sizeof(s_channels[0]); ++index) {
        if (s_channels[index].gpio_num == BOARD_GPIO_NOT_WIRED) continue;
        const int level = gpio_get_level(s_channels[index].gpio_num);
        board_input_debouncer_init_from_level(&s_channels[index].debouncer, level,
                                              INPUT_DEBOUNCE_SAMPLES);
        board_button_gesture_init(&s_channels[index].gesture);
        s_channels[index].wait_release = dual_encoder && (index == 0 || index == 5) && level == 0;
    }
    if (s_encoder_buttons) {
        const int lines[2] = {s_encoders[0].left, s_encoders[0].right};
        for (size_t index = 0U; index < 2U; ++index) {
            board_input_debouncer_init_from_level(&s_step_debouncers[index],
                                                  gpio_get_level(lines[index]),
                                                  INPUT_DEBOUNCE_SAMPLES);
            board_button_repeat_init(&s_step_repeats[index]);
        }
    }
    for (size_t index = 0; index < 2; ++index) {
        board_encoder_channel_t *encoder = &s_encoders[index];
        if ((index == 0 && s_encoder_buttons) || encoder->left < 0 || encoder->right < 0) continue;
        const esp_err_t counted = board_encoder_pcnt_start(encoder);
        if (counted != ESP_OK) {
            const gpio_config_t fallback = {
                .pin_bit_mask = board_input_pin_bit(encoder->left) | board_input_pin_bit(encoder->right),
                .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
            };
            const esp_err_t restored = gpio_config(&fallback);
            if (restored != ESP_OK) {
                for (size_t other = 0; other < 2; ++other) board_encoder_pcnt_release(&s_encoders[other]);
                vQueueDelete(s_event_queue);
                s_event_queue = NULL;
                return restored;
            }
            board_encoder_decoder_init_edges(&encoder->decoder, gpio_get_level(encoder->left),
                                              gpio_get_level(encoder->right), encoder->edges);
            ESP_LOGW(TAG, "encoder %u pulse counter unavailable (%s); polling instead",
                     (unsigned)index + 1U, esp_err_to_name(counted));
        }
    }
    if (xTaskCreate(board_input_task, "board_input", 3072, NULL, 5, NULL) != pdPASS) {
        for (size_t index = 0; index < 2; ++index) board_encoder_pcnt_release(&s_encoders[index]);
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "input task started; poll=%d ms debounce=%d ms encoder=%s", INPUT_POLL_MS,
             INPUT_DEBOUNCE_MS,
             s_encoder_buttons ? "three buttons" : s_encoders[0].unit != NULL ? "pcnt" : "polled");
    ESP_LOGI(TAG, "controls=%s; ENC2 A=%d B=%d (%s); menu=%d", dual_encoder ? "dual_encoder" : "legacy",
             s_encoders[1].left, s_encoders[1].right, s_encoders[1].unit != NULL ? "pcnt" :
             s_encoders[1].left >= 0 && s_encoders[1].right >= 0 ? "polled" : "not wired",
             s_channels[5].gpio_num);
    /* The pins as the firmware took them from the wiring. A board that went to
     * sleep two seconds after every boot had its sleep button's pin floating -
     * no button soldered yet - and nothing in the log said which pin that was
     * or whether its pull-up was on. */
    ESP_LOGI(TAG, "encoder L=%d R=%d button=%d; sleep=%d quick=%d prev=%d next=%d; "
                  "internal pull-ups on",
             wiring->encoder_left, wiring->encoder_right, wiring->encoder_button,
             wiring->button_sleep, wiring->button_quick_menu, wiring->button_prev,
             wiring->button_next);
    // In s_channels' order.
    static const char *const names[] = {"encoder button", "sleep", "quick", "prev", "next", "menu"};
    for (size_t index = 0; index < sizeof(s_channels) / sizeof(s_channels[0]); ++index) {
        if (s_channels[index].gpio_num == BOARD_GPIO_NOT_WIRED) continue;
        // Buttons pull to ground: low before anyone could have touched one is
        // a pin with nothing holding it up, and the hold will act on its own.
        if (gpio_get_level(s_channels[index].gpio_num) == 0) {
            ESP_LOGW(TAG, "%s button (GPIO %d) reads pressed at boot: held, or shorted to ground",
                     names[index], s_channels[index].gpio_num);
        }
    }
    return ESP_OK;
}

bool board_input_inject(board_input_action_t action)
{
    if (s_event_queue == NULL || action == BOARD_INPUT_ACTION_NONE) return false;
    return xQueueSend(s_event_queue, &action, 0) == pdTRUE;
}

QueueHandle_t board_input_queue(void)
{
    return s_event_queue;
}

bool board_input_read(board_input_action_t *action, TickType_t timeout)
{
    return s_event_queue != NULL && action != NULL &&
           xQueueReceive(s_event_queue, action, timeout) == pdTRUE;
}
#endif
