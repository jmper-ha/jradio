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
};
static int s_encoder_left = BOARD_GPIO_NOT_WIRED;
static int s_encoder_right = BOARD_GPIO_NOT_WIRED;
static board_encoder_decoder_t s_encoder_decoder;
/* The encoder in hardware. Polling the two lines every 5 ms lost detents on a
 * quick turn: four transitions a detent at forty detents a second is one every
 * 6 ms, two of them fell between samples, and the software decoder threw the
 * whole detent away on the jump. The pulse counter sees every edge whenever
 * this task gets to run, and contact bounce on one line while the other holds
 * counts up and down again in quadrature, so it cancels. NULL when the
 * counter could not be set up; the software decoder then does the job as
 * before. */
static pcnt_unit_handle_t s_encoder_pcnt;
static int s_encoder_count_seen;
static int32_t s_encoder_pending;
/* Far enough out that a count never reaches them between two reads; the unit
 * accumulates across them anyway, so the count read is continuous. */
#define ENCODER_PCNT_LIMIT 30000
/* Spikes shorter than this are not edges. The counter's filter tops out a
 * little under 13 us at the APB clock; contact bounce is longer and is dealt
 * with by the quadrature counting above. */
#define ENCODER_PCNT_GLITCH_NS 10000

static void board_input_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        if (s_encoder_pcnt != NULL) {
            int count = s_encoder_count_seen;
            if (pcnt_unit_get_count(s_encoder_pcnt, &count) == ESP_OK) {
                int32_t detents = board_encoder_detents_take(&s_encoder_pending,
                                                             (int32_t)(count - s_encoder_count_seen));
                s_encoder_count_seen = count;
                for (; detents != 0; detents += detents > 0 ? -1 : 1) {
                    const board_input_action_t step = detents > 0
                                                          ? BOARD_INPUT_ACTION_ENCODER_RIGHT
                                                          : BOARD_INPUT_ACTION_ENCODER_LEFT;
                    if (xQueueSend(s_event_queue, &step, 0) != pdTRUE) {
                        ESP_LOGW(TAG, "input queue full; action=%d dropped", (int)step);
                    }
                }
            }
        }
        const board_input_action_t encoder_action =
            s_encoder_pcnt != NULL
                ? BOARD_INPUT_ACTION_NONE
                : board_encoder_decoder_update(&s_encoder_decoder, gpio_get_level(s_encoder_left),
                                               gpio_get_level(s_encoder_right));
        if (encoder_action != BOARD_INPUT_ACTION_NONE &&
            xQueueSend(s_event_queue, &encoder_action, 0) != pdTRUE) {
            ESP_LOGW(TAG, "input queue full; action=%d dropped", (int)encoder_action);
        }
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
    if (decoder == NULL) {
        return;
    }
    decoder->last_state = board_encoder_state_from_levels(left_level, right_level);
    decoder->transition_sum = 0;
}

int32_t board_encoder_detents_take(int32_t *pending, int32_t counted)
{
    if (pending == NULL) return 0;
    *pending += counted;
    /* Toward zero, so a turn that stopped short of a detent leaves its partial
     * count to be finished by the next movement - or undone by going back -
     * rather than rounded into a step nobody made. */
    const int32_t detents = *pending / 4;
    *pending -= detents * 4;
    return detents;
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
/* A bit for gpio_config's mask, or none for a line that is not wired. */
static uint64_t board_input_pin_bit(int gpio_num)
{
    return gpio_num == BOARD_GPIO_NOT_WIRED ? 0ULL : 1ULL << gpio_num;
}

/* Two channels for x4 quadrature, signed to match the software decoder's
 * table: a rise on the right line while the left is low counts up, as does a
 * fall on the right line while the left is high - with both lines idling high
 * on their pull-ups, a detent to the right is +4. */
static esp_err_t board_encoder_pcnt_start(int left, int right)
{
    const pcnt_unit_config_t unit_config = {
        .low_limit = -ENCODER_PCNT_LIMIT,
        .high_limit = ENCODER_PCNT_LIMIT,
        .flags.accum_count = true,
    };
    pcnt_unit_handle_t unit = NULL;
    esp_err_t err = pcnt_new_unit(&unit_config, &unit);
    if (err != ESP_OK) return err;
    const pcnt_glitch_filter_config_t filter = {.max_glitch_ns = ENCODER_PCNT_GLITCH_NS};
    const pcnt_chan_config_t on_right = {.edge_gpio_num = right, .level_gpio_num = left};
    const pcnt_chan_config_t on_left = {.edge_gpio_num = left, .level_gpio_num = right};
    pcnt_channel_handle_t right_channel = NULL;
    pcnt_channel_handle_t left_channel = NULL;
    if ((err = pcnt_unit_set_glitch_filter(unit, &filter)) != ESP_OK ||
        (err = pcnt_new_channel(unit, &on_right, &right_channel)) != ESP_OK ||
        (err = pcnt_new_channel(unit, &on_left, &left_channel)) != ESP_OK ||
        (err = pcnt_channel_set_edge_action(right_channel, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                            PCNT_CHANNEL_EDGE_ACTION_INCREASE)) != ESP_OK ||
        (err = pcnt_channel_set_level_action(right_channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                             PCNT_CHANNEL_LEVEL_ACTION_INVERSE)) != ESP_OK ||
        (err = pcnt_channel_set_edge_action(left_channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                            PCNT_CHANNEL_EDGE_ACTION_DECREASE)) != ESP_OK ||
        (err = pcnt_channel_set_level_action(left_channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                             PCNT_CHANNEL_LEVEL_ACTION_INVERSE)) != ESP_OK ||
        (err = pcnt_unit_add_watch_point(unit, ENCODER_PCNT_LIMIT)) != ESP_OK ||
        (err = pcnt_unit_add_watch_point(unit, -ENCODER_PCNT_LIMIT)) != ESP_OK ||
        (err = pcnt_unit_enable(unit)) != ESP_OK ||
        (err = pcnt_unit_clear_count(unit)) != ESP_OK ||
        (err = pcnt_unit_start(unit)) != ESP_OK) {
        if (left_channel != NULL) (void)pcnt_del_channel(left_channel);
        if (right_channel != NULL) (void)pcnt_del_channel(right_channel);
        (void)pcnt_del_unit(unit);
        return err;
    }
    s_encoder_pcnt = unit;
    s_encoder_count_seen = 0;
    s_encoder_pending = 0;
    return ESP_OK;
}

esp_err_t board_input_init(void)
{
    const board_config_t *wiring = board_config_get();
    s_encoder_left = wiring->encoder_left;
    s_encoder_right = wiring->encoder_right;
    // In s_channels' order.
    const int8_t buttons[] = {wiring->encoder_button, wiring->button_sleep,
                              wiring->button_quick_menu, wiring->button_prev,
                              wiring->button_next};
    for (size_t index = 0; index < sizeof(s_channels) / sizeof(s_channels[0]); ++index) {
        s_channels[index].gpio_num = buttons[index] >= 0 ? buttons[index] : BOARD_GPIO_NOT_WIRED;
    }
    /* Two calls rather than one: the encoder and the buttons want different
     * pull-up settings, and gpio_config() applies one setting to every pin in
     * its mask. The encoder button belongs to the encoder here - it is the
     * same part and the same external pull-up. */
    const gpio_config_t encoder_config = {
        .pin_bit_mask = (1ULL << wiring->encoder_right) | (1ULL << wiring->encoder_left) |
                        (1ULL << wiring->encoder_button),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = wiring->encoder_pullups ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t button_config = {
        .pin_bit_mask = board_input_pin_bit(s_channels[1].gpio_num) |
                        board_input_pin_bit(s_channels[2].gpio_num) |
                        board_input_pin_bit(s_channels[3].gpio_num) |
                        board_input_pin_bit(s_channels[4].gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = wiring->buttons_pullups ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t result = gpio_config(&encoder_config);
    if (result != ESP_OK) {
        return result;
    }
    /* An empty mask is an argument error to gpio_config, not a no-op, so a
     * board with no function buttons at all skips the call. */
    if (button_config.pin_bit_mask != 0ULL) {
        result = gpio_config(&button_config);
        if (result != ESP_OK) {
            return result;
        }
    }
    if (s_event_queue != NULL) {
        return ESP_OK;
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
    }
    board_encoder_decoder_init(&s_encoder_decoder, gpio_get_level(s_encoder_left),
                               gpio_get_level(s_encoder_right));
    if (s_encoder_left >= 0 && s_encoder_right >= 0) {
        const esp_err_t counted = board_encoder_pcnt_start(s_encoder_left, s_encoder_right);
        if (counted != ESP_OK) {
            ESP_LOGW(TAG, "encoder pulse counter unavailable (%s); polling the lines instead",
                     esp_err_to_name(counted));
        }
    }
    if (xTaskCreate(board_input_task, "board_input", 3072, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "input task started; poll=%d ms debounce=%d ms encoder=%s", INPUT_POLL_MS,
             INPUT_DEBOUNCE_MS, s_encoder_pcnt != NULL ? "pcnt" : "polled");
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
