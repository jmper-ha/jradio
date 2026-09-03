#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "audio_volume.h"
#include "board.h"
#include "board_audio_health.h"
#include "board_audio_startup.h"
#include "board_audio_format.h"
#include "board_display_profile.h"
#include "display/board_panel.h"
#include "boot_splash.h"
#include "board_options.h"
#include "board_input.h"
#include "pcm_diagnostics.h"

/* Parts this file has code for. Selecting anything else in board_options.h
 * fails the build rather than initialising a device that then misbehaves. */
/* The SPI host enum is not numbered by peripheral, so map it explicitly. */
#if DISPLAY_SPI_PERIPHERAL == 2
#define LCD_HOST SPI2_HOST
#elif DISPLAY_SPI_PERIPHERAL == 3
#define LCD_HOST SPI3_HOST
#else
#error "unsupported DISPLAY_SPI_PERIPHERAL"
#endif

/* Height of one flush band, and whether to block until the panel has taken it.
 * Both are firmware tradeoffs rather than facts about the hardware: LVGL is
 * given a single draw buffer, so the flush must complete before that buffer is
 * reused. */
#define LCD_DRAW_LINES 20
#define LCD_WAIT_FOR_TRANSFER 1
/* Zero the DMA buffer after each callback so a stalled writer clocks out
 * silence rather than replaying the last block. */
#define I2S_AUTO_CLEAR_AFTER_CB 1
/* ledc_timer_bit_t enumerators are the bit counts themselves, which lets the
 * option stay a plain number. Verified rather than assumed. */
_Static_assert(LEDC_TIMER_13_BIT == 13,
               "ledc_timer_bit_t no longer equals its bit count");
#define BACKLIGHT_DUTY_RES ((ledc_timer_bit_t)BACKLIGHT_DUTY_BITS)
#define BACKLIGHT_MAX_DUTY ((1U << BACKLIGHT_DUTY_BITS) - 1U)
#define I2S_BYTES_PER_FRAME \
    (AUDIO_CHANNEL_COUNT * (AUDIO_BITS_PER_SAMPLE / 8U))
#define I2S_DMA_BUFFER_BYTES (I2S_DMA_FRAME_NUM * I2S_BYTES_PER_FRAME)
#define AUDIO_HEALTH_REPORT_MS 10000U
/* Averaging window for the level meter, in frames: 256 is 5.8 ms at 44.1 kHz.
 * A decoded block is 23-26 ms, and averaging one whole flattens the attack of
 * every note - the movement the meter is watched for. Short enough to let
 * transients through, long enough that the reading is still energy and not the
 * peak of a handful of samples. */
#define AUDIO_LEVEL_WINDOW_FRAMES 256U

static const char *TAG = "board";
static uint16_t s_draw_buffer[TFT_WIDTH * LCD_DRAW_LINES];
static uint16_t s_fill_buffer[TFT_WIDTH * LCD_DRAW_LINES];
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_lcd_transfer_done;
static i2s_chan_handle_t s_i2s_tx;
static SemaphoreHandle_t s_audio_mutex;
static bool s_audio_enabled;
static const uint8_t s_i2s_silence[I2S_DMA_BUFFER_BYTES];
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
static uint32_t s_audio_sample_rate = AUDIO_DEFAULT_SAMPLE_RATE;
/* Peak since the UI last looked. Written by the player tasks, taken by the UI
 * task, so this uses atomics rather than s_audio_mutex: drawing a bar must
 * never block the audio path, nor be blocked by it. */
static atomic_uint s_audio_level_left = ATOMIC_VAR_INIT(0U);
static atomic_uint s_audio_level_right = ATOMIC_VAR_INIT(0U);
/* Whether a block was measured at all since the last take. A level of zero is
 * a real reading - a silent passage - and the UI has to tell it from having
 * looked between two blocks, which is what most of its passes do. */
static atomic_bool s_audio_level_fresh = ATOMIC_VAR_INIT(false);
/* Volume setting and the scratch the scaled samples go into. Full volume needs
 * neither: the block is handed to I2S untouched, so the common case costs
 * nothing and stays bit-exact. */
static atomic_uint s_audio_volume = ATOMIC_VAR_INIT(AUDIO_VOLUME_MAX_PERCENT);
static uint8_t *s_audio_gain_scratch;
static size_t s_audio_gain_scratch_size;
/* Frames clocked out since output was last enabled, counted only as far as the
 * fade needs. Guarded by s_audio_mutex like the scratch. */
static uint32_t s_audio_fade_frames;

static void board_audio_level_note(unsigned int value, atomic_uint *slot)
{
    unsigned int previous = atomic_load_explicit(slot, memory_order_relaxed);
    // Keep the loudest block written since the last take, so a peak falling
    // between two UI polls is still the one reported.
    while (value > previous &&
           !atomic_compare_exchange_weak_explicit(slot, &previous, value,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

void board_audio_set_volume(uint8_t percent)
{
    if (percent > AUDIO_VOLUME_MAX_PERCENT) percent = AUDIO_VOLUME_MAX_PERCENT;
    atomic_store_explicit(&s_audio_volume, percent, memory_order_relaxed);
}

uint8_t board_audio_volume(void)
{
    return (uint8_t)atomic_load_explicit(&s_audio_volume, memory_order_relaxed);
}

bool board_audio_level_take(uint16_t *left, uint16_t *right)
{
    /* Cleared before the levels are read, so a block landing in between is
     * reported by this call rather than being dropped with its flag. */
    const bool fresh = atomic_exchange_explicit(&s_audio_level_fresh, false,
                                                memory_order_relaxed);
    if (left != NULL) {
        *left = (uint16_t)atomic_exchange_explicit(&s_audio_level_left, 0U,
                                                   memory_order_relaxed);
    }
    if (right != NULL) {
        *right = (uint16_t)atomic_exchange_explicit(&s_audio_level_right, 0U,
                                                    memory_order_relaxed);
    }
    return fresh;
}

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
        I2S_DOUT_GPIO,
        I2S_BCLK_GPIO,
        I2S_LRCK_GPIO,
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

    const uint32_t duty = (BACKLIGHT_MAX_DUTY * percent) / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty), TAG,
                        "set backlight duty failed");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static esp_err_t board_backlight_init(void)
{
    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BACKLIGHT_DUTY_RES,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BACKLIGHT_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel_config = {
        .gpio_num = TFT_BACKLIGHT_GPIO,
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
    channel_config.dma_desc_num = I2S_DMA_DESC_NUM;
    channel_config.dma_frame_num = I2S_DMA_FRAME_NUM;
    channel_config.auto_clear_after_cb = I2S_AUTO_CLEAR_AFTER_CB != 0;
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
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_DEFAULT_SAMPLE_RATE),
        /* PCM5102 uses the Philips I2S timing: audio data is delayed by one
         * BCLK after the LRCK edge.  MSB/left-justified timing shifts every
         * sample by one bit and is not the interface selected on this board. */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                         I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK_GPIO,
            .ws = I2S_LRCK_GPIO,
            .dout = I2S_DOUT_GPIO,
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
    std_config.slot_cfg.slot_bit_width = (i2s_slot_bit_width_t)I2S_SLOT_BIT_WIDTH;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std_config), TAG,
                        "configure I2S TX failed");
    ESP_RETURN_ON_ERROR(gpio_input_enable(I2S_DOUT_GPIO), TAG,
                        "enable I2S DOUT pad observation failed");
    ESP_RETURN_ON_ERROR(gpio_input_enable(I2S_BCLK_GPIO), TAG,
                        "enable I2S BCLK pad observation failed");
    ESP_RETURN_ON_ERROR(gpio_input_enable(I2S_LRCK_GPIO), TAG,
                        "enable I2S LRCK pad observation failed");
    ESP_LOGI(TAG,
             "PCM5102 I2S: Philips, 16-bit stereo in %u-bit slots, BCLK=%uxFs, "
             "DMA=%ux%u frames (%lu ms)",
             I2S_SLOT_BIT_WIDTH, I2S_SLOT_BIT_WIDTH * AUDIO_CHANNEL_COUNT,
             I2S_DMA_DESC_NUM, I2S_DMA_FRAME_NUM,
             (unsigned long)((I2S_DMA_DESC_NUM * I2S_DMA_FRAME_NUM * 1000U) /
                             AUDIO_DEFAULT_SAMPLE_RATE));
    /* Keep TX in READY state. It is preloaded and enabled only when an audio
     * source starts, so the DAC never starts from empty DMA descriptors. */
    s_audio_enabled = false;
    return ESP_OK;
}

static esp_err_t board_audio_preload_silence(void)
{
#if I2S_PRELOAD_SILENCE
    size_t total_loaded = 0U;
    for (size_t descriptor = 0; descriptor < I2S_DMA_DESC_NUM; ++descriptor) {
        size_t loaded = 0U;
        ESP_RETURN_ON_ERROR(i2s_channel_preload_data(s_i2s_tx, s_i2s_silence,
                                                      sizeof(s_i2s_silence), &loaded),
                            TAG, "preload I2S silence failed");
        total_loaded += loaded;
        if (loaded != sizeof(s_i2s_silence)) {
            break;
        }
    }
    const size_t expected = board_audio_startup_silence_bytes(I2S_DMA_DESC_NUM,
                                                              sizeof(s_i2s_silence));
    if (total_loaded != expected) {
        ESP_LOGE(TAG, "I2S silence preload incomplete: loaded=%u expected=%u",
                 (unsigned int)total_loaded, (unsigned int)expected);
        return ESP_FAIL;
    }
#endif
    return ESP_OK;
}

_Static_assert(AUDIO_VOLUME_FRAME_BYTES == I2S_BYTES_PER_FRAME,
               "volume ramp and I2S disagree about the size of a frame");

/* Returns the block to hand I2S: `pcm` itself when it needs no change, or the
 * scratch holding an attenuated copy. Advances the fade.
 *
 * Both the first block and every one after it come through here. They used not
 * to: the first was written straight to I2S, at full volume whatever the
 * listener had set, which is the click that started playback on a box turned
 * down.
 *
 * Call with s_audio_mutex held - the scratch and the fade counter are shared
 * between the radio's task and the file player's. */
static const void *board_audio_shaped_block(const void *pcm, size_t pcm_length)
{
    const uint16_t gain = audio_volume_gain(board_audio_volume());
    const uint32_t frames = (uint32_t)(pcm_length / AUDIO_VOLUME_FRAME_BYTES);
    const uint16_t gain_start =
        audio_volume_fade_gain(gain, s_audio_fade_frames, AUDIO_VOLUME_FADE_FRAMES);
    const uint16_t gain_end = audio_volume_fade_gain(gain, s_audio_fade_frames + frames,
                                                     AUDIO_VOLUME_FADE_FRAMES);
    if (s_audio_fade_frames < AUDIO_VOLUME_FADE_FRAMES) {
        s_audio_fade_frames += frames;
    }
    if (gain_start == AUDIO_VOLUME_UNITY && gain_end == AUDIO_VOLUME_UNITY) {
        // Full volume, fade over: bit-exact, and no copy.
        return pcm;
    }
    if (s_audio_gain_scratch == NULL || s_audio_gain_scratch_size < pcm_length) {
        // Grown once and kept: block sizes are fixed per source, so this
        // allocates on the first quiet block after boot and never again. PSRAM
        // first, like every other buffer here - internal memory is the scarce
        // pool and I2S copies out of this anyway.
        heap_caps_free(s_audio_gain_scratch);
        s_audio_gain_scratch = heap_caps_malloc(pcm_length,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_audio_gain_scratch == NULL) {
            s_audio_gain_scratch = heap_caps_malloc(pcm_length,
                                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        s_audio_gain_scratch_size = s_audio_gain_scratch != NULL ? pcm_length : 0U;
    }
    /* No scratch means no attenuation this block. Playing one block at full
     * volume is wrong but recoverable; refusing to write would starve the
     * DAC. */
    if (s_audio_gain_scratch == NULL) return pcm;
    audio_volume_apply_ramp(pcm, s_audio_gain_scratch, pcm_length, gain_start, gain_end);
    return s_audio_gain_scratch;
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
        /* Attenuate before the write, but measure after nothing: the meter
         * below reads `pcm`, the decoder's own block, so it reports what the
         * station is sending rather than how loud the listener set it. A meter
         * that fell with the volume knob would look like a failing stream. */
        const uint8_t volume = board_audio_volume();
        const void *out = board_audio_shaped_block(pcm, pcm_length);
        result = i2s_channel_write(s_i2s_tx, out, pcm_length, written,
                                   pdMS_TO_TICKS(timeout_ms));
        if (result == ESP_OK) {
            uint16_t peak_left = 0U;
            uint16_t peak_right = 0U;
            pcm_s16le_peak_stereo(pcm, *written, &peak_left, &peak_right);
            /* The health log wants the peak - it is looking for clipping and
             * for a channel gone dead, both of which are peak questions. The
             * meter wants RMS: peaks of mastered music sit a couple of dB under
             * full scale continuously, so a peak-fed meter never leaves the top
             * of the scale. */
            uint16_t level_left = 0U;
            uint16_t level_right = 0U;
            pcm_s16le_rms_stereo(pcm, *written, AUDIO_LEVEL_WINDOW_FRAMES, &level_left,
                                 &level_right);
            board_audio_level_note(level_left, &s_audio_level_left);
            board_audio_level_note(level_right, &s_audio_level_right);
            atomic_store_explicit(&s_audio_level_fresh, true, memory_order_relaxed);
            board_audio_health_add(&s_audio_health, *written, pcm_length,
                                   peak_left > peak_right ? peak_left : peak_right);
            // Measured on the bytes actually clocked out, not on the
            // decoder's block: it is the DAC's zero-detect this tracks, and
            // the DAC sees the attenuated samples.
            const uint32_t zero_run =
                pcm_s16le_zero_frame_run(out, *written, &s_audio_zero_carry);
            board_audio_health_note_zero_run(&s_audio_health, zero_run);
            // Silence the listener asked for is not a fault to report.
            if (volume > 0U && !s_audio_mute_reported &&
                zero_run >= AUDIO_ZERO_DETECT_FRAMES) {
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
            if (elapsed >= AUDIO_HEALTH_REPORT_MS) {
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
                 (unsigned int)mute_run, (unsigned int)AUDIO_ZERO_DETECT_FRAMES);
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
            // The DAC has been fed silence up to this instant, so this block
            // is the step the fade exists for.
            s_audio_fade_frames = 0U;
            const void *out = board_audio_shaped_block(pcm, pcm_length);
            result = i2s_channel_write(s_i2s_tx, out, pcm_length, preloaded,
                                       pdMS_TO_TICKS(1000));
        }
        if (result == ESP_OK && *preloaded == pcm_length) {
            ESP_LOGI(TAG,
                     "PCM5102 I2S output enabled after %u-byte silent clock pre-roll",
                     (unsigned int)board_audio_startup_silence_bytes(
                         I2S_DMA_DESC_NUM, sizeof(s_i2s_silence)));
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
                s_audio_fade_frames = 0U;
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
    size_t frames_remaining = ((size_t)AUDIO_DEFAULT_SAMPLE_RATE * duration_ms) / 1000U;
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
            pcm, frames * 4U, AUDIO_DEFAULT_SAMPLE_RATE, 1000U, 12000, &phase);
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
        x2 > TFT_WIDTH || y2 > TFT_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    const int width = x2 - x1;
    for (int y = y1; y < y2; y += LCD_DRAW_LINES) {
        const int lines = (y2 - y) < LCD_DRAW_LINES ? (y2 - y) : LCD_DRAW_LINES;
        for (int index = 0; index < width * lines; ++index) {
            const int source_row = (y - y1) + index / width;
            s_draw_buffer[index] = __builtin_bswap16(pixels[source_row * width + (index % width)]);
        }
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(s_panel, x1, y, x2, y + lines, s_draw_buffer), TAG,
                            "draw display rectangle failed");
        if (LCD_WAIT_FOR_TRANSFER &&
            xSemaphoreTake(s_lcd_transfer_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "timed out waiting for LCD DMA transfer");
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

/* The splash replaces the solid fill this used to draw, and keeps its job: a
 * picture on the panel proves the SPI link, the panel init and the backlight
 * as well as a flat colour did, and it stays up until ui_init() builds the
 * LVGL screens a second or two later.
 *
 * Decoded a band at a time straight into the fill buffer, so the picture
 * needs no buffer of its own - the runs arrive in raster order, which is the
 * order the bands are drawn in. */
static esp_err_t board_display_splash(void)
{
    size_t pair = 0;
    uint16_t remaining = 0;
    uint16_t colour = 0;

    for (int y = 0; y < TFT_HEIGHT; y += LCD_DRAW_LINES) {
        const int y_end = (y + LCD_DRAW_LINES < TFT_HEIGHT) ?
                              y + LCD_DRAW_LINES : TFT_HEIGHT;
        const size_t count = (size_t)TFT_WIDTH * (size_t)(y_end - y);
        for (size_t index = 0; index < count; ++index) {
            if (remaining == 0) {
                if (pair >= boot_splash_rle_pairs) {
                    ESP_LOGE(TAG, "boot splash ran out of runs at row %d", y);
                    return ESP_ERR_INVALID_SIZE;
                }
                remaining = boot_splash_rle[pair * 2U];
                colour = boot_splash_rle[pair * 2U + 1U];
                ++pair;
            }
            s_fill_buffer[index] = colour;
            --remaining;
        }
        ESP_RETURN_ON_ERROR(board_display_draw_rgb565(0, y, TFT_WIDTH, y_end, s_fill_buffer), TAG,
                            "draw boot splash failed");
    }
    return ESP_OK;
}

static esp_err_t board_display_init(bool flip_vertical, bool flip_horizontal)
{
    s_lcd_transfer_done = xSemaphoreCreateBinary();
    if (s_lcd_transfer_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t bus_config = {
        .sclk_io_num = TFT_SCLK_GPIO,
        .mosi_io_num = TFT_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * LCD_DRAW_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO), TAG,
                        "initialize LCD SPI bus failed");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = TFT_DC_GPIO,
        .cs_gpio_num = TFT_CS_GPIO,
        .pclk_hz = DISPLAY_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = board_lcd_color_transfer_done,
        .user_ctx = s_lcd_transfer_done,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                                  &io_config, &io_handle), TAG,
                        "create LCD SPI I/O failed");

    /* Which controller answers here is the one thing this file does not know:
     * board_panel_create() lives in display/<part>.c and is the only code that
     * names a vendor driver. What follows it is MADCTL, the same register on
     * every panel in the catalogue. */
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(board_panel_create(io_handle, &panel), TAG,
                        "create " BOARD_PANEL_NAME " panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel, TFT_SWAP_XY), TAG,
                        "set landscape rotation failed");
    s_panel = panel;
    /* Before the splash, and through the same call the settings screen uses:
     * the baseline mirror and the user's two switches compose in exactly one
     * place, and the picture is written under the mapping it will be read
     * back through. Mirroring after it is drawn would not move it - see
     * board_display_set_rotation(). */
    ESP_RETURN_ON_ERROR(board_display_set_rotation(flip_vertical, flip_horizontal), TAG,
                        "set LCD mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "turn LCD on failed");
    return board_display_splash();
}

/* The argument order looks transposed and is not: with TFT_SWAP_XY set, MADCTL
 * has MV on, and reasoning from the datasheet suggests MX should then act on
 * the screen's vertical axis - i.e. that these two are swapped. Checked on the
 * panel 2026-08-16: both switches move the image the way their labels say. Do
 * not "fix" this without looking at the screen again.
 *
 * mirror() only touches MX/MY and leaves MV alone, so the swap_xy set at init
 * survives.
 *
 * The user's two switches compose with the panel's own baseline rather than
 * replacing it. esp_lcd_panel_mirror() sets MX/MY outright, so passing the
 * switches alone would throw away TFT_MIRROR_X/Y - the orientation the panel
 * needs before anybody touches a setting - the first time either switch moved.
 * Invisible on this board, where both are 0 and XOR changes nothing; on a panel
 * with a non-zero baseline it is the difference between a flip and a picture
 * that lands upside down and stays there.
 *
 * XOR rather than OR: a switch has to be able to turn a baseline mirror off
 * again, or the setting would only ever work in one direction. */
esp_err_t board_display_set_rotation(bool flip_vertical, bool flip_horizontal)
{
    if (s_panel == NULL) return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_mirror(s_panel, flip_horizontal != (bool)TFT_MIRROR_X,
                                flip_vertical != (bool)TFT_MIRROR_Y);
}

esp_err_t board_init(bool flip_vertical, bool flip_horizontal)
{
    ESP_LOGI(TAG, "initializing input, PWM backlight, I2S and " BOARD_PANEL_NAME);
    ESP_RETURN_ON_ERROR(board_input_init(), TAG, "configure input GPIOs failed");
    ESP_RETURN_ON_ERROR(board_backlight_init(), TAG, "initialize backlight failed");
    ESP_RETURN_ON_ERROR(board_audio_init(), TAG, "initialize PCM5102 I2S output failed");
    ESP_RETURN_ON_ERROR(board_display_init(flip_vertical, flip_horizontal), TAG,
                        "initialize " BOARD_PANEL_NAME " failed");
    ESP_RETURN_ON_ERROR(board_backlight_set(50), TAG, "set initial backlight failed");
    ESP_LOGI(TAG, "board initialized; display shows the boot splash");
    return ESP_OK;
}
