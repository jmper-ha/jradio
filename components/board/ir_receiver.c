#include "ir_receiver.h"

#include "board_options.h"

#ifdef IR_RECEIVER_GPIO

#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "ir";

/* The receiver's output is the carrier's envelope, idle high and low for a
 * mark, measured in microseconds: NEC's shortest element is 562 us and its
 * leader 9 ms, so a 1 MHz clock and a 16-bit duration cover everything. */
#define IR_RESOLUTION_HZ 1000000U
/* A frame ends when the line stays idle this long. NEC's longest space inside
 * a frame is 4.5 ms; its repeat comes 40 ms after the frame, which is what
 * has to be seen as a new one. */
#define IR_FRAME_GAP_NS 12000000U
/* Below this a pulse is a glitch. The filter counts APB ticks, eight bits of
 * them, so the most it can be is about 3 us at 80 MHz - not the 100 us that
 * looked right for a signal whose shortest element is 562 us, and which the
 * driver refused with "signal_range_min_ns too big". A receiver's output has
 * no sub-microsecond glitches anyway; the filter is for the wire. */
#define IR_GLITCH_NS 2000U
/* The S3's RMT memory holds 48 symbols per channel, and a NEC frame is 34 -
 * a longer protocol is cut at 48, which is still a stable signature for the
 * raw hash. */
#define IR_SYMBOLS 48U
/* The listener saves remote.csv on this task after every key learned, and a
 * LittleFS write through the VFS goes deeper than the decoding does: 3072
 * overflowed on the bench, on the sixth key learned in a row. */
#define IR_TASK_STACK 5120U
#define IR_TASK_PRIORITY 5

static rmt_channel_handle_t s_channel;
static QueueHandle_t s_frames;
static rmt_symbol_word_t s_symbols[IR_SYMBOLS];
static ir_receiver_listener_t s_listener;
static void *s_listener_context;

static bool ir_on_frame(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *frame,
                        void *context)
{
    (void)channel;
    (void)context;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_frames, frame, &woken);
    return woken == pdTRUE;
}

static void ir_task(void *arg)
{
    (void)arg;
    const rmt_receive_config_t receive = {
        .signal_range_min_ns = IR_GLITCH_NS,
        .signal_range_max_ns = IR_FRAME_GAP_NS,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_receive(s_channel, s_symbols, sizeof(s_symbols), &receive));
    rmt_rx_done_event_data_t frame;
    ir_pulse_t pulses[IR_SYMBOLS];
    while (true) {
        if (xQueueReceive(s_frames, &frame, portMAX_DELAY) != pdTRUE) continue;
        size_t count = 0U;
        for (size_t index = 0U; index < frame.num_symbols && count < IR_SYMBOLS; ++index) {
            const rmt_symbol_word_t symbol = frame.received_symbols[index];
            /* The receiver pulls the line low for a mark, so level0 is the
             * mark's level; RMT hands the pair in the order it saw them. */
            const uint16_t first = (uint16_t)symbol.duration0;
            const uint16_t second = (uint16_t)symbol.duration1;
            pulses[count++] = symbol.level0 == 0
                                  ? (ir_pulse_t){.mark_us = first, .space_us = second}
                                  : (ir_pulse_t){.mark_us = second, .space_us = first};
        }
        /* Re-arm before decoding so the repeat frame 40 ms behind this one is
         * not lost to the log line. */
        ESP_ERROR_CHECK_WITHOUT_ABORT(rmt_receive(s_channel, s_symbols, sizeof(s_symbols), &receive));

        const ir_code_t code = ir_decode(pulses, count);
        char text[24];
        ir_code_format(&code, text, sizeof(text));
        if (code.kind == IR_CODE_NONE) {
            ESP_LOGD(TAG, "%u pulses, not a key", (unsigned)count);
        } else if (code.kind == IR_CODE_RAW) {
            /* The first pulses beside the hash, so a new remote's protocol can
             * be recognised from the log alone. */
            ESP_LOGI(TAG, "%s (%u pulses: %u/%u %u/%u %u/%u ...)", text, (unsigned)count,
                     pulses[0].mark_us, pulses[0].space_us, pulses[1].mark_us,
                     pulses[1].space_us, pulses[2].mark_us, pulses[2].space_us);
        } else {
            ESP_LOGI(TAG, "%s", text);
        }
        if (s_listener != NULL && code.kind != IR_CODE_NONE) s_listener(&code, s_listener_context);
    }
}

esp_err_t ir_receiver_init(void)
{
    /* Started once: the remote's wake check runs it before the board exists,
     * and board_init() asks again a moment later. */
    if (s_channel != NULL) return ESP_OK;
    const rmt_rx_channel_config_t config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = IR_SYMBOLS,
        .gpio_num = IR_RECEIVER_GPIO,
        .flags = {.invert_in = false, .with_dma = false, .io_loop_back = false},
    };
    esp_err_t result = rmt_new_rx_channel(&config, &s_channel);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "RMT channel on GPIO %d: %s", IR_RECEIVER_GPIO, esp_err_to_name(result));
        return result;
    }
    s_frames = xQueueCreate(4, sizeof(rmt_rx_done_event_data_t));
    if (s_frames == NULL) return ESP_ERR_NO_MEM;
    const rmt_rx_event_callbacks_t callbacks = {.on_recv_done = ir_on_frame};
    result = rmt_rx_register_event_callbacks(s_channel, &callbacks, NULL);
    if (result != ESP_OK) return result;
    result = rmt_enable(s_channel);
    if (result != ESP_OK) return result;
    if (xTaskCreatePinnedToCore(ir_task, "ir", IR_TASK_STACK, NULL, IR_TASK_PRIORITY, NULL, 0) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    /* A bare receiver's output is open-collector on some parts, and a module
     * may or may not carry its own pull-up; the chip's costs nothing. */
    (void)gpio_pullup_en(IR_RECEIVER_GPIO);
    ESP_LOGI(TAG, "receiver on GPIO %d, idle level %d", IR_RECEIVER_GPIO,
             gpio_get_level(IR_RECEIVER_GPIO));
    return ESP_OK;
}

void ir_receiver_set_listener(ir_receiver_listener_t listener, void *context)
{
    s_listener_context = context;
    s_listener = listener;
}

#else

esp_err_t ir_receiver_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void ir_receiver_set_listener(ir_receiver_listener_t listener, void *context)
{
    (void)listener;
    (void)context;
}

#endif
