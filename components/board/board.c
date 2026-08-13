#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board.h"
#include "board_audio_health.h"
#include "board_audio_startup.h"
#include "board_config.h"
#include "board_input.h"
#include "pcm_diagnostics.h"

#define BOARD_LCD_HOST SPI2_HOST
#define BOARD_LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
#define BOARD_LCD_DRAW_LINES 20
#define BOARD_BACKLIGHT_PWM_HZ 5000
#define BOARD_BACKLIGHT_DUTY_RES LEDC_TIMER_13_BIT
#define BOARD_BACKLIGHT_MAX_DUTY ((1U << BOARD_BACKLIGHT_DUTY_RES) - 1U)
#define BOARD_I2S_BYTES_PER_FRAME \
    (BOARD_AUDIO_CHANNEL_COUNT * (BOARD_AUDIO_BITS_PER_SAMPLE / 8U))
#define BOARD_I2S_DMA_BUFFER_BYTES (BOARD_I2S_DMA_FRAME_NUM * BOARD_I2S_BYTES_PER_FRAME)
#define BOARD_AUDIO_HEALTH_REPORT_MS 10000U

static const char *TAG = "board";
static uint16_t s_draw_buffer[BOARD_TFT_WIDTH * BOARD_LCD_DRAW_LINES];
static uint16_t s_fill_buffer[BOARD_TFT_WIDTH * BOARD_LCD_DRAW_LINES];
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_lcd_transfer_done;
static i2s_chan_handle_t s_i2s_tx;
static SemaphoreHandle_t s_audio_mutex;
static bool s_audio_enabled;
static const uint8_t s_i2s_silence[BOARD_I2S_DMA_BUFFER_BYTES];
/* Output accounting lives here rather than in each player because this is the
 * one point both the radio and the USB player pass through, and because a
 * silent-but-running fault has to be told apart from a stalled one without
 * either player noticing anything is wrong. Guarded by s_audio_mutex. */
static board_audio_health_t s_audio_health;
static TickType_t s_audio_health_started;
static unsigned int s_audio_health_underruns;
static bool s_audio_gap_reported;
static bool s_audio_mute_reported;
static uint32_t s_audio_zero_carry;
static uint32_t s_audio_sample_rate = BOARD_AUDIO_DEFAULT_SAMPLE_RATE;

typedef struct {
    TaskHandle_t caller;
    uint32_t transitions[3];
    bool saw_low[3];
    bool saw_high[3];
} board_audio_probe_t;

static void board_audio_probe_task(void *arg)
{
    board_audio_probe_t *probe = arg;
    const gpio_num_t pins[] = {
        BOARD_I2S_DOUT_GPIO,
        BOARD_I2S_BCLK_GPIO,
        BOARD_I2S_LRCK_GPIO,
    };
    int previous[3];

    for (size_t index = 0; index < 3U; ++index) {
        previous[index] = gpio_get_level(pins[index]);
    }
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(500);
    while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
        for (size_t index = 0; index < 3U; ++index) {
            const int level = gpio_get_level(pins[index]);
            probe->saw_low[index] |= level == 0;
            probe->saw_high[index] |= level != 0;
            if (level != previous[index]) {
                probe->transitions[index]++;
                previous[index] = level;
            }
        }
    }
    xTaskNotifyGive(probe->caller);
    vTaskDelete(NULL);
}

static bool IRAM_ATTR board_lcd_color_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                                     esp_lcd_panel_io_event_data_t *edata,
                                                     void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}

esp_err_t board_backlight_set(uint8_t percent)
{
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t duty = (BOARD_BACKLIGHT_MAX_DUTY * percent) / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty), TAG,
                        "set backlight duty failed");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static esp_err_t board_backlight_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BOARD_BACKLIGHT_DUTY_RES,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BOARD_BACKLIGHT_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel_config = {
        .gpio_num = BOARD_TFT_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };

    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "configure PWM timer failed");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "configure PWM channel failed");
    return board_backlight_set(0);
}

static atomic_uint s_audio_underruns = ATOMIC_VAR_INIT(0U);

static IRAM_ATTR bool board_audio_on_send_q_ovf(i2s_chan_handle_t handle,
                                                i2s_event_data_t *event, void *context)
{
    (void)handle;
    (void)event;
    (void)context;
    atomic_fetch_add_explicit(&s_audio_underruns, 1U, memory_order_relaxed);
    return false;
}

unsigned int board_audio_underrun_count(void)
{
    return atomic_load_explicit(&s_audio_underruns, memory_order_relaxed);
}

/* Call under s_audio_mutex whenever output is (re)started. A window that
 * straddles a stopped period measures real time against bytes that were never
 * meant to flow, and reads as a fault that is not there. */
static void board_audio_health_rearm(void)
{
    board_audio_health_reset(&s_audio_health);
    s_audio_health_started = xTaskGetTickCount();
    s_audio_health_underruns = board_audio_underrun_count();
    s_audio_gap_reported = false;
    s_audio_mute_reported = false;
    /* Not the zero-run carry: a run in flight belongs to the audio, not to the
     * reporting window, and resetting it here would hide exactly the run that
     * straddles a window boundary. */
}

static esp_err_t board_audio_init(void)
{
    s_audio_mutex = xSemaphoreCreateMutex();
    if (s_audio_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = BOARD_I2S_DMA_DESC_NUM;
    channel_config.dma_frame_num = BOARD_I2S_DMA_FRAME_NUM;
    channel_config.auto_clear_after_cb = BOARD_I2S_AUTO_CLEAR_AFTER_CB != 0;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &s_i2s_tx, NULL), TAG,
                        "create I2S TX channel failed");

    /* A TX "send queue overflow" means the DMA ran out of filled descriptors
     * and replayed/zeroed a buffer - i.e. an audible dropout. Nothing else in
     * the system notices this, so count it here and let callers poll. */
    const i2s_event_callbacks_t audio_callbacks = {
        .on_send_q_ovf = board_audio_on_send_q_ovf,
    };
    ESP_RETURN_ON_ERROR(i2s_channel_register_event_callback(s_i2s_tx, &audio_callbacks, NULL),
                        TAG, "register I2S TX callbacks failed");

    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BOARD_AUDIO_DEFAULT_SAMPLE_RATE),
        /* PCM5102 uses the Philips I2S timing: audio data is delayed by one
         * BCLK after the LRCK edge.  MSB/left-justified timing shifts every
         * sample by one bit and is not the interface selected on this board. */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                         I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOARD_I2S_BCLK_GPIO,
            .ws = BOARD_I2S_LRCK_GPIO,
            .dout = BOARD_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    /* PCM5102 supports a 32 x Fs BCLK with 16-bit stereo I2S. Keeping the
     * slot equal to the decoded sample width avoids padding and halves the
     * BCLK edge rate compared with 32-bit slots. */
    std_config.slot_cfg.slot_bit_width = (i2s_slot_bit_width_t)BOARD_I2S_SLOT_BIT_WIDTH;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std_config), TAG,
                        "configure I2S TX failed");
    ESP_RETURN_ON_ERROR(gpio_input_enable(BOARD_I2S_DOUT_GPIO), TAG,
                        "enable I2S DOUT pad observation failed");
    ESP_RETURN_ON_ERROR(gpio_input_enable(BOARD_I2S_BCLK_GPIO), TAG,
                        "enable I2S BCLK pad observation failed");
    ESP_RETURN_ON_ERROR(gpio_input_enable(BOARD_I2S_LRCK_GPIO), TAG,
                        "enable I2S LRCK pad observation failed");
    ESP_LOGI(TAG,
             "PCM5102 I2S: Philips, 16-bit stereo in %u-bit slots, BCLK=%uxFs, "
             "DMA=%ux%u frames (%lu ms)",
             BOARD_I2S_SLOT_BIT_WIDTH, BOARD_I2S_SLOT_BIT_WIDTH * BOARD_AUDIO_CHANNEL_COUNT,
             BOARD_I2S_DMA_DESC_NUM, BOARD_I2S_DMA_FRAME_NUM,
             (unsigned long)((BOARD_I2S_DMA_DESC_NUM * BOARD_I2S_DMA_FRAME_NUM * 1000U) /
                             BOARD_AUDIO_DEFAULT_SAMPLE_RATE));
    /* Keep TX in READY state. It is preloaded and enabled only when an audio
     * source starts, so the DAC never starts from empty DMA descriptors. */
    s_audio_enabled = false;
    return ESP_OK;
}

static esp_err_t board_audio_preload_silence(void)
{
#if BOARD_I2S_PRELOAD_SILENCE
    size_t total_loaded = 0U;
    for (size_t descriptor = 0; descriptor < BOARD_I2S_DMA_DESC_NUM; ++descriptor) {
        size_t loaded = 0U;
        ESP_RETURN_ON_ERROR(i2s_channel_preload_data(s_i2s_tx, s_i2s_silence,
                                                      sizeof(s_i2s_silence), &loaded),
                            TAG, "preload I2S silence failed");
        total_loaded += loaded;
        if (loaded != sizeof(s_i2s_silence)) {
            break;
        }
    }
    const size_t expected = board_audio_startup_silence_bytes(BOARD_I2S_DMA_DESC_NUM,
                                                              sizeof(s_i2s_silence));
    if (total_loaded != expected) {
        ESP_LOGE(TAG, "I2S silence preload incomplete: loaded=%u expected=%u",
                 (unsigned int)total_loaded, (unsigned int)expected);
        return ESP_FAIL;
    }
#endif
    return ESP_OK;
}

esp_err_t board_audio_write(const void *pcm, size_t pcm_length, size_t *written,
                            uint32_t timeout_ms)
{
    if (pcm == NULL || written == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_i2s_tx == NULL || s_audio_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_audio_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_ERR_INVALID_STATE;
    board_audio_health_t window = {0};
    uint32_t window_ms = 0U;
    unsigned int window_underruns = 0U;
    uint32_t window_rate = 0U;
    unsigned int gap_count = 0U;
    uint32_t mute_run = 0U;
    bool report = false;
    bool mark_gap = false;
    bool mark_silence = false;
    bool mark_mute = false;
    if (s_audio_enabled) {
        result = i2s_channel_write(s_i2s_tx, pcm, pcm_length, written,
                                   pdMS_TO_TICKS(timeout_ms));
        if (result == ESP_OK) {
            board_audio_health_add(&s_audio_health, *written, pcm_length,
                                   pcm_s16le_peak(pcm, *written));
            const uint32_t zero_run =
                pcm_s16le_zero_frame_run(pcm, *written, &s_audio_zero_carry);
            board_audio_health_note_zero_run(&s_audio_health, zero_run);
            if (!s_audio_mute_reported && zero_run >= BOARD_AUDIO_ZERO_DETECT_FRAMES) {
                s_audio_mute_reported = true;
                mute_run = zero_run;
                mark_mute = true;
            }
            /* The 10 s window only says a fault happened somewhere inside it,
             * which is too coarse to line up against what the listener heard.
             * These pin the moment instead. Only the first occurrence of each
             * kind per window is marked, so a long silence cannot flood the
             * UART and stall the pipeline it is measuring. */
            mark_silence = s_audio_health.silent_writes == 1U;
            if (!s_audio_gap_reported &&
                board_audio_underrun_count() != s_audio_health_underruns) {
                gap_count = board_audio_underrun_count() - s_audio_health_underruns;
                s_audio_gap_reported = true;
                mark_gap = true;
            }
            const TickType_t now = xTaskGetTickCount();
            const uint32_t elapsed =
                (uint32_t)(now - s_audio_health_started) * portTICK_PERIOD_MS;
            if (elapsed >= BOARD_AUDIO_HEALTH_REPORT_MS) {
                window = s_audio_health;
                window_ms = elapsed;
                window_rate = s_audio_sample_rate;
                const unsigned int underruns = board_audio_underrun_count();
                window_underruns = underruns - s_audio_health_underruns;
                board_audio_health_rearm();
                report = true;
            }
        }
    }
    xSemaphoreGive(s_audio_mutex);
    /* Logged outside the lock: ESP_LOG writes to the UART at 115200 baud, and
     * this line is long enough that holding the audio mutex across it would
     * stall the very pipeline being measured. */
    if (mark_gap) {
        ESP_LOGW(TAG, "audio gap: the DMA ran dry (%u buffer%s)", gap_count,
                 gap_count == 1U ? "" : "s");
    }
    if (mark_silence) {
        ESP_LOGW(TAG, "audio silence: an all-zero block reached the DAC");
    }
    if (mark_mute) {
        ESP_LOGW(TAG,
                 "audio zero-run: %u consecutive zero frames - past the PCM5102 "
                 "zero-data detect at %u, so the DAC has muted its analog output",
                 (unsigned int)mute_run, (unsigned int)BOARD_AUDIO_ZERO_DETECT_FRAMES);
    }
    if (report) {
        ESP_LOGI(TAG,
                 "audio health: realtime=%u%% peak=%u silent=%u zero_run=%u underruns=%u "
                 "writes=%u short=%u bytes=%u rate=%u",
                 board_audio_health_realtime_percent(window.bytes, window_rate, window_ms),
                 (unsigned int)window.peak, window.silent_writes,
                 (unsigned int)window.max_zero_run, window_underruns,
                 window.writes, window.short_writes, (unsigned int)window.bytes,
                 (unsigned int)window_rate);
    }
    return result;
}

esp_err_t board_audio_start(const void *pcm, size_t pcm_length, size_t *preloaded)
{
    if (pcm == NULL || pcm_length == 0U || preloaded == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_i2s_tx == NULL || s_audio_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    *preloaded = 0U;
    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (!s_audio_enabled) {
        result = board_audio_preload_silence();
        if (result == ESP_OK) {
            result = i2s_channel_enable(s_i2s_tx);
        }
        if (result == ESP_OK) {
            s_audio_enabled = true;
            board_audio_health_rearm();
            result = i2s_channel_write(s_i2s_tx, pcm, pcm_length, preloaded,
                                       pdMS_TO_TICKS(1000));
        }
        if (result == ESP_OK && *preloaded == pcm_length) {
            ESP_LOGI(TAG,
                     "PCM5102 I2S output enabled after %u-byte silent clock pre-roll",
                     (unsigned int)board_audio_startup_silence_bytes(
                         BOARD_I2S_DMA_DESC_NUM, sizeof(s_i2s_silence)));
        } else if (s_audio_enabled) {
            (void)i2s_channel_disable(s_i2s_tx);
            s_audio_enabled = false;
            if (result == ESP_OK) {
                result = ESP_FAIL;
            }
        }
    }
    xSemaphoreGive(s_audio_mutex);
    return result;
}

esp_err_t board_audio_set_enabled(bool enabled)
{
    if (s_i2s_tx == NULL || s_audio_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    esp_err_t result = ESP_OK;
    if (enabled != s_audio_enabled) {
        if (enabled) {
            result = board_audio_preload_silence();
            if (result == ESP_OK) {
                result = i2s_channel_enable(s_i2s_tx);
            }
        } else {
            result = i2s_channel_disable(s_i2s_tx);
        }
        if (result == ESP_OK) {
            s_audio_enabled = enabled;
            if (enabled) {
                board_audio_health_rearm();
            }
            ESP_LOGI(TAG, "PCM5102 I2S output %s", enabled ? "enabled" : "disabled");
        }
    }
    xSemaphoreGive(s_audio_mutex);
    return result;
}

esp_err_t board_audio_set_sample_rate(uint32_t sample_rate)
{
    if (sample_rate == 0U || s_i2s_tx == NULL || s_audio_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const i2s_std_clk_config_t clock_config = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    const bool was_enabled = s_audio_enabled;
    esp_err_t result = ESP_OK;
    if (was_enabled) {
        result = i2s_channel_disable(s_i2s_tx);
        if (result == ESP_OK) {
            s_audio_enabled = false;
        }
    }
    if (result == ESP_OK) {
        result = i2s_channel_reconfig_std_clock(s_i2s_tx, &clock_config);
    }
    if (result == ESP_OK) {
        // The health window measures bytes against real time, so it is only
        // meaningful against the rate those bytes are actually clocked at.
        s_audio_sample_rate = sample_rate;
        board_audio_health_rearm();
    }
    if (result == ESP_OK && was_enabled) {
        result = i2s_channel_enable(s_i2s_tx);
        if (result == ESP_OK) {
            s_audio_enabled = true;
        }
    }
    xSemaphoreGive(s_audio_mutex);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "set I2S sample rate to %lu Hz failed: %s", (unsigned long)sample_rate,
                 esp_err_to_name(result));
    }
    return result;
}

esp_err_t board_audio_self_test(uint32_t duration_ms)
{
    uint8_t pcm[512];
    uint32_t phase = 0;
    size_t frames_remaining = ((size_t)BOARD_AUDIO_DEFAULT_SAMPLE_RATE * duration_ms) / 1000U;
    board_audio_probe_t probe = {
        .caller = xTaskGetCurrentTaskHandle(),
    };
    bool output_started = false;
    esp_err_t result = ESP_OK;

    if (duration_ms == 0U || s_i2s_tx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGW(TAG, "starting PCM5102 self-test: 1000 Hz, %lu ms",
             (unsigned long)duration_ms);
    if (xTaskCreatePinnedToCore(board_audio_probe_task, "i2s_probe", 2048, &probe, 10, NULL, 1) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    while (frames_remaining > 0U) {
        const size_t buffer_frames = sizeof(pcm) / 4U;
        const size_t frames = frames_remaining < buffer_frames ? frames_remaining : buffer_frames;
        const size_t generated = pcm_s16le_stereo_square_fill(
            pcm, frames * 4U, BOARD_AUDIO_DEFAULT_SAMPLE_RATE, 1000U, 12000, &phase);
        size_t written = 0;
        if (!output_started) {
            result = board_audio_start(pcm, generated, &written);
            output_started = result == ESP_OK;
        } else {
            result = board_audio_write(pcm, generated, &written, 1000);
        }
        if (result != ESP_OK) {
            goto cleanup;
        }
        if (written != generated) {
            result = ESP_FAIL;
            goto cleanup;
        }
        frames_remaining -= generated / 4U;
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0U) {
        ESP_LOGE(TAG, "I2S pad activity probe timed out");
        result = ESP_ERR_TIMEOUT;
        goto cleanup;
    }
    ESP_LOGW(TAG,
             "I2S pad activity: DOUT edges=%lu levels=%d%d, BCLK edges=%lu levels=%d%d, "
             "LRCK edges=%lu levels=%d%d",
             (unsigned long)probe.transitions[0], probe.saw_low[0], probe.saw_high[0],
             (unsigned long)probe.transitions[1], probe.saw_low[1], probe.saw_high[1],
             (unsigned long)probe.transitions[2], probe.saw_low[2], probe.saw_high[2]);
    ESP_LOGW(TAG, "PCM5102 self-test complete");
    result = ESP_OK;

cleanup:
    if (output_started) {
        const esp_err_t disable_result = board_audio_set_enabled(false);
        if (result == ESP_OK && disable_result != ESP_OK) {
            result = disable_result;
        }
    }
    return result;
}

esp_err_t board_display_draw_rgb565(int x1, int y1, int x2, int y2, const uint16_t *pixels)
{
    if (s_panel == NULL || pixels == NULL || x1 < 0 || y1 < 0 || x2 <= x1 || y2 <= y1 ||
        x2 > BOARD_TFT_WIDTH || y2 > BOARD_TFT_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    const int width = x2 - x1;
    for (int y = y1; y < y2; y += BOARD_LCD_DRAW_LINES) {
        const int lines = (y2 - y) < BOARD_LCD_DRAW_LINES ? (y2 - y) : BOARD_LCD_DRAW_LINES;
        for (int index = 0; index < width * lines; ++index) {
            const int source_row = (y - y1) + index / width;
            s_draw_buffer[index] = __builtin_bswap16(pixels[source_row * width + (index % width)]);
        }
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_panel, x1, y, x2, y + lines, s_draw_buffer), TAG,
                            "draw display rectangle failed");
        if (BOARD_LCD_WAIT_FOR_TRANSFER &&
            xSemaphoreTake(s_lcd_transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "timed out waiting for LCD DMA transfer");
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

static esp_err_t board_display_fill(uint16_t rgb565)
{
    for (size_t index = 0; index < BOARD_TFT_WIDTH * BOARD_LCD_DRAW_LINES; ++index) {
        s_fill_buffer[index] = rgb565;
    }
    for (int y = 0; y < BOARD_TFT_HEIGHT; y += BOARD_LCD_DRAW_LINES) {
        const int y_end = (y + BOARD_LCD_DRAW_LINES < BOARD_TFT_HEIGHT) ?
                              y + BOARD_LCD_DRAW_LINES : BOARD_TFT_HEIGHT;
        ESP_RETURN_ON_ERROR(board_display_draw_rgb565(0, y, BOARD_TFT_WIDTH, y_end, s_fill_buffer), TAG,
                            "draw display test pattern failed");
    }
    return ESP_OK;
}

static esp_err_t board_display_init(void)
{
    s_lcd_transfer_done = xSemaphoreCreateBinary();
    if (s_lcd_transfer_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t bus_config = {
        .sclk_io_num = BOARD_TFT_SCLK_GPIO,
        .mosi_io_num = BOARD_TFT_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_TFT_WIDTH * BOARD_LCD_DRAW_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG,
                        "initialize LCD SPI bus failed");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BOARD_TFT_DC_GPIO,
        .cs_gpio_num = BOARD_TFT_CS_GPIO,
        .pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = board_lcd_color_transfer_done,
        .user_ctx = s_lcd_transfer_done,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_HOST,
                                                  &io_config, &io_handle), TAG,
                        "create LCD SPI I/O failed");

    esp_lcd_panel_handle_t panel = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = BOARD_TFT_RGB_ORDER_BGR ? LCD_RGB_ELEMENT_ORDER_BGR :
                                                   LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel), TAG,
                        "create ILI9341 panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset ILI9341 failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "initialize ILI9341 failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel, BOARD_TFT_SWAP_XY), TAG,
                        "set landscape rotation failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel, BOARD_TFT_MIRROR_X, BOARD_TFT_MIRROR_Y), TAG,
                        "set LCD mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "turn LCD on failed");
    s_panel = panel;
    return board_display_fill(0x001F);
}

esp_err_t board_init(void)
{
    ESP_LOGI(TAG, "initializing input, PWM backlight, I2S and ILI9341");
    ESP_RETURN_ON_ERROR(board_input_init(), TAG, "configure input GPIOs failed");
    ESP_RETURN_ON_ERROR(board_backlight_init(), TAG, "initialize backlight failed");
    ESP_RETURN_ON_ERROR(board_audio_init(), TAG, "initialize PCM5102 I2S output failed");
    ESP_RETURN_ON_ERROR(board_display_init(), TAG, "initialize ILI9341 failed");
    ESP_RETURN_ON_ERROR(board_backlight_set(50), TAG, "set initial backlight failed");
    ESP_LOGI(TAG, "board initialized; display shows solid blue test screen");
    return ESP_OK;
}
