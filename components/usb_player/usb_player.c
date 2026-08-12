#include "usb_player.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "board.h"
#include "radio_decoder.h"
#include "radio_stream_format.h"
#include "usb_wav.h"

static const char *TAG = "usb_player";

// A local file never stalls the way a network stream does, so none of the
// radio's prebuffering applies here. The input buffer only has to hold one
// worst-case compressed frame plus slack; FLAC frames are the large ones.
#define USB_PLAYER_INPUT_SIZE 32768U
#define USB_PLAYER_READ_CHUNK 4096U
#define USB_PLAYER_PCM_SIZE 16384U
#define USB_PLAYER_STOP_TIMEOUT_MS 5000U
// Pinned to core 1 for the same reason as the radio decoder: core 0 carries
// Wi-Fi, lwIP, the HTTP server and LVGL, and lwIP at priority 18 preempts the
// decoder mid-frame when it shares a core.
#define USB_PLAYER_TASK_CORE 1
#define USB_PLAYER_TASK_PRIORITY 6
#define USB_PLAYER_TASK_STACK 8192

static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static usb_player_status_t s_status;
static SemaphoreHandle_t s_control_lock;
static TaskHandle_t s_task;
static atomic_bool s_stop_requested = ATOMIC_VAR_INIT(false);
static atomic_bool s_paused = ATOMIC_VAR_INIT(false);
static atomic_bool s_task_running = ATOMIC_VAR_INIT(false);

typedef struct {
    char path[USB_BROWSER_PATH_MAX_LEN];
    usb_browser_format_t format;
} usb_player_request_t;

static usb_player_request_t s_request;
static usb_player_finished_cb_t s_finished_callback;

void usb_player_set_finished_callback(usb_player_finished_cb_t callback)
{
    s_finished_callback = callback;
}

static void status_set_state(usb_player_state_t state)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status.state = state;
    taskEXIT_CRITICAL(&s_status_lock);
}

static bool stream_format_for(usb_browser_format_t format, radio_stream_format_t *out)
{
    switch (format) {
    case USB_BROWSER_FORMAT_MP3: *out = RADIO_STREAM_FORMAT_MP3; return true;
    case USB_BROWSER_FORMAT_AAC: *out = RADIO_STREAM_FORMAT_AAC; return true;
    case USB_BROWSER_FORMAT_FLAC: *out = RADIO_STREAM_FORMAT_FLAC; return true;
    case USB_BROWSER_FORMAT_OGG_FLAC: *out = RADIO_STREAM_FORMAT_OGG_FLAC; return true;
    default: return false;
    }
}

static uint8_t *alloc_buffer(size_t size)
{
    // Internal SRAM headroom is tight, so try PSRAM first and keep the
    // internal fallback, matching the radio path.
    uint8_t *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buffer;
}

typedef struct {
    FILE *file;
    uint8_t *compressed;
    uint8_t *pcm;
    size_t available;
    size_t offset;
    bool eof;
    bool output_started;
    uint32_t output_sample_rate;
} usb_player_context_t;

static bool refill(usb_player_context_t *ctx)
{
    if (ctx->offset > 0U) {
        // Files are cheap to read, so compact on every refill rather than
        // tracking a lazy read cursor the way the network path has to.
        memmove(ctx->compressed, ctx->compressed + ctx->offset, ctx->available);
        ctx->offset = 0U;
    }
    const size_t room = USB_PLAYER_INPUT_SIZE - ctx->available;
    if (room == 0U) return false;
    const size_t want = room < USB_PLAYER_READ_CHUNK ? room : USB_PLAYER_READ_CHUNK;
    const size_t received = fread(ctx->compressed + ctx->available, 1U, want, ctx->file);
    ctx->available += received;
    if (received < want) ctx->eof = true;
    return received > 0U;
}

static esp_err_t apply_info(usb_player_context_t *ctx, const radio_decoder_info_t *info)
{
    taskENTER_CRITICAL(&s_status_lock);
    if (info->bitrate_kbps > 0U) s_status.bitrate_kbps = (uint16_t)info->bitrate_kbps;
    if (info->sample_rate > 0U) s_status.sample_rate_hz = info->sample_rate;
    taskEXIT_CRITICAL(&s_status_lock);

    if (info->sample_rate == 0U || info->sample_rate == ctx->output_sample_rate) {
        return ESP_OK;
    }
    // Changing rate under a running I2S channel keeps the old clock for the
    // already-queued DMA buffers, so the tail of the previous track plays back
    // at the wrong speed. Disable, retune, and let the next PCM block restart
    // output with its silent pre-roll.
    esp_err_t result = ESP_OK;
    if (ctx->output_started) {
        result = board_audio_set_enabled(false);
        if (result == ESP_OK) ctx->output_started = false;
    }
    if (result == ESP_OK) result = board_audio_set_sample_rate(info->sample_rate);
    if (result == ESP_OK) ctx->output_sample_rate = info->sample_rate;
    return result;
}

static esp_err_t write_pcm(usb_player_context_t *ctx, const uint8_t *pcm, size_t length)
{
    size_t written = 0U;
    esp_err_t result;
    if (!ctx->output_started) {
        result = board_audio_start(pcm, length, &written);
        if (result == ESP_OK && written < length) {
            size_t remainder = 0U;
            result = board_audio_write(pcm + written, length - written, &remainder, 1000U);
            written += remainder;
        }
        if (result == ESP_OK) {
            ctx->output_started = true;
            status_set_state(USB_PLAYER_STATE_PLAYING);
            ESP_LOGI(TAG, "PCM output started: rate=%u block=%u",
                     (unsigned int)ctx->output_sample_rate, (unsigned int)length);
        }
    } else {
        result = board_audio_write(pcm, length, &written, 1000U);
    }
    if (result != ESP_OK || written != length) {
        ESP_LOGE(TAG, "PCM output failed: err=%s written=%u expected=%u",
                 esp_err_to_name(result), (unsigned int)written, (unsigned int)length);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool should_stop(void)
{
    return atomic_load_explicit(&s_stop_requested, memory_order_acquire);
}

// True while paused, after sleeping; keeps the two playback loops from each
// growing their own copy of the pause handling.
static bool wait_while_paused(void)
{
    if (!atomic_load_explicit(&s_paused, memory_order_acquire)) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

// WAV carries finished PCM, so there is no decoder in this path at all - only
// the header parse, an optional mono-to-stereo expansion, and I2S.
static bool play_wav(usb_player_context_t *ctx, unsigned int *pcm_blocks)
{
    usb_wav_info_t wav = {0};
    usb_wav_result_t parsed = usb_wav_parse_header(ctx->compressed, ctx->available, &wav);
    while (parsed == USB_WAV_NEED_MORE_DATA && !ctx->eof) {
        if (!refill(ctx) && ctx->available == USB_PLAYER_INPUT_SIZE) break;
        parsed = usb_wav_parse_header(ctx->compressed, ctx->available, &wav);
    }
    if (parsed != USB_WAV_OK) {
        ESP_LOGE(TAG, "unusable WAV header: result=%d", (int)parsed);
        return true;
    }
    ESP_LOGI(TAG, "WAV ready: rate=%u channels=%u bits=%u data=%u bytes",
             (unsigned int)wav.sample_rate, (unsigned int)wav.channels,
             (unsigned int)wav.bits_per_sample, (unsigned int)wav.data_length);

    const radio_decoder_info_t info = {
        .sample_rate = wav.sample_rate,
        .channels = (uint8_t)wav.channels,
        .bits_per_sample = (uint8_t)wav.bits_per_sample,
        .bitrate_kbps = wav.sample_rate * wav.channels * wav.bits_per_sample / 1000U,
    };
    if (apply_info(ctx, &info) != ESP_OK) {
        ESP_LOGE(TAG, "cannot configure output for %u Hz", (unsigned int)wav.sample_rate);
        return true;
    }
    if (fseek(ctx->file, (long)wav.data_offset, SEEK_SET) != 0) {
        ESP_LOGE(TAG, "cannot seek to the WAV sample data");
        return true;
    }

    const size_t frame_bytes = (size_t)wav.channels * 2U;
    // A declared length of zero means the writer never finished the header, so
    // play to the end of the file instead of stopping immediately.
    size_t remaining = wav.data_length > 0U ? wav.data_length : SIZE_MAX;
    for (;;) {
        if (should_stop()) return false;
        if (wait_while_paused()) continue;

        size_t want = USB_PLAYER_READ_CHUNK < remaining ? USB_PLAYER_READ_CHUNK : remaining;
        want -= want % frame_bytes;
        if (want == 0U) return false;
        const size_t received = fread(ctx->compressed, 1U, want, ctx->file);
        // A trailing partial frame is truncation in the file, not an error
        // worth failing the track over.
        const size_t usable = received - (received % frame_bytes);
        if (usable == 0U) return false;
        remaining -= usable;

        const uint8_t *out = ctx->compressed;
        size_t out_length = usable;
        if (wav.channels == 1U) {
            // The I2S channel is fixed at 16-bit stereo slots, so a mono file
            // has to be duplicated into both channels rather than reconfigured.
            const int16_t *source = (const int16_t *)(const void *)ctx->compressed;
            int16_t *target = (int16_t *)(void *)ctx->pcm;
            const size_t samples = usable / 2U;
            for (size_t i = 0U; i < samples; ++i) {
                target[2U * i] = source[i];
                target[2U * i + 1U] = source[i];
            }
            out = ctx->pcm;
            out_length = usable * 2U;
        }
        if (write_pcm(ctx, out, out_length) != ESP_OK) return true;
        ++(*pcm_blocks);
        if (received < want) return false;
    }
}

static void usb_player_task(void *arg)
{
    (void)arg;
    atomic_store_explicit(&s_task_running, true, memory_order_release);

    usb_player_context_t ctx = {0};
    radio_decoder_t *decoder = NULL;
    radio_stream_format_t stream_format = RADIO_STREAM_FORMAT_MP3;
    const bool is_wav = s_request.format == USB_BROWSER_FORMAT_WAV;
    bool failed = false;

    if (!is_wav && !stream_format_for(s_request.format, &stream_format)) {
        ESP_LOGE(TAG, "no decoder for format %d", (int)s_request.format);
        failed = true;
    }

    if (!failed) {
        ctx.compressed = alloc_buffer(USB_PLAYER_INPUT_SIZE);
        ctx.pcm = alloc_buffer(USB_PLAYER_PCM_SIZE);
        ctx.file = fopen(s_request.path, "rb");
        if (!is_wav) decoder = radio_decoder_create(stream_format);
        if (ctx.compressed == NULL || ctx.pcm == NULL || (!is_wav && decoder == NULL)) {
            ESP_LOGE(TAG, "allocation failed for %s", s_request.path);
            failed = true;
        } else if (ctx.file == NULL) {
            ESP_LOGE(TAG, "cannot open %s", s_request.path);
            failed = true;
        }
    }

    if (!failed) {
        taskENTER_CRITICAL(&s_status_lock);
        snprintf(s_status.codec, sizeof(s_status.codec), "%s",
                 is_wav ? "WAV" : radio_stream_format_codec_name(stream_format));
        taskEXIT_CRITICAL(&s_status_lock);
        ESP_LOGI(TAG, "playing %s", s_request.path);
    }

    unsigned int pcm_blocks = 0U;
    if (!failed && is_wav) {
        failed = play_wav(&ctx, &pcm_blocks);
    }
    while (!failed && !is_wav) {
        if (should_stop()) break;
        if (wait_while_paused()) continue;

        bool need_input = ctx.available == 0U;
        if (!need_input) {
            size_t consumed = 0U;
            size_t pcm_bytes = 0U;
            radio_decoder_info_t info = {0};
            const radio_decoder_result_t result =
                radio_decoder_decode(decoder, ctx.compressed + ctx.offset, ctx.available,
                                     ctx.pcm, USB_PLAYER_PCM_SIZE, &consumed, &pcm_bytes,
                                     &info);
            if (consumed > ctx.available) {
                ESP_LOGE(TAG, "decoder consumed %u of %u available", (unsigned int)consumed,
                         (unsigned int)ctx.available);
                failed = true;
                break;
            }
            ctx.available -= consumed;
            ctx.offset += consumed;

            if (result == RADIO_DECODER_HEADER_READY || result == RADIO_DECODER_PCM_READY) {
                if (apply_info(&ctx, &info) != ESP_OK) {
                    ESP_LOGE(TAG, "cannot configure output for %u Hz",
                             (unsigned int)info.sample_rate);
                    failed = true;
                    break;
                }
                if (result == RADIO_DECODER_HEADER_READY) {
                    ESP_LOGI(TAG, "decoder ready: codec=%s rate=%u channels=%u bits=%u",
                             radio_stream_format_codec_name(stream_format),
                             (unsigned int)info.sample_rate, (unsigned int)info.channels,
                             (unsigned int)info.bits_per_sample);
                } else if (pcm_bytes > 0U) {
                    if (write_pcm(&ctx, ctx.pcm, pcm_bytes) != ESP_OK) {
                        failed = true;
                        break;
                    }
                    ++pcm_blocks;
                }
            } else if (result == RADIO_DECODER_NEED_MORE_DATA) {
                need_input = true;
            } else if (result == RADIO_DECODER_END_OF_STREAM) {
                break;
            } else {
                ESP_LOGE(TAG, "decoder error: result=%d available=%u consumed=%u", (int)result,
                         (unsigned int)ctx.available, (unsigned int)consumed);
                failed = true;
                break;
            }
        }

        if (need_input) {
            // The decoder wants more but the file is spent. Whatever is left
            // in the buffer is a partial frame or a trailing tag, so this is a
            // normal end of track rather than a corrupt file.
            if (ctx.eof) break;
            if (!refill(&ctx) && ctx.available == USB_PLAYER_INPUT_SIZE) {
                ESP_LOGE(TAG, "compressed frame larger than the %u byte input buffer",
                         (unsigned int)USB_PLAYER_INPUT_SIZE);
                failed = true;
                break;
            }
        }
    }

    if (ctx.output_started) (void)board_audio_set_enabled(false);
    if (decoder != NULL) radio_decoder_destroy(decoder);
    if (ctx.file != NULL) fclose(ctx.file);
    free(ctx.compressed);
    free(ctx.pcm);

    const bool ended_by_itself = !failed && !should_stop();
    ESP_LOGI(TAG, "playback finished: blocks=%u failed=%d natural_end=%d", pcm_blocks,
             (int)failed, (int)ended_by_itself);
    status_set_state(failed ? USB_PLAYER_STATE_ERROR : USB_PLAYER_STATE_STOPPED);
    // Clear the running flag and the handle before notifying, so the listener
    // can start the next track without waiting out this task's stop timeout.
    atomic_store_explicit(&s_task_running, false, memory_order_release);
    s_task = NULL;
    const usb_player_finished_cb_t callback = s_finished_callback;
    if (ended_by_itself && callback != NULL) callback();
    vTaskDelete(NULL);
}

static void wait_for_task_exit(void)
{
    atomic_store_explicit(&s_stop_requested, true, memory_order_release);
    // Unpause too: a paused task never reaches the stop check otherwise.
    atomic_store_explicit(&s_paused, false, memory_order_release);
    for (unsigned int waited = 0U;
         atomic_load_explicit(&s_task_running, memory_order_acquire) &&
         waited < USB_PLAYER_STOP_TIMEOUT_MS;
         waited += 10U) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    atomic_store_explicit(&s_stop_requested, false, memory_order_release);
}

esp_err_t usb_player_init(void)
{
    if (s_control_lock != NULL) return ESP_OK;
    s_control_lock = xSemaphoreCreateMutex();
    if (s_control_lock == NULL) return ESP_ERR_NO_MEM;
    memset(&s_status, 0, sizeof(s_status));
    return ESP_OK;
}

esp_err_t usb_player_play(const char *path, const char *display_name,
                          usb_browser_format_t format)
{
    if (path == NULL || s_control_lock == NULL) return ESP_ERR_INVALID_ARG;
    if (strlen(path) >= sizeof(s_request.path)) return ESP_ERR_INVALID_SIZE;
    radio_stream_format_t decoded;
    // WAV bypasses radio_decoder entirely, so it has no stream format of its
    // own and must be admitted separately.
    if (format != USB_BROWSER_FORMAT_WAV && !stream_format_for(format, &decoded)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    wait_for_task_exit();

    snprintf(s_request.path, sizeof(s_request.path), "%s", path);
    s_request.format = format;
    taskENTER_CRITICAL(&s_status_lock);
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = USB_PLAYER_STATE_STARTING;
    snprintf(s_status.track, sizeof(s_status.track), "%s",
             display_name != NULL ? display_name : path);
    taskEXIT_CRITICAL(&s_status_lock);

    const BaseType_t created =
        xTaskCreatePinnedToCore(usb_player_task, "usb_play", USB_PLAYER_TASK_STACK, NULL,
                                USB_PLAYER_TASK_PRIORITY, &s_task, USB_PLAYER_TASK_CORE);
    if (created != pdPASS) {
        s_task = NULL;
        status_set_state(USB_PLAYER_STATE_ERROR);
        xSemaphoreGive(s_control_lock);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_control_lock);
    return ESP_OK;
}

esp_err_t usb_player_stop(void)
{
    if (s_control_lock == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    wait_for_task_exit();
    status_set_state(USB_PLAYER_STATE_STOPPED);
    xSemaphoreGive(s_control_lock);
    return ESP_OK;
}

esp_err_t usb_player_pause(void)
{
    if (!atomic_load_explicit(&s_task_running, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store_explicit(&s_paused, true, memory_order_release);
    // Silence the DAC rather than letting the DMA repeat its last buffer, the
    // same reason the radio disables output on pause.
    (void)board_audio_set_enabled(false);
    status_set_state(USB_PLAYER_STATE_PAUSED);
    return ESP_OK;
}

esp_err_t usb_player_resume(void)
{
    if (!atomic_load_explicit(&s_task_running, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = board_audio_set_enabled(true);
    if (result != ESP_OK) return result;
    atomic_store_explicit(&s_paused, false, memory_order_release);
    status_set_state(USB_PLAYER_STATE_PLAYING);
    return ESP_OK;
}

void usb_player_get_status(usb_player_status_t *status)
{
    if (status == NULL) return;
    taskENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    taskEXIT_CRITICAL(&s_status_lock);
}
