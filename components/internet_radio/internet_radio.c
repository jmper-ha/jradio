#include "internet_radio.h"

#include <ctype.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "board.h"
#include "board_audio_format.h"
#include "hls_playlist.h"
#include "icy_metadata.h"
#include "pcm_diagnostics.h"
#include "radio_http_status.h"
#include "radio_prebuffer.h"
#include "radio_decoder.h"
#include "radio_stream_format.h"
#include "station_resume.h"

#define RADIO_HTTP_BUFFER_SIZE 2048
/* esp_http_client builds the whole request line and its headers inside this
 * buffer, and its default is 512 bytes. A station URL never comes close, but a
 * signed Yandex track link is 1108 characters of one opaque path segment, and
 * the client answers a request that does not fit with "Out of buffer" - which
 * looks like a network failure and is not one. */
#define RADIO_HTTP_TX_BUFFER_SIZE 2048
/* Derived from what the catalogue may hold rather than typed: a buffer that
 * does not fit the file truncates it silently, and the count has already been
 * raised once. */
#define RADIO_CATALOG_BUFFER_SIZE STATION_CATALOG_TEXT_MAX_LEN
#define RADIO_HTTP_MAX_REDIRECTS 5U
#define RADIO_HTTP_CONNECT_RETRIES 3U
/* A live stream that stops delivering has already silenced the DAC well
 * before this fires: even at 128 kbps the input buffer only holds about a
 * second of audio. Waiting 30 s to notice, as this used to, turned a server
 * stall into 30 s of dead air; reconnecting promptly costs a fraction of it. */
#define RADIO_HTTP_IDLE_TIMEOUT_MS 3000U
#define RADIO_HTTP_RECONNECT_DELAY_MS 500U
/* Backlog held ahead of the decoder. 16 KB was about a second at 128 kbps but
 * only 90 ms of a 1441 kbps FLAC stream - less cushion than the I2S DMA
 * itself - so high-bitrate streams glitched on any network jitter. This lives
 * in PSRAM, where the extra 48 KB is cheap. */
#define RADIO_DIRECT_INPUT_SIZE 65536U
/* Wall-clock cap on one top-up burst. The bound has to be time, not a chunk
 * count: a stream we are behind on always has more bytes waiting, so the
 * question is only how long the loop may stay away from the decoder. Well
 * under the ~93 ms of I2S DMA, or the burst starves the DAC it is protecting -
 * which is exactly what an uncapped version did. */
#define RADIO_DIRECT_TOPUP_BUDGET_MS 20U
#define RADIO_DIRECT_NETWORK_CHUNK 2048U
#define RADIO_DIRECT_PCM_SIZE 16384U
#define RADIO_DIRECT_STOP_TIMEOUT_MS 12000U
#define RADIO_STARVATION_REPORT_MS 10000U
/* How long the decoder may consume input without producing a single sample
 * before the stream is treated as unplayable rather than slow.
 *
 * It has to be a timeout rather than an error count because a decoder does not
 * have to report an error to make no progress: micro_mp3 answers a corrupt
 * frame with MP3_DECODE_ERROR, which maps to "need more data" so that one bad
 * frame is skipped instead of killing the station - and a stream that turns
 * permanently unparseable then produces that answer forever. Measured on a
 * station that does it: every pass consumed one 208-byte frame and returned
 * need-more-data for as long as it was left running, with the input buffer
 * full, the player still reporting "playing", and nothing in the log.
 *
 * Two seconds is far past any legitimate gap - the I2S DMA holds ~93 ms, and
 * while data is buffered the decoder should answer in milliseconds - and still
 * short enough that the listener hears an interruption rather than a station
 * that never comes back. */
#define RADIO_DIRECT_DECODE_STALL_MS 2000U
/* Room for one HLS playlist. Relax FM's is 653 bytes; the ceiling is a master
 * playlist listing many variants, which is still text and still small. */
#define RADIO_HLS_TEXT_SIZE 8192U
#define RADIO_HLS_URL_MAX 320U
/* How long playback may go without a new segment before the stream counts as
 * dead. Has to be several segments: HLS delivers in ~6 s lumps, so the 3 s
 * that catches a stalled socket would fire between every one of them. */
#define RADIO_HLS_STALL_TIMEOUT_MS 25000U
/* Fallback when a playlist omits EXT-X-TARGETDURATION. */
#define RADIO_HLS_DEFAULT_TARGET_MS 6000U
#define RADIO_HLS_TIMEOUT_MS 10000
/* Cap on one read-ahead attempt into an open segment. Short enough that a
 * genuinely empty buffer costs the decode loop almost nothing. */
#define RADIO_HLS_TOPUP_TIMEOUT_MS 15

static const char *TAG = "internet_radio";

/* Live state of an HLS session: which playlist, how far through it, and the
 * bytes of the current segment still to be skipped. */
typedef struct {
    bool active;
    char media_url[RADIO_HLS_URL_MAX];
    uint64_t next_sequence;
    uint32_t target_duration_ms;
    /* Earliest tick at which the playlist may be fetched again. RFC 8216 bars
     * hammering a live playlist; without this a dry queue would re-fetch in a
     * tight loop for the whole gap before the next segment is published. */
    TickType_t refresh_allowed;
    TickType_t last_segment;
    hls_playlist_t *playlist;
    char *text;
    /* A response body is open on a segment. Not the same as "the connection is
     * open": the connection is held across segments on purpose. */
    bool segment_open;
    /* A request is on the wire whose response has not been collected yet. The
     * gap between the two is where the decoder keeps working. */
    bool request_sent;
    bool request_is_playlist;
    /* First bytes of a segment, read early to look for an ID3 tag and held
     * here when there was none - they are audio and must not be dropped. */
    uint8_t lead[16];
    size_t lead_length;
    size_t lead_offset;
} radio_hls_t;

typedef struct {
    esp_http_client_handle_t http;
    radio_hls_t hls;
    icy_metadata_t icy;
    internet_radio_status_t status;
    size_t icy_interval;
    size_t pcm_bytes;
    size_t pcm_blocks;
    bool output_logged;
    bool output_started;
    bool initialized;
    radio_stream_format_t stream_format;
    /* In PSRAM, not in this struct: the array is 35 KB, and this struct is a
     * static. Internal DRAM is the scarce resource here - a TLS handshake
     * needs it and cannot borrow from PSRAM - so the one big table that is
     * only ever read from tasks goes where there is room. Allocated once at
     * init and never reallocated, which is what lets readers take no lock;
     * see the note in internet_radio_replace_playlist(). */
    station_catalog_t *catalog;
    radio_decoder_t *decoder;
    TaskHandle_t direct_task;
    SemaphoreHandle_t direct_done;
    SemaphoreHandle_t direct_io_mutex;
    /* Set while the source hands out one finite track at a time. The end of a
     * body is then the end of a track, not a dropped connection. */
    bool track_chain;
    internet_radio_track_source_fn track_source;
    internet_radio_track_reopen_fn track_reopen;
    /* Bytes of the current track's body already taken off the socket, and the
     * offset the next open should ask for with a Range header (0 for the whole
     * body, which is every open but the one that resumes a paused track).
     *
     * Raw body bytes, counted before the ICY layer strips anything, because
     * that is the coordinate a Range is in. */
    size_t track_offset;
    size_t range_offset;
    // Whether the last open answered 206 rather than 200 - that is, whether
    // the Range was honoured and what is buffered is still the continuation.
    bool range_granted;
    atomic_bool skip_requested;
    atomic_bool direct_stop_requested;
    atomic_bool direct_paused;
    /* Written by the decode task on every pass and read by whoever asks for
     * status. Atomic rather than inside the status critical section: the loop
     * runs thousands of times a second and this is one byte of display. */
    atomic_uint input_fill_percent;
    uint32_t output_sample_rate;
} internet_radio_context_t;

static internet_radio_context_t s_radio;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

static bool radio_direct_reconnect(internet_radio_context_t *radio);
static bool radio_direct_advance(internet_radio_context_t *radio);
static bool radio_direct_resume_track(internet_radio_context_t *radio);
static bool radio_stream_is_open(const internet_radio_context_t *radio);
/* Defined below, past the HLS layer they dispatch to. */
static esp_err_t radio_stream_open(internet_radio_context_t *radio, const char *url);
static void radio_stream_close(internet_radio_context_t *radio);
static int radio_stream_read_blocking(internet_radio_context_t *radio, uint8_t *buffer,
                                      int length);
static int radio_stream_read_ready(internet_radio_context_t *radio, uint8_t *buffer, int length,
                                   uint32_t budget_ms);

static const station_catalog_entry_t s_europa_plus_station = {
    .name = "Европа плюс",
    .url = "http://online-1.gkvr.ru:8000/europa_krd_128.aac",
    .flag = 0,
};

static esp_err_t radio_sync_output(internet_radio_context_t *radio)
{
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    return board_audio_set_enabled(internet_radio_state_output_enabled(status.state));
}

static bool radio_status_apply(internet_radio_context_t *radio, internet_radio_event_t event)
{
    bool applied;
    taskENTER_CRITICAL(&s_status_lock);
    applied = internet_radio_state_apply(&radio->status.state, event);
    taskEXIT_CRITICAL(&s_status_lock);
    return applied;
}

static void radio_http_close(internet_radio_context_t *radio)
{
    if (radio->http == NULL) {
        return;
    }
    esp_http_client_close(radio->http);
    esp_http_client_cleanup(radio->http);
    radio->http = NULL;
}

static int radio_http_read_blocking(internet_radio_context_t *radio, uint8_t *buffer, int length)
{
    const TickType_t idle_started = xTaskGetTickCount();
    for (;;) {
        if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
            return -1;
        }
        const int received = esp_http_client_read(radio->http, (char *)buffer, length);
        const internet_radio_read_action_t action =
            internet_radio_read_classify(received, -ESP_ERR_HTTP_EAGAIN);
        if (action == INTERNET_RADIO_READ_DATA) {
            return received;
        }
        if (action == INTERNET_RADIO_READ_CLOSED) {
            ESP_LOGW(TAG, "HTTP stream closed by server");
            return 0;
        }
        if (action == INTERNET_RADIO_READ_ERROR) {
            ESP_LOGE(TAG, "HTTP stream read failed: result=%d", received);
            return received;
        }
        if ((uint32_t)((xTaskGetTickCount() - idle_started) * portTICK_PERIOD_MS) >=
            RADIO_HTTP_IDLE_TIMEOUT_MS) {
            ESP_LOGW(TAG, "HTTP stream idle for %u ms", RADIO_HTTP_IDLE_TIMEOUT_MS);
            return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* Reads what the socket holds now, waiting at most `budget_ms` for the first
 * byte and never blocking once data is flowing.
 *
 * The polling matters. esp_http_client_read() loops until it has filled the
 * length asked for (`while (need_read > 0 && is_data_remain)`), blocking on
 * the transport for up to client->timeout_ms - ten seconds here. An earlier
 * version of this function passed the full free space and called itself
 * non-blocking; it was not, which is why the backlog never grew and the loop
 * spent its time waiting on the socket instead of decoding.
 *
 * So: poll first, then read only as much as is already buffered. That caps
 * each call at one socket's worth of data and keeps the decoder running. */
static int radio_http_read_ready(internet_radio_context_t *radio, uint8_t *buffer,
                                 int length, uint32_t budget_ms)
{
    if (length <= 0) return 0;
    /* select() on the transport's own socket rather than a private
     * esp_http_client call: get_socket is the only readiness hook the public
     * API offers. Over TLS this is a lower bound - esp-tls may already hold a
     * decrypted record with the socket quiet - but that only costs one skipped
     * top-up, never a stall, because the urgent path still blocks. */
    const int fd = esp_http_client_get_socket(radio->http);
    if (fd >= 0) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(fd, &readable);
        struct timeval timeout = {
            .tv_sec = (time_t)(budget_ms / 1000U),
            .tv_usec = (suseconds_t)((budget_ms % 1000U) * 1000U),
        };
        const int ready = select(fd + 1, &readable, NULL, NULL, &timeout);
        if (ready < 0) return -1;
        if (ready == 0) return 0;
    }
    /* One MSS at a time: asking for more would make the client block waiting
     * for the rest of the request even though the poll only promised one
     * segment. */
    if (length > RADIO_DIRECT_NETWORK_CHUNK) length = RADIO_DIRECT_NETWORK_CHUNK;
    const int received = esp_http_client_read(radio->http, (char *)buffer, length);
    switch (internet_radio_read_classify(received, -ESP_ERR_HTTP_EAGAIN)) {
    case INTERNET_RADIO_READ_DATA:
        return received;
    case INTERNET_RADIO_READ_RETRY:
        return 0;
    default:
        return -1;
    }
}

static bool radio_delay_interruptible(internet_radio_context_t *radio, uint32_t delay_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
    while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
        if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

static void radio_set_title(void *context, const char *title)
{
    internet_radio_context_t *radio = context;
    taskENTER_CRITICAL(&s_status_lock);
    snprintf(radio->status.title, sizeof(radio->status.title), "%s", title);
    taskEXIT_CRITICAL(&s_status_lock);
    ESP_LOGI(TAG, "title: %s", title);
}

static esp_err_t radio_pcm_output(internet_radio_context_t *radio, const uint8_t *data,
                                  size_t data_size)
{
    const uint16_t peak = pcm_s16le_peak(data, (size_t)data_size);
    size_t written = 0;
    esp_err_t result;
    if (internet_radio_output_start_once(&radio->output_started)) {
        result = board_audio_start(data, (size_t)data_size, &written);
        if (result == ESP_OK && written < (size_t)data_size) {
            size_t remainder_written = 0;
            result = board_audio_write(data + written, (size_t)data_size - written,
                                       &remainder_written, 1000);
            written += remainder_written;
        }
    } else {
        result = board_audio_write(data, (size_t)data_size, &written, 1000);
    }
    radio->pcm_bytes += written;
    radio->pcm_blocks++;
    if (radio->pcm_blocks <= 4U) {
        ESP_LOGI(TAG, "PCM progress: block=%u bytes=%u written=%u total=%u peak=%u",
                 (unsigned int)radio->pcm_blocks, (unsigned int)data_size,
                 (unsigned int)written, (unsigned int)radio->pcm_bytes, peak);
    }
    if (!radio->output_logged && written > 0U) {
        // Sampled here rather than at connect time: the I2S DMA buffers are
        // only allocated once output is enabled, so this is the first point
        // that reflects the DMA heap the TLS record decryption will see.
        ESP_LOGI(TAG, "PCM output started: block=%u total=%u peak=%u "
                      "dma_free=%u dma_largest_block=%u",
                 (unsigned int)data_size, (unsigned int)radio->pcm_bytes, peak,
                 (unsigned int)heap_caps_get_free_size(MALLOC_CAP_DMA),
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
        radio->output_logged = true;
        if (radio_status_apply(radio, INTERNET_RADIO_EVENT_PLAYER_RUNNING)) {
            (void)radio_sync_output(radio);
        }
    }
    if (result != ESP_OK || written != data_size) {
        ESP_LOGE(TAG, "PCM output failed: err=%s written=%u expected=%u",
                 esp_err_to_name(result), (unsigned int)written, (unsigned int)data_size);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t radio_direct_update_info(internet_radio_context_t *radio,
                                          const radio_decoder_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t sample_rate = info->sample_rate;
    bool sample_rate_changed = false;
    bool restart_output = false;
    xSemaphoreTake(radio->direct_io_mutex, portMAX_DELAY);
    taskENTER_CRITICAL(&s_status_lock);
    if (info->bitrate_kbps > 0U && radio->status.bitrate_kbps == 0U) {
        radio->status.bitrate_kbps = (uint16_t)info->bitrate_kbps;
    }
    if (sample_rate > 0U && sample_rate != radio->output_sample_rate) {
        restart_output = internet_radio_output_needs_restart(
            radio->output_sample_rate, sample_rate, radio->output_started);
        radio->output_sample_rate = sample_rate;
        sample_rate_changed = true;
    }
    if (sample_rate > 0U) {
        radio->status.sample_rate_hz = sample_rate;
    }
    taskEXIT_CRITICAL(&s_status_lock);
    esp_err_t result = ESP_OK;
    if (restart_output) {
        ESP_LOGI(TAG, "audio format changed; restarting I2S with silent pre-roll");
        result = board_audio_set_enabled(false);
        if (result == ESP_OK) {
            radio->output_started = false;
        }
    }
    if (sample_rate_changed) {
        if (result == ESP_OK) {
            result = board_audio_set_sample_rate(sample_rate);
        }
    }
    xSemaphoreGive(radio->direct_io_mutex);
    return result;
}

/* Reports what the decoder is producing, on the first block and on every
 * change afterwards. `logged` holds the last line's contents, so calling this
 * per block costs a comparison.
 *
 * Driven from the PCM itself rather than from a header event because the
 * simple decoder can reach its first samples without ever reporting a header,
 * and an AAC stream then played with nothing about its format in the log. The
 * channel count is the value worth having: it is what distinguishes a source
 * that decodes to mono, which the output stage cannot take as it stands. */
static void radio_log_decoder_info(radio_stream_format_t format,
                                   const radio_decoder_info_t *info,
                                   radio_decoder_info_t *logged)
{
    /* An unpopulated info block is not a format change, just a decoder that
     * had nothing to say on this pass. */
    if (info->sample_rate == 0U) return;
    if (info->sample_rate == logged->sample_rate && info->channels == logged->channels &&
        info->bits_per_sample == logged->bits_per_sample) {
        return;
    }
    *logged = *info;
    ESP_LOGI(TAG, "direct decoder ready: codec=%s rate=%u channels=%u bits=%u",
             radio_stream_format_codec_name(format), (unsigned int)info->sample_rate,
             (unsigned int)info->channels, (unsigned int)info->bits_per_sample);
}

static void radio_direct_task(void *arg)
{
    internet_radio_context_t *radio = arg;
    uint8_t *compressed = heap_caps_malloc(RADIO_DIRECT_INPUT_SIZE,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (compressed == NULL) {
        compressed = heap_caps_malloc(RADIO_DIRECT_INPUT_SIZE,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    uint8_t *network = heap_caps_malloc(RADIO_DIRECT_NETWORK_CHUNK,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (network == NULL) {
        network = heap_caps_malloc(RADIO_DIRECT_NETWORK_CHUNK,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    uint8_t *audio = heap_caps_malloc(RADIO_DIRECT_NETWORK_CHUNK,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (audio == NULL) {
        audio = heap_caps_malloc(RADIO_DIRECT_NETWORK_CHUNK,
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    uint8_t *pcm = heap_caps_malloc(RADIO_DIRECT_PCM_SIZE,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = heap_caps_malloc(RADIO_DIRECT_PCM_SIZE,
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    /* Where the output buffer starts rather than where it stays. A FLAC
     * station chooses its own block size, and a frame is delivered whole or
     * not at all, so a buffer one sample short of a frame rejects every frame
     * the station sends. The header says how much is needed. */
    size_t pcm_capacity = pcm != NULL ? RADIO_DIRECT_PCM_SIZE : 0U;
    size_t available = 0U;
    size_t compressed_offset = 0U;
    unsigned int decode_error_retries = 0U;
    bool fatal = false;
    radio_prebuffer_config_t prebuffer;
    radio_prebuffer_config_init(&prebuffer, RADIO_DIRECT_INPUT_SIZE,
                                RADIO_DIRECT_NETWORK_CHUNK);
    if (compressed == NULL || network == NULL || audio == NULL || pcm == NULL || radio->decoder == NULL) {
        ESP_LOGE(TAG, "direct decoder buffer allocation failed");
        fatal = true;
    }
    // Starvation accounting: an audible dropout leaves no other trace, so
    // track how often the decoder was left with an empty input buffer and how
    // low the backlog ran, and pair it with the I2S underrun counter.
    unsigned int starvations = 0U;
    size_t min_available = RADIO_DIRECT_INPUT_SIZE;
    unsigned int last_underruns = board_audio_underrun_count();
    size_t last_pcm_bytes = 0U;
    bool report_armed = false;
    // What the decoder says it is producing. Logged on the first block and on
    // every change rather than only from a header event: the simple decoder
    // reaches its first PCM without ever reporting a header, so an AAC stream
    // used to play with its rate and channel count never printed at all - and
    // the channel count is what tells a mono source apart from a stereo one.
    radio_decoder_info_t logged_info = {0};
    /* When the decoder last produced anything, for RADIO_DIRECT_DECODE_STALL_MS.
     * `decode_stalls` is reported so a station that keeps doing this is visible
     * in the health line instead of only as silence. */
    TickType_t last_pcm_tick = xTaskGetTickCount();
    unsigned int decode_stalls = 0U;
    bool decoder_reset_tried = false;
    /* Set on a chain source when the body ran out; cleared once the next track
     * is open. Nothing is read from the network while it is set. */
    bool track_ended = false;
    TickType_t next_report = xTaskGetTickCount() + pdMS_TO_TICKS(RADIO_STARVATION_REPORT_MS);

    while (!fatal &&
           !atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
        if (atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
            /* Nothing reads the socket while paused, so the server drops us
             * anyway - it just does it on its own schedule, and the reconnect
             * then lands minutes later in the middle of the music. Letting go
             * here makes the moment ours, and hands the TLS session's internal
             * SRAM back for as long as the pause lasts.
             *
             * Not while a chain is playing out a finished track: its body is
             * already consumed and the next act is to open the following one,
             * so there is nothing here to reopen. */
            if (radio_stream_is_open(radio) && !track_ended) {
                radio_stream_close(radio);
                if (!radio->track_chain) {
                    /* A live stream has no position to hold. What is buffered
                     * is a few seconds of a broadcast that has since moved on,
                     * and playing it out on resume is how a pause turned into
                     * a delay. */
                    radio_decoder_reset(radio->decoder);
                    available = 0U;
                    compressed_offset = 0U;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            /* A pause decodes nothing by design, so it must not age into a
             * stall the moment playback resumes. */
            last_pcm_tick = xTaskGetTickCount();
            continue;
        }

        /* Resumed, with the pause having closed the stream. A station comes
         * back on the live edge, a track where it left off - which is the
         * whole difference between the two, and the only place it is spelled
         * out. A pending skip is left to the branch below: reopening a track
         * the listener has already asked to leave would cost an API call to
         * throw the answer away. */
        if (!radio_stream_is_open(radio) && !track_ended &&
            !atomic_load_explicit(&radio->skip_requested, memory_order_acquire)) {
            const bool reopened = radio->track_chain ? radio_direct_resume_track(radio)
                                                     : radio_direct_reconnect(radio);
            if (!reopened) {
                fatal = !atomic_load_explicit(&radio->direct_stop_requested,
                                              memory_order_acquire);
                break;
            }
            if (!radio->track_chain || !radio->range_granted) {
                // Either a station, whose backlog the pause already dropped,
                // or a server that ignored the Range and is sending the track
                // from its start: what is held is no longer its continuation.
                radio_decoder_reset(radio->decoder);
                available = 0U;
                compressed_offset = 0U;
            }
            decode_error_retries = 0U;
            decoder_reset_tried = false;
            last_pcm_tick = xTaskGetTickCount();
            continue;
        }

        if (!radio->output_started) {
            // Output is down (starting up, or torn down by a reconnect, which
            // also zeroes radio->pcm_bytes). Re-arm from scratch so the next
            // window measures a whole interval of real playback instead of
            // straddling the outage and underflowing the byte delta.
            report_armed = false;
        } else if (!report_armed) {
            report_armed = true;
            last_pcm_bytes = radio->pcm_bytes;
            last_underruns = board_audio_underrun_count();
            starvations = 0U;
            min_available = RADIO_DIRECT_INPUT_SIZE;
            next_report = xTaskGetTickCount() + pdMS_TO_TICKS(RADIO_STARVATION_REPORT_MS);
        } else if ((int32_t)(xTaskGetTickCount() - next_report) >= 0) {
            const unsigned int underruns = board_audio_underrun_count();
            // PCM throughput as a percentage of real time tells starvation
            // apart from a decoder that simply cannot keep up: at 44.1 kHz
            // stereo 16-bit, real time is 176400 B/s.
            const size_t pcm_delta = radio->pcm_bytes - last_pcm_bytes;
            const uint32_t expected =
                (radio->output_sample_rate > 0U ? radio->output_sample_rate : 44100U) *
                (uint32_t)AUDIO_CHANNEL_COUNT *
                (uint32_t)(AUDIO_BITS_PER_SAMPLE / 8U) *
                (RADIO_STARVATION_REPORT_MS / 1000U);
            // min_backlog in milliseconds is the number that matters: the same
            // byte count is a second of a 128 kbps stream and 90 ms of FLAC,
            // and what protects the DAC is time, not bytes.
            uint16_t bitrate;
            taskENTER_CRITICAL(&s_status_lock);
            bitrate = radio->status.bitrate_kbps;
            taskEXIT_CRITICAL(&s_status_lock);
            ESP_LOGI(TAG,
                     "stream health: i2s_underruns=%u starvations=%u decode_stalls=%u "
                     "min_backlog=%u/%u (%ums) pcm=%u%% of realtime",
                     underruns - last_underruns, starvations, decode_stalls,
                     (unsigned int)min_available, (unsigned int)RADIO_DIRECT_INPUT_SIZE,
                     (unsigned int)radio_prebuffer_millis(min_available, bitrate),
                     expected > 0U ? (unsigned int)((pcm_delta * 100U) / expected) : 0U);
            last_pcm_bytes = radio->pcm_bytes;
            last_underruns = underruns;
            starvations = 0U;
            decode_stalls = 0U;
            min_available = RADIO_DIRECT_INPUT_SIZE;
            next_report = xTaskGetTickCount() + pdMS_TO_TICKS(RADIO_STARVATION_REPORT_MS);
        }

        if (radio->track_chain &&
            atomic_exchange_explicit(&radio->skip_requested, false, memory_order_acq_rel)) {
            /* A skip has to be immediate, and the backlog is a second of the
             * track being skipped - so unlike the end of a track, this throws
             * it away rather than playing it out. */
            available = 0U;
            compressed_offset = 0U;
            track_ended = true;
        }
        bool need_input = available == 0U;
        /* The current track's body has ended. Its bytes are still in the
         * backlog, so the next link is not opened until the decoder has
         * finished them: opening earlier would hand the tail of one track to a
         * decoder that has just been reset for the next one, which sounds like
         * a burst of noise and loses the end of the track. */
        if (track_ended && need_input) {
            if (!radio_direct_advance(radio)) {
                fatal = !atomic_load_explicit(&radio->direct_stop_requested,
                                              memory_order_acquire);
                break;
            }
            track_ended = false;
            compressed_offset = 0U;
            decode_error_retries = 0U;
            decoder_reset_tried = false;
            last_pcm_tick = xTaskGetTickCount();
            continue;
        }
        if (available < min_available) {
            min_available = available;
        }
        atomic_store_explicit(&radio->input_fill_percent,
                              radio_prebuffer_percent(available, RADIO_DIRECT_INPUT_SIZE),
                              memory_order_relaxed);
        if (need_input && radio->output_started) {
            ++starvations;
        }
        if (!need_input) {
            size_t consumed = 0U;
            size_t pcm_bytes = 0U;
            radio_decoder_info_t info = {0};
            const radio_decoder_result_t result = radio_decoder_decode(
                radio->decoder, compressed + compressed_offset, available, pcm,
                pcm_capacity, &consumed, &pcm_bytes, &info);
            if (consumed > available) {
                fatal = true;
                break;
            }
            if (consumed > 0U) {
                available -= consumed;
                // Just advance the read cursor instead of shifting the
                // remaining backlog down on every decode call (a single MP3
                // frame consumes ~100-400 bytes out of a 16 KB buffer); the
                // buffer is only compacted lazily, when a network refill
                // would otherwise run out of trailing room.
                compressed_offset += consumed;
            }
            if (result == RADIO_DECODER_HEADER_READY) {
                decode_error_retries = 0U;
                if (info.pcm_frame_bytes > pcm_capacity) {
                    uint8_t *grown = heap_caps_malloc(info.pcm_frame_bytes,
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (grown == NULL) {
                        grown = heap_caps_malloc(info.pcm_frame_bytes,
                                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                    }
                    if (grown == NULL) {
                        ESP_LOGE(TAG, "no memory for a %u byte frame",
                                 (unsigned int)info.pcm_frame_bytes);
                        fatal = true;
                        break;
                    }
                    ESP_LOGI(TAG, "output buffer grown from %u to %u bytes for this stream",
                             (unsigned int)pcm_capacity, (unsigned int)info.pcm_frame_bytes);
                    heap_caps_free(pcm);
                    pcm = grown;
                    pcm_capacity = info.pcm_frame_bytes;
                }
                if (radio_direct_update_info(radio, &info) != ESP_OK) {
                    ESP_LOGE(TAG, "failed to configure audio output for %u Hz",
                             (unsigned int)info.sample_rate);
                    fatal = true;
                    break;
                }
                radio_log_decoder_info(radio->stream_format, &info, &logged_info);
            } else if (result == RADIO_DECODER_PCM_READY) {
                decode_error_retries = 0U;
                if (radio_direct_update_info(radio, &info) != ESP_OK) {
                    ESP_LOGE(TAG, "failed to configure audio output for %u Hz",
                             (unsigned int)info.sample_rate);
                    fatal = true;
                    break;
                }
                radio_log_decoder_info(radio->stream_format, &info, &logged_info);
                xSemaphoreTake(radio->direct_io_mutex, portMAX_DELAY);
                const bool output_allowed =
                    !atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire) &&
                    !atomic_load_explicit(&radio->direct_paused, memory_order_acquire) &&
                    pcm_bytes > 0U;
                const esp_err_t output_result = output_allowed
                                                    ? radio_pcm_output(radio, pcm, pcm_bytes)
                                                    : ESP_OK;
                xSemaphoreGive(radio->direct_io_mutex);
                if (output_result != ESP_OK) {
                    fatal = true;
                    break;
                }
                need_input = false;
            } else if (result == RADIO_DECODER_NEED_MORE_DATA) {
                need_input = true;
            } else {
                if (radio->stream_format == RADIO_STREAM_FORMAT_MP3 && consumed == 0U &&
                    pcm_bytes == 0U && decode_error_retries < 100U) {
                    ++decode_error_retries;
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue;
                }
                ESP_LOGE(TAG, "direct decoder error: codec=%s result=%d available=%u consumed=%u pcm=%u",
                         radio_stream_format_codec_name(radio->stream_format), (int)result,
                         (unsigned int)available, (unsigned int)consumed,
                         (unsigned int)pcm_bytes);
                fatal = true;
                break;
            }

            if (pcm_bytes > 0U) {
                last_pcm_tick = xTaskGetTickCount();
                decoder_reset_tried = false;
            } else if (internet_radio_decode_stalled(
                           (uint32_t)((xTaskGetTickCount() - last_pcm_tick) * portTICK_PERIOD_MS),
                           available, RADIO_DIRECT_DECODE_STALL_MS)) {
                ++decode_stalls;
                if (!decoder_reset_tried) {
                    // Try the decoder before the network, because the decoder is
                    // what this usually is. An MP3 frame can carry part of its
                    // main data in earlier frames, so once a frame is skipped the
                    // ones referring back to it fail too, and skipping those keeps
                    // the reservoir broken - the failure feeds itself and never
                    // ends. Clearing the decoder's state is enough to break that,
                    // and costs a fraction of what reopening the stream costs.
                    ESP_LOGW(TAG,
                             "decoder produced nothing for %ums with %u bytes buffered; "
                             "resetting the decoder",
                             (unsigned int)RADIO_DIRECT_DECODE_STALL_MS,
                             (unsigned int)available);
                    decoder_reset_tried = true;
                    radio_decoder_reset(radio->decoder);
                    last_pcm_tick = xTaskGetTickCount();
                    continue;
                }
                // The decoder was already given a clean start and still produces
                // nothing, so the fault is in what it is being fed. Reconnecting
                // reopens the stream and resets the decoder again with it.
                ESP_LOGW(TAG, "decoder still silent after a reset; reconnecting");
                if (!radio_direct_reconnect(radio)) {
                    fatal = !atomic_load_explicit(&radio->direct_stop_requested,
                                                  memory_order_acquire);
                    break;
                }
                available = 0U;
                compressed_offset = 0U;
                decode_error_retries = 0U;
                decoder_reset_tried = false;
                last_pcm_tick = xTaskGetTickCount();
                continue;
            }
        }

        // Read whenever the backlog is below target, not only once the decoder
        // has drained it to empty. Refilling from empty is what kept
        // min_backlog at a few bytes on a link delivering well above real
        // time, leaving the 93 ms of I2S DMA as the only real cushion.
        const radio_prebuffer_plan_t plan =
            radio_prebuffer_plan(&prebuffer, available, need_input);
        if (plan.should_read && !track_ended) {
            const size_t room = plan.max_bytes;
            const int request_length = (int)(room < RADIO_DIRECT_NETWORK_CHUNK
                                                 ? room
                                                 : RADIO_DIRECT_NETWORK_CHUNK);
            // Only a starved decoder may wait on the socket; a top-up takes
            // what has already arrived and returns to decoding.
            const int received =
                plan.budget_ms == 0U
                    ? radio_stream_read_ready(radio, network, request_length, 0U)
                    : radio_stream_read_blocking(radio, network, request_length);
            if (received == 0 && plan.budget_ms == 0U) {
                // Nothing buffered yet and the decoder is not waiting: keep
                // decoding rather than spinning on an idle socket.
                continue;
            }
            if (received <= 0) {
                if (atomic_load_explicit(&radio->direct_stop_requested,
                                         memory_order_acquire)) {
                    break;
                }
                if (atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
                    continue;
                }
                if (radio->track_chain) {
                    /* Complete or not, this track is over. A connection that
                     * died mid-track costs its tail and no more: the chain
                     * moves on, where reconnecting to a link that expires
                     * after a minute would usually fail anyway. */
                    if (!esp_http_client_is_complete_data_received(radio->http)) {
                        ESP_LOGW(TAG, "track ended early; moving to the next one");
                    }
                    track_ended = true;
                    continue;
                }
                if (radio_direct_reconnect(radio)) {
                    available = 0U;
                    compressed_offset = 0U;
                    decode_error_retries = 0U;
                    continue;
                }
                if (atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
                    continue;
                }
                fatal = !atomic_load_explicit(&radio->direct_stop_requested,
                                              memory_order_acquire);
                break;
            }
            /* Counted before the ICY layer takes its bytes out, because a
             * Range is in body coordinates. HLS is left out: its reads are
             * segments, and a resumed byte offset would mean nothing across
             * them - a live playlist is not something a pause holds a place
             * in anyway. */
            if (radio->track_chain && !radio->hls.active) {
                radio->track_offset += (size_t)received;
            }
            int chunk = received;
            const TickType_t topup_started = xTaskGetTickCount();
            for (;;) {
                size_t audio_length = 0U;
                const icy_metadata_result_t icy_result = icy_metadata_feed(
                    &radio->icy, network, (size_t)chunk, audio, RADIO_DIRECT_NETWORK_CHUNK,
                    &audio_length);
                if (icy_result != ICY_METADATA_OK) {
                    ESP_LOGE(TAG, "ICY metadata processing failed: result=%d received=%d",
                             (int)icy_result, chunk);
                    fatal = true;
                    break;
                }
                if (audio_length > RADIO_DIRECT_INPUT_SIZE - available) {
                    ESP_LOGE(TAG, "direct compressed buffer overflow: audio=%u available=%u",
                             (unsigned int)audio_length, (unsigned int)available);
                    fatal = true;
                    break;
                }
                if (audio_length > 0U) {
                    if (compressed_offset + available + audio_length > RADIO_DIRECT_INPUT_SIZE) {
                        // Trailing room ran out; reclaim the space already
                        // freed by consumed frames with one compaction instead
                        // of shifting on every decode call.
                        if (available > 0U) {
                            memmove(compressed, compressed + compressed_offset, available);
                        }
                        compressed_offset = 0U;
                    }
                    memcpy(compressed + compressed_offset + available, audio, audio_length);
                    available += audio_length;
                }

                // Drain whatever else the socket already holds, bounded by
                // time rather than by a chunk count: what must not happen is
                // spending longer here than the DMA can cover, and that is a
                // duration. An early exit is cheap - the next pass reads again
                // as soon as the decoder has produced a block.
                if (available >= prebuffer.target ||
                    (uint32_t)((xTaskGetTickCount() - topup_started) *
                               portTICK_PERIOD_MS) >= RADIO_DIRECT_TOPUP_BUDGET_MS) {
                    break;
                }
                const size_t free_room = RADIO_DIRECT_INPUT_SIZE - available;
                const int ready_length = (int)(free_room < RADIO_DIRECT_NETWORK_CHUNK
                                                   ? free_room
                                                   : RADIO_DIRECT_NETWORK_CHUNK);
                chunk = radio_stream_read_ready(radio, network, ready_length, 0U);
                if (chunk <= 0) break;
            }
            if (fatal) break;
        } else if (internet_radio_input_buffer_stalled(need_input, available,
                                                       RADIO_DIRECT_INPUT_SIZE)) {
            ESP_LOGE(TAG, "decoder needs more data but compressed buffer is full");
            fatal = true;
        }
    }

    if (fatal &&
        !atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
        ESP_LOGE(TAG, "direct decoder stopped with an error");
        (void)radio_status_apply(radio, INTERNET_RADIO_EVENT_FATAL);
        (void)radio_sync_output(radio);
    }
    (void)board_audio_set_enabled(false);
    // Nothing is buffered once the task is gone; leaving the last reading in
    // place would show a healthy buffer on a stopped player.
    atomic_store_explicit(&radio->input_fill_percent, 0U, memory_order_relaxed);
    radio_stream_close(radio);
    radio_decoder_destroy(radio->decoder);
    radio->decoder = NULL;
    heap_caps_free(compressed);
    heap_caps_free(network);
    heap_caps_free(audio);
    heap_caps_free(pcm);
    /* The handle belongs to whoever started this task, and clearing it here
     * raced with them writing it: the store happens after xTaskCreate returns,
     * so a task that failed early cleared a handle that had not been published
     * yet and the creator then restored a dead one. internet_radio_stop() owns
     * it now and clears it once this semaphore says the task is gone. Every
     * reader is really asking whether the session is live, which is what the
     * control side knows anyway. */
    if (radio->direct_done != NULL) {
        xSemaphoreGive(radio->direct_done);
    }
    vTaskDelete(NULL);
}

static esp_err_t radio_http_event(esp_http_client_event_t *event)
{
    internet_radio_context_t *radio = event->user_data;
    if (radio == NULL || event->event_id != HTTP_EVENT_ON_HEADER || event->header_key == NULL ||
        event->header_value == NULL) {
        return ESP_OK;
    }
    if (strcasecmp(event->header_key, "icy-metaint") == 0) {
        radio->icy_interval = (size_t)strtoul(event->header_value, NULL, 10);
    } else if (strcasecmp(event->header_key, "icy-br") == 0) {
        const uint16_t bitrate = radio_stream_bitrate_kbps_from_icy_header(event->header_value);
        if (bitrate > 0U) {
            taskENTER_CRITICAL(&s_status_lock);
            radio->status.bitrate_kbps = bitrate;
            taskEXIT_CRITICAL(&s_status_lock);
        }
    } else if (strcasecmp(event->header_key, "Content-Type") == 0) {
        // The server knows what it is sending; the URL suffix is only a guess
        // and misses streams served from extensionless or oddly named paths.
        radio_stream_format_t format = radio->stream_format;
        if (radio_stream_format_from_content_type(event->header_value, &format) &&
            format != radio->stream_format) {
            ESP_LOGI(TAG, "codec from Content-Type '%s': %s (URL suggested %s)",
                     event->header_value, radio_stream_format_codec_name(format),
                     radio_stream_format_codec_name(radio->stream_format));
            radio->stream_format = format;
        }
    } else if (strcasecmp(event->header_key, "icy-name") == 0) {
        taskENTER_CRITICAL(&s_status_lock);
        snprintf(radio->status.station, sizeof(radio->status.station), "%s", event->header_value);
        taskEXIT_CRITICAL(&s_status_lock);
    }
    return ESP_OK;
}

/* Opens `url` into radio->http and follows redirects, leaving the response
 * body ready to read. Split out of radio_http_open() because an HLS session
 * runs this once per segment, and must not reset the per-station counters and
 * status that opening a stream does. */
static esp_err_t radio_http_connect(internet_radio_context_t *radio, const char *url)
{
    if (url == NULL || url[0] == '\0') return ESP_ERR_INVALID_ARG;
    const esp_http_client_config_t config = {
        .url = url,
        .buffer_size = RADIO_HTTP_BUFFER_SIZE,
        .buffer_size_tx = RADIO_HTTP_TX_BUFFER_SIZE,
        .timeout_ms = 10000,
        .keep_alive_enable = false,
        .user_agent = "VLC/3.0",
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = radio_http_event,
        .user_data = radio,
    };
    radio->http = esp_http_client_init(&config);
    if (radio->http == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_header(radio->http, "Icy-MetaData", "1");
    radio->range_granted = false;
    if (err == ESP_OK && radio->range_offset > 0U) {
        char range[32];
        snprintf(range, sizeof(range), "bytes=%u-", (unsigned int)radio->range_offset);
        err = esp_http_client_set_header(radio->http, "Range", range);
    }
    unsigned int redirects = 0U;
    while (err == ESP_OK) {
        // MALLOC_CAP_DMA is reported separately and deliberately: it is a
        // strict subset of the internal pool (PSRAM never carries this flag,
        // even on targets whose GDMA can reach it), and it is the pool the
        // hardware AES driver allocates its DMA descriptors from on every TLS
        // record. An HTTPS stream can therefore fail while the internal
        // figures still look healthy.
        const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        ESP_LOGI(TAG,
                 "HTTP open heap: internal_free=%u largest_block=%u "
                 "dma_free=%u dma_largest_block=%u",
                 (unsigned int)heap_caps_get_free_size(internal_caps),
                 (unsigned int)heap_caps_get_largest_free_block(internal_caps),
                 (unsigned int)heap_caps_get_free_size(MALLOC_CAP_DMA),
                 (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
        err = esp_http_client_open(radio->http, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP open failed after %u redirects: %s", redirects,
                     esp_err_to_name(err));
            int tls_error = 0;
            int tls_flags = 0;
            if (esp_http_client_get_and_clear_last_tls_error(radio->http, &tls_error,
                                                              &tls_flags) == ESP_OK &&
                (tls_error != 0 || tls_flags != 0)) {
                ESP_LOGE(TAG, "TLS details: error=%d flags=0x%x", tls_error, tls_flags);
            }
            break;
        }
        if (esp_http_client_fetch_headers(radio->http) < 0) {
            err = ESP_FAIL;
            ESP_LOGE(TAG, "HTTP header fetch failed after %u redirects", redirects);
            break;
        }

        const int status_code = esp_http_client_get_status_code(radio->http);
        if (!radio_http_status_is_redirect(status_code)) {
            const radio_http_body_t body = radio_http_body_kind(status_code);
            if (body == RADIO_HTTP_BODY_FAILED) {
                err = ESP_FAIL;
                ESP_LOGE(TAG, "HTTP status %d", status_code);
            }
            radio->range_granted = body == RADIO_HTTP_BODY_PARTIAL;
            break;
        }
        if (redirects >= RADIO_HTTP_MAX_REDIRECTS) {
            err = ESP_FAIL;
            ESP_LOGE(TAG, "HTTP redirect limit reached; status=%d", status_code);
            break;
        }
        ESP_LOGI(TAG, "HTTP redirect %u: status=%d", redirects + 1U, status_code);
        err = esp_http_client_set_redirection(radio->http);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP redirect target unavailable: %s", esp_err_to_name(err));
            break;
        }
        err = esp_http_client_close(radio->http);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP redirect close failed: %s", esp_err_to_name(err));
            break;
        }
        ++redirects;
    }
    if (err != ESP_OK) {
        radio_http_close(radio);
    }
    return err;
}

/* Resets the per-station counters and publishes the codec. Everything here is
 * once per station, not once per HTTP response. */
static void radio_stream_reset_status(internet_radio_context_t *radio)
{
    radio->icy_interval = 0;
    // A body about to be read from the start, unless the open that follows
    // asked for a Range and got one - which is where this is set again.
    radio->track_offset = 0U;
    radio->pcm_bytes = 0;
    radio->pcm_blocks = 0;
    radio->output_logged = false;
    radio->output_started = false;
    radio->output_sample_rate = 0U;
    taskENTER_CRITICAL(&s_status_lock);
    radio->status.bitrate_kbps = 0U;
    radio->status.sample_rate_hz = 0U;
    snprintf(radio->status.codec, sizeof(radio->status.codec), "%s",
             radio_stream_format_codec_name(radio->stream_format));
    taskEXIT_CRITICAL(&s_status_lock);
}

static esp_err_t radio_http_open(internet_radio_context_t *radio, const char *url)
{
    radio_stream_reset_status(radio);
    const esp_err_t err = radio_http_connect(radio, url);
    if (err != ESP_OK) return err;

    icy_metadata_init(&radio->icy, radio->icy_interval, radio_set_title, radio);
    // Headers have been parsed by now, so Content-Type may have corrected the
    // format guessed from the URL; republish the codec name to match.
    taskENTER_CRITICAL(&s_status_lock);
    snprintf(radio->status.codec, sizeof(radio->status.codec), "%s",
             radio_stream_format_codec_name(radio->stream_format));
    taskEXIT_CRITICAL(&s_status_lock);
    ESP_LOGI(TAG, "HTTP connected: status=%d icy-metaint=%u codec=%s",
             esp_http_client_get_status_code(radio->http), (unsigned int)radio->icy_interval,
             radio_stream_format_codec_name(radio->stream_format));
    return ESP_OK;
}

/* ---------------------------------------------------------------- HLS ----
 *
 * An .m3u8 station is an index, not a stream: a text file naming a handful of
 * short files that the server keeps replacing as time passes. Playing it means
 * fetching segments back to back and re-fetching the index when the queue runs
 * dry, while the decode loop upstream still believes it is reading one endless
 * socket. That illusion is what these functions provide - radio_hls_read()
 * has the same contract as radio_http_read_*(), so nothing above it changes.
 *
 * Everything goes over one keep-alive connection, deliberately. The first
 * version opened a fresh client per segment and the TLS handshake - several
 * hundred milliseconds at this signal strength - blocked the decode loop once
 * every six seconds, far longer than the ~93 ms of I2S DMA, so the DAC ran dry
 * on every segment boundary. Reusing the connection turns that into one
 * request on an open socket.
 *
 * The parsing lives in hls_playlist.c, where it is host-tested; only the
 * fetching and the sequencing are here.
 */

static void radio_hls_stop(internet_radio_context_t *radio)
{
    radio->hls.active = false;
    radio->hls.segment_open = false;
    radio->hls.request_sent = false;
    radio->hls.lead_length = 0U;
    radio->hls.lead_offset = 0U;
    if (radio->hls.playlist != NULL) {
        heap_caps_free(radio->hls.playlist);
        radio->hls.playlist = NULL;
    }
    if (radio->hls.text != NULL) {
        heap_caps_free(radio->hls.text);
        radio->hls.text = NULL;
    }
}

/* Puts a GET on the wire and returns without waiting for the answer.
 *
 * This split is the whole trick. Opening a segment used to be one blocking
 * call, and the round trip - 77 to 180 ms measured on the device - stopped the
 * decode loop for longer than the ~93 ms the I2S DMA holds, so the DAC ran dry
 * at every segment boundary. Writing the request is fast on an already
 * connected socket; only the reply costs a round trip, and the decoder can
 * spend that time on the two seconds of audio already in its buffer. */
static esp_err_t radio_hls_send_request(internet_radio_context_t *radio, const char *url)
{
    esp_err_t err = esp_http_client_set_url(radio->http, url);
    if (err != ESP_OK) return err;
    err = esp_http_client_open(radio->http, 0);
    if (err != ESP_OK) {
        // The pooled socket may have been closed by the server while idle.
        // One reconnect covers that; a second failure is a real one.
        esp_http_client_close(radio->http);
        err = esp_http_client_open(radio->http, 0);
        if (err != ESP_OK) return err;
    }
    radio->hls.request_sent = true;
    return ESP_OK;
}

/* True once the reply to the outstanding request has started to arrive.
 *
 * select() is exact here, unlike in the middle of a transfer: nothing is
 * waiting inside esp-tls, because the reply has not begun. */
static bool radio_hls_response_ready(internet_radio_context_t *radio, uint32_t budget_ms)
{
    const int fd = esp_http_client_get_socket(radio->http);
    if (fd < 0) return true;
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    struct timeval timeout = {
        .tv_sec = (time_t)(budget_ms / 1000U),
        .tv_usec = (suseconds_t)((budget_ms % 1000U) * 1000U),
    };
    return select(fd + 1, &readable, NULL, NULL, &timeout) > 0;
}

/* Reads the response headers of the outstanding request. Only called once the
 * reply is known to have arrived, so it does not wait on the network. */
static esp_err_t radio_hls_collect_headers(internet_radio_context_t *radio)
{
    radio->hls.request_sent = false;
    if (esp_http_client_fetch_headers(radio->http) < 0) {
        esp_http_client_close(radio->http);
        return ESP_FAIL;
    }
    const int status = esp_http_client_get_status_code(radio->http);
    if (radio_http_status_is_redirect(status)) {
        if (esp_http_client_set_redirection(radio->http) != ESP_OK ||
            esp_http_client_open(radio->http, 0) != ESP_OK ||
            esp_http_client_fetch_headers(radio->http) < 0) {
            esp_http_client_close(radio->http);
            return ESP_FAIL;
        }
    } else if (status != 200) {
        ESP_LOGW(TAG, "HLS request status %d", status);
        esp_http_client_close(radio->http);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* Drains the body of a playlist response and parses it. */
static esp_err_t radio_hls_read_playlist_body(internet_radio_context_t *radio)
{
    size_t length = 0U;
    while (length + 1U < RADIO_HLS_TEXT_SIZE) {
        if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
            return ESP_ERR_INVALID_STATE;
        }
        const int received = esp_http_client_read(radio->http, radio->hls.text + length,
                                                  (int)(RADIO_HLS_TEXT_SIZE - 1U - length));
        if (received < 0) return ESP_FAIL;
        if (received == 0) break;
        length += (size_t)received;
    }
    radio->hls.text[length] = '\0';
    if (!hls_playlist_parse(radio->hls.text, length, radio->hls.playlist)) {
        ESP_LOGE(TAG, "HLS playlist parse failed: %u bytes", (unsigned int)length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

/* Blocking playlist fetch, used only while starting a session - there is no
 * audio to protect yet, so the round trip costs nothing. */
static esp_err_t radio_hls_load_playlist(internet_radio_context_t *radio, const char *url)
{
    esp_err_t err = radio_hls_send_request(radio, url);
    if (err != ESP_OK) return err;
    (void)radio_hls_response_ready(radio, RADIO_HLS_TIMEOUT_MS);
    err = radio_hls_collect_headers(radio);
    if (err != ESP_OK) return err;
    return radio_hls_read_playlist_body(radio);
}

/* Opens an HLS session: create the connection, fetch the playlist, follow a
 * master playlist to its variant, and pick the segment to join at. No segment
 * is opened here - the read path fetches the first one like every other. */
static esp_err_t radio_hls_start(internet_radio_context_t *radio, const char *url)
{
    radio_hls_stop(radio);
    radio_http_close(radio);
    radio->hls.playlist = heap_caps_malloc(sizeof(hls_playlist_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (radio->hls.playlist == NULL) {
        radio->hls.playlist = heap_caps_malloc(sizeof(hls_playlist_t),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    radio->hls.text = heap_caps_malloc(RADIO_HLS_TEXT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (radio->hls.text == NULL) {
        radio->hls.text = heap_caps_malloc(RADIO_HLS_TEXT_SIZE,
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (radio->hls.playlist == NULL || radio->hls.text == NULL) {
        ESP_LOGE(TAG, "HLS buffer allocation failed");
        radio_hls_stop(radio);
        return ESP_ERR_NO_MEM;
    }
    if (snprintf(radio->hls.media_url, sizeof(radio->hls.media_url), "%s", url) >=
        (int)sizeof(radio->hls.media_url)) {
        ESP_LOGE(TAG, "HLS playlist URL too long");
        radio_hls_stop(radio);
        return ESP_ERR_INVALID_ARG;
    }

    const esp_http_client_config_t config = {
        .url = url,
        .buffer_size = RADIO_HTTP_BUFFER_SIZE,
        .timeout_ms = RADIO_HLS_TIMEOUT_MS,
        // The whole point: one handshake for the session, not one per segment.
        .keep_alive_enable = true,
        .user_agent = "VLC/3.0",
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = radio_http_event,
        .user_data = radio,
    };
    radio->http = esp_http_client_init(&config);
    if (radio->http == NULL) {
        radio_hls_stop(radio);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = radio_hls_load_playlist(radio, radio->hls.media_url);
    if (err == ESP_OK && radio->hls.playlist->is_master) {
        char variant[RADIO_HLS_URL_MAX];
        if (!hls_url_resolve(radio->hls.media_url, radio->hls.playlist->variant_uri, variant,
                             sizeof(variant))) {
            ESP_LOGE(TAG, "HLS variant URL does not fit");
            err = ESP_ERR_INVALID_ARG;
        } else {
            ESP_LOGI(TAG, "HLS variant selected: %u bps",
                     (unsigned int)radio->hls.playlist->variant_bandwidth);
            snprintf(radio->hls.media_url, sizeof(radio->hls.media_url), "%s", variant);
            err = radio_hls_load_playlist(radio, radio->hls.media_url);
            if (err == ESP_OK && radio->hls.playlist->is_master) {
                ESP_LOGE(TAG, "HLS variant is another master playlist");
                err = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    if (err == ESP_OK && radio->hls.playlist->segment_count == 0U) {
        ESP_LOGE(TAG, "HLS playlist has no segments");
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err != ESP_OK) {
        radio_http_close(radio);
        radio_hls_stop(radio);
        return err;
    }

    radio->hls.target_duration_ms = radio->hls.playlist->target_duration_ms > 0U
                                        ? radio->hls.playlist->target_duration_ms
                                        : RADIO_HLS_DEFAULT_TARGET_MS;
    radio->hls.next_sequence = hls_playlist_live_start(radio->hls.playlist);
    radio->hls.refresh_allowed = xTaskGetTickCount();
    radio->hls.last_segment = xTaskGetTickCount();
    radio->hls.segment_open = false;
    radio->hls.request_sent = false;
    radio->hls.lead_length = 0U;
    radio->hls.lead_offset = 0U;
    radio->hls.active = true;

    // The segment names carry the codec; the playlist URL says nothing about
    // it, so without this every HLS station would be decoded as MP3.
    radio->stream_format = radio_stream_format_from_url(radio->hls.playlist->segments[0].uri);
    radio_stream_reset_status(radio);
    // HLS carries no ICY metadata; an interval of 0 makes the feed a
    // pass-through so the decode loop needs no special case.
    icy_metadata_init(&radio->icy, 0U, radio_set_title, radio);
    ESP_LOGI(TAG, "HLS session: %u segments, target %u ms, joining at %llu, codec=%s",
             (unsigned int)radio->hls.playlist->segment_count,
             (unsigned int)radio->hls.target_duration_ms,
             (unsigned long long)radio->hls.next_sequence,
             radio_stream_format_codec_name(radio->stream_format));
    return ESP_OK;
}

/* Discards the ID3 tag every elementary-AAC segment starts with. The tag has
 * to go before the decoder sees the bytes - it looks for an ADTS sync word -
 * and this is the only place that knows where a segment begins. */
static esp_err_t radio_hls_consume_lead(internet_radio_context_t *radio)
{
    radio->hls.lead_length = 0U;
    radio->hls.lead_offset = 0U;
    // esp_http_client_read() returns short only at the end of the body, so a
    // segment with fewer than ten bytes in it is broken rather than slow.
    const int received = esp_http_client_read(radio->http, (char *)radio->hls.lead, 10);
    if (received < 10) return ESP_FAIL;

    const size_t prefix = hls_id3_prefix_length(radio->hls.lead, (size_t)received);
    if (prefix == 0U) {
        // No tag: those ten bytes are audio, so hand them to the decoder.
        radio->hls.lead_length = (size_t)received;
        return ESP_OK;
    }

    size_t remaining = prefix - (size_t)received;
    char scratch[128];
    while (remaining > 0U) {
        const int want = (int)(remaining < sizeof(scratch) ? remaining : sizeof(scratch));
        const int skipped = esp_http_client_read(radio->http, scratch, want);
        if (skipped <= 0) return ESP_FAIL;
        remaining -= (size_t)skipped;
    }
    return ESP_OK;
}

/* Puts the next request on the wire: a segment when one is published, a
 * playlist refresh when the queue has run dry. ESP_ERR_NOT_FOUND means there
 * is nothing to ask for yet - a wait, not a failure. */
static esp_err_t radio_hls_send_next(internet_radio_context_t *radio)
{
    const size_t index = hls_playlist_index_of(radio->hls.playlist, radio->hls.next_sequence);
    if (index == HLS_SEGMENT_NONE) {
        if ((int32_t)(xTaskGetTickCount() - radio->hls.refresh_allowed) < 0) {
            return ESP_ERR_NOT_FOUND;
        }
        // Half the target duration is the RFC 8216 rate for re-reading a
        // playlist that gained nothing last time.
        radio->hls.refresh_allowed =
            xTaskGetTickCount() + pdMS_TO_TICKS(radio->hls.target_duration_ms / 2U);
        radio->hls.request_is_playlist = true;
        return radio_hls_send_request(radio, radio->hls.media_url);
    }

    char url[RADIO_HLS_URL_MAX];
    if (!hls_url_resolve(radio->hls.media_url, radio->hls.playlist->segments[index].uri, url,
                         sizeof(url))) {
        ESP_LOGE(TAG, "HLS segment URL does not fit; skipping sequence %llu",
                 (unsigned long long)radio->hls.next_sequence);
        ++radio->hls.next_sequence;
        return ESP_ERR_NOT_FOUND;
    }
    radio->hls.request_is_playlist = false;
    return radio_hls_send_request(radio, url);
}

/* Collects the reply to the outstanding request. ESP_OK means a segment body
 * is now open; ESP_ERR_NOT_FOUND means the reply was a playlist and the caller
 * should ask again. */
static esp_err_t radio_hls_collect(internet_radio_context_t *radio)
{
    const bool was_playlist = radio->hls.request_is_playlist;
    esp_err_t err = radio_hls_collect_headers(radio);
    if (err != ESP_OK) {
        if (was_playlist) return err;
        // One segment the server will not serve is not a dead station. Step
        // over it - the stall timeout still fires if every segment fails,
        // because it is only cleared by a segment that actually opened.
        ESP_LOGW(TAG, "HLS segment %llu unavailable; skipping",
                 (unsigned long long)radio->hls.next_sequence);
        ++radio->hls.next_sequence;
        return ESP_ERR_NOT_FOUND;
    }

    if (was_playlist) {
        err = radio_hls_read_playlist_body(radio);
        if (err != ESP_OK) return err;
        if (radio->hls.playlist->has_endlist &&
            radio->hls.next_sequence >=
                radio->hls.playlist->media_sequence + radio->hls.playlist->segment_count) {
            ESP_LOGI(TAG, "HLS playlist ended");
            return ESP_FAIL;
        }
        if (radio->hls.next_sequence < radio->hls.playlist->media_sequence) {
            // The window slid past while we were behind: the audio in between
            // is gone, and chasing it would only fall further behind.
            ESP_LOGW(TAG, "HLS fell behind the window: wanted %llu, rejoining at %llu",
                     (unsigned long long)radio->hls.next_sequence,
                     (unsigned long long)radio->hls.playlist->media_sequence);
            radio->hls.next_sequence = radio->hls.playlist->media_sequence;
        }
        return ESP_ERR_NOT_FOUND;
    }

    ++radio->hls.next_sequence;
    radio->hls.last_segment = xTaskGetTickCount();
    radio->hls.segment_open = true;
    if (radio_hls_consume_lead(radio) != ESP_OK) {
        ESP_LOGW(TAG, "HLS segment too short to read; skipping");
        radio->hls.segment_open = false;
        esp_http_client_close(radio->http);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

/* Reads ahead into the open segment without waiting on real time.
 *
 * radio_http_read_ready() cannot be used here. It polls the socket, and over
 * TLS the socket says nothing while esp-tls holds a decrypted record - which
 * is most of the time, since one 16 KB record feeds eight 2 KB reads. The
 * backlog therefore never grew past a single chunk and every segment boundary
 * emptied the DMA.
 *
 * Reading ahead is safe here in a way it is not on a live socket: a segment is
 * a finished file already sitting on the server, so the bytes are a download,
 * not a wait for the next moment of audio to happen. esp_http_client_read()
 * blocks until it fills the request, so the bound is the timeout - buffered
 * bytes come back at once, an empty buffer costs 15 ms and returns whatever
 * had arrived. */
static int radio_hls_read_topup(internet_radio_context_t *radio, uint8_t *buffer, int length)
{
    if (length > (int)RADIO_DIRECT_NETWORK_CHUNK) length = (int)RADIO_DIRECT_NETWORK_CHUNK;
    esp_http_client_set_timeout_ms(radio->http, RADIO_HLS_TOPUP_TIMEOUT_MS);
    const int received = esp_http_client_read(radio->http, (char *)buffer, length);
    esp_http_client_set_timeout_ms(radio->http, RADIO_HLS_TIMEOUT_MS);
    switch (internet_radio_read_classify(received, -ESP_ERR_HTTP_EAGAIN)) {
    case INTERNET_RADIO_READ_DATA:
        return received;
    case INTERNET_RADIO_READ_RETRY:
        return 0;
    case INTERNET_RADIO_READ_CLOSED:
        // May be the end of the segment body; the caller decides by asking
        // whether the whole response arrived.
        return 0;
    default:
        return -1;
    }
}

/* Same contract as radio_http_read_*: bytes read, 0 for nothing available
 * right now, negative for a stream that needs reconnecting. Segment
 * boundaries are invisible to the caller. */
static int radio_hls_read(internet_radio_context_t *radio, uint8_t *buffer, int length,
                          bool blocking)
{
    if (length <= 0) return 0;
    for (;;) {
        if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
            return -1;
        }
        // The bytes read to test for an ID3 tag come first, or the first frame
        // of every untagged segment would be lost.
        if (radio->hls.lead_offset < radio->hls.lead_length) {
            const size_t held = radio->hls.lead_length - radio->hls.lead_offset;
            const size_t take = held < (size_t)length ? held : (size_t)length;
            memcpy(buffer, radio->hls.lead + radio->hls.lead_offset, take);
            radio->hls.lead_offset += take;
            return (int)take;
        }

        if (!radio->hls.segment_open) {
            if ((uint32_t)((xTaskGetTickCount() - radio->hls.last_segment) *
                           portTICK_PERIOD_MS) >= RADIO_HLS_STALL_TIMEOUT_MS) {
                ESP_LOGW(TAG, "HLS published no segment for %u ms", RADIO_HLS_STALL_TIMEOUT_MS);
                return -1;
            }
            if (!radio->hls.request_sent) {
                const esp_err_t err = radio_hls_send_next(radio);
                if (err == ESP_ERR_NOT_FOUND) {
                    // Nothing published yet. Only a starved decoder waits;
                    // anyone else returns to decoding what it already has.
                    if (!blocking) return 0;
                    vTaskDelay(pdMS_TO_TICKS(50));
                    continue;
                }
                if (err != ESP_OK) return -1;
            }
            // The reply is worth waiting for only when there is nothing left
            // to decode. Otherwise return empty-handed and let the loop spend
            // the round trip on the backlog - which is the point of sending
            // the request ahead of needing it.
            if (!radio_hls_response_ready(radio, blocking ? 50U : 0U)) {
                if (!blocking) return 0;
                continue;
            }
            const esp_err_t err = radio_hls_collect(radio);
            if (err == ESP_ERR_NOT_FOUND) continue;
            if (err != ESP_OK) return -1;
        }

        const int received = blocking ? radio_http_read_blocking(radio, buffer, length)
                                      : radio_hls_read_topup(radio, buffer, length);
        if (received > 0) return received;
        if (received < 0) {
            // A segment that dies mid-download is not a dead stream: drop the
            // connection and move on, the next segment is a separate request.
            ESP_LOGW(TAG, "HLS segment read failed; advancing");
            radio->hls.segment_open = false;
            radio->hls.request_sent = false;
            esp_http_client_close(radio->http);
            continue;
        }
        if (!esp_http_client_is_complete_data_received(radio->http)) {
            // A top-up reading nothing means only that the socket is quiet.
            if (!blocking) return 0;
            // A blocking read reaching zero is different: it waits for data
            // and returns zero only when the transport closed, so the body
            // ended short. Retrying would spin the task forever on a segment
            // that has no more bytes to give.
            ESP_LOGW(TAG, "HLS segment truncated; advancing");
            radio->hls.segment_open = false;
            radio->hls.request_sent = false;
            esp_http_client_close(radio->http);
            continue;
        }
        // Body complete. Leave the connection alone - keep-alive is what makes
        // the next segment cost a request instead of a handshake.
        radio->hls.segment_open = false;
    }
}

static esp_err_t radio_stream_open(internet_radio_context_t *radio, const char *url)
{
    if (hls_url_is_playlist(url)) {
        return radio_hls_start(radio, url);
    }
    radio_hls_stop(radio);
    return radio_http_open(radio, url);
}

static void radio_stream_close(internet_radio_context_t *radio)
{
    radio_http_close(radio);
    radio_hls_stop(radio);
}

/* Whether there is a connection to read from. False between a pause closing
 * one and the resume reopening it, which is the only moment the decode loop
 * has to tell apart - a chain playing out the tail of a finished track still
 * has its connection, it simply has nothing left to give. */
static bool radio_stream_is_open(const internet_radio_context_t *radio)
{
    return radio->http != NULL || radio->hls.active;
}

static int radio_stream_read_blocking(internet_radio_context_t *radio, uint8_t *buffer,
                                      int length)
{
    if (radio->hls.active) return radio_hls_read(radio, buffer, length, true);
    return radio_http_read_blocking(radio, buffer, length);
}

static int radio_stream_read_ready(internet_radio_context_t *radio, uint8_t *buffer, int length,
                                   uint32_t budget_ms)
{
    if (radio->hls.active) return radio_hls_read(radio, buffer, length, false);
    return radio_http_read_ready(radio, buffer, length, budget_ms);
}

static bool radio_direct_reconnect(internet_radio_context_t *radio)
{
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    if (status.station_index >= radio->catalog->count ||
        atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire) ||
        atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
        return false;
    }

    xSemaphoreTake(radio->direct_io_mutex, portMAX_DELAY);
    if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire) ||
        atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
        xSemaphoreGive(radio->direct_io_mutex);
        return false;
    }
    (void)radio_status_apply(radio, INTERNET_RADIO_EVENT_FAILURE);
    (void)radio_sync_output(radio);
    radio->output_started = false;
    xSemaphoreGive(radio->direct_io_mutex);

    radio_stream_close(radio);
    radio_decoder_reset(radio->decoder);
    const char *url = radio->catalog->entries[status.station_index].url;
    for (unsigned int attempt = 0U; attempt < RADIO_HTTP_CONNECT_RETRIES; ++attempt) {
        if (!radio_delay_interruptible(radio, RADIO_HTTP_RECONNECT_DELAY_MS)) {
            return false;
        }
        if (!radio_status_apply(radio, INTERNET_RADIO_EVENT_RETRY)) {
            return false;
        }
        const esp_err_t err = radio_stream_open(radio, url);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "HTTP stream reconnected on attempt %u/%u", attempt + 1U,
                     RADIO_HTTP_CONNECT_RETRIES);
            return true;
        }
        if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire) ||
            atomic_load_explicit(&radio->direct_paused, memory_order_acquire)) {
            return false;
        }
        if (!radio_status_apply(radio, INTERNET_RADIO_EVENT_FAILURE)) {
            return false;
        }
        ESP_LOGW(TAG, "HTTP reconnect attempt %u/%u failed: %s", attempt + 1U,
                 RADIO_HTTP_CONNECT_RETRIES, esp_err_to_name(err));
    }
    return false;
}

/* Moves a chain source on to its next track: ask for a link, reset the
 * decoder, open it. Unlike a reconnect this is not a failure, so the state
 * stays PLAYING and the screen shows a new title rather than "reconnecting".
 *
 * The link is fetched here, on the decode task, because it expires in about a
 * minute - fetching it in advance would mean holding a link that dies while
 * the previous track is still playing. It costs three API calls, which the
 * backlog this runs at the end of is there to cover. */
/* Reopens the track a pause closed, at the byte it stopped on.
 *
 * The link is asked for again rather than kept: a signed link is short-lived,
 * and after a pause of any length the one that was playing has usually
 * expired. What comes back names the same track - that is the whole point of
 * a separate source from the one that hands out the next one.
 *
 * The output counters are carried across for the reason radio_direct_advance
 * gives: the I2S channel is still running, and starting a running channel
 * fails in a way the decode task treats as fatal.
 *
 * A server that ignores the Range answers 200 and starts the track again;
 * radio->range_granted says which happened, and the caller throws its backlog
 * away when the answer is no. */
static bool radio_direct_resume_track(internet_radio_context_t *radio)
{
    if (radio->track_reopen == NULL) {
        ESP_LOGW(TAG, "no way to reopen the paused track; moving on");
        return false;
    }
    const char *url = radio->track_reopen();
    if (url == NULL) {
        ESP_LOGW(TAG, "the paused track could not be resolved again");
        return false;
    }
    const bool output_was_started = radio->output_started;
    const bool output_was_logged = radio->output_logged;
    const uint32_t output_rate = radio->output_sample_rate;
    const size_t played_bytes = radio->pcm_bytes;
    const size_t played_blocks = radio->pcm_blocks;
    const size_t resume_offset = radio->track_offset;
    radio->range_offset = resume_offset;
    const esp_err_t err = radio_stream_open(radio, url);
    radio->range_offset = 0U;
    radio->output_started = output_was_started;
    radio->output_logged = output_was_logged;
    radio->output_sample_rate = output_rate;
    radio->pcm_bytes = played_bytes;
    radio->pcm_blocks = played_blocks;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "the paused track did not reopen: %s", esp_err_to_name(err));
        return false;
    }
    // The open reset the counter to zero; the body it returned starts where
    // the pause stopped, unless the Range was refused.
    radio->track_offset = radio->range_granted ? resume_offset : 0U;
    ESP_LOGI(TAG, "resumed the track at %u bytes%s", (unsigned int)resume_offset,
             radio->range_granted ? "" : " - the server ignored it and started over");
    return true;
}

static bool radio_direct_advance(internet_radio_context_t *radio)
{
    if (radio->track_source == NULL) return false;

    for (unsigned int attempt = 0U; attempt < RADIO_HTTP_CONNECT_RETRIES; ++attempt) {
        if (atomic_load_explicit(&radio->direct_stop_requested, memory_order_acquire)) {
            return false;
        }
        if (attempt > 0U && !radio_delay_interruptible(radio, RADIO_HTTP_RECONNECT_DELAY_MS)) {
            return false;
        }
        char title[INTERNET_RADIO_TITLE_MAX_LEN];
        title[0] = '\0';
        const char *url = radio->track_source(title, sizeof(title));
        if (url == NULL) {
            ESP_LOGW(TAG, "no next track on attempt %u/%u", attempt + 1U,
                     RADIO_HTTP_CONNECT_RETRIES);
            continue;
        }
        radio_stream_close(radio);
        radio_decoder_reset(radio->decoder);
        /* Opening a stream resets what the output has done so far, which is
         * right for a new station and wrong here: the I2S channel is still
         * running and must keep running, or there would be a gap between
         * every pair of tracks. Starting an already-started channel fails
         * with ESP_ERR_INVALID_STATE, and the decode task treats a failed
         * PCM write as fatal - so this is not cosmetic, it is the difference
         * between a chain that plays on and one that dies on track two. */
        const bool output_was_started = radio->output_started;
        const bool output_was_logged = radio->output_logged;
        const uint32_t output_rate = radio->output_sample_rate;
        const size_t played_bytes = radio->pcm_bytes;
        const size_t played_blocks = radio->pcm_blocks;
        const esp_err_t err = radio_stream_open(radio, url);
        radio->output_started = output_was_started;
        radio->output_logged = output_was_logged;
        radio->output_sample_rate = output_rate;
        /* Kept for the same reason: the health report measures PCM against
         * elapsed time, and a counter that restarts mid-window underflows
         * into a meaningless percentage. */
        radio->pcm_bytes = played_bytes;
        radio->pcm_blocks = played_blocks;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "next track did not open on attempt %u/%u: %s", attempt + 1U,
                     RADIO_HTTP_CONNECT_RETRIES, esp_err_to_name(err));
            continue;
        }
        /* After the open, which resets the status line the stream reports. */
        radio_set_title(radio, title);
        return true;
    }
    return false;
}

static void radio_load_catalog(internet_radio_context_t *radio)
{
    FILE *file = fopen(STATION_CATALOG_PATH, "r");
    if (file == NULL) {
        radio->catalog->count = 0;
        ESP_LOGW(TAG, "station catalog unavailable: %s", STATION_CATALOG_PATH);
        return;
    }
    char *text = malloc(RADIO_CATALOG_BUFFER_SIZE);
    if (text == NULL) {
        fclose(file);
        radio->catalog->count = 0;
        ESP_LOGE(TAG, "station catalog allocation failed");
        return;
    }
    const size_t bytes_read = fread(text, 1, RADIO_CATALOG_BUFFER_SIZE - 1, file);
    const bool complete = feof(file) != 0;
    fclose(file);
    if (!complete) {
        free(text);
        radio->catalog->count = 0;
        ESP_LOGE(TAG, "station catalog is too large or unreadable");
        return;
    }
    text[bytes_read] = '\0';
    (void)station_catalog_load_text(text, radio->catalog);
    if (station_catalog_append_if_missing(radio->catalog, &s_europa_plus_station)) {
        ESP_LOGI(TAG, "added built-in AAC station to catalog");
    }
    free(text);
    ESP_LOGI(TAG, "loaded %u internet radio stations", (unsigned int)radio->catalog->count);
}

esp_err_t internet_radio_catalog_replace(const char *text, size_t length, size_t *out_count,
                                         bool *out_active_station_removed)
{
    if (out_active_station_removed != NULL) {
        *out_active_station_removed = false;
    }
    if (!s_radio.initialized || text == NULL || length == 0U || length >= RADIO_CATALOG_BUFFER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    char *buffer = malloc(length + 1U);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(buffer, text, length);
    buffer[length] = '\0';

    station_catalog_t *parsed = heap_caps_malloc(sizeof(station_catalog_t),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (parsed == NULL) {
        parsed = heap_caps_malloc(sizeof(station_catalog_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (parsed == NULL) {
        free(buffer);
        return ESP_ERR_NO_MEM;
    }
    (void)station_catalog_load_text(buffer, parsed);

    bool has_content = false;
    for (size_t index = 0U; index < length; ++index) {
        if (!isspace((unsigned char)buffer[index])) {
            has_content = true;
            break;
        }
    }
    if (parsed->count == 0U && has_content) {
        ESP_LOGW(TAG, "playlist replace rejected: no valid station lines found");
        free(buffer);
        heap_caps_free(parsed);
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(STATION_CATALOG_TEMP_PATH, "w");
    if (file == NULL) {
        free(buffer);
        heap_caps_free(parsed);
        return ESP_FAIL;
    }
    const bool write_ok = fwrite(buffer, 1, length, file) == length;
    const bool close_ok = fclose(file) == 0;
    free(buffer);
    if (!write_ok || !close_ok) {
        (void)unlink(STATION_CATALOG_TEMP_PATH);
        heap_caps_free(parsed);
        return ESP_FAIL;
    }
    if (rename(STATION_CATALOG_TEMP_PATH, STATION_CATALOG_PATH) != 0) {
        (void)unlink(STATION_CATALOG_TEMP_PATH);
        heap_caps_free(parsed);
        return ESP_FAIL;
    }

    // Install the new catalog and, if a station is currently playing,
    // re-resolve its index by URL so status/list highlighting stays correct
    // even if the save reordered or removed entries ahead of it.
    //
    // Only the status bookkeeping runs under s_status_lock. The catalog copy
    // (35 KB) and the URL search must stay outside it: taskENTER_CRITICAL
    // disables interrupts on this core, and holding it that long starves the
    // I2S DMA and Wi-Fi ISRs. Readers of s_radio.catalog
    // (internet_radio_station_at() and friends) do not take this lock anyway:
    // the entries array is fixed-size and never reallocated, so a concurrent
    // read during the copy can at worst observe a torn (mixed old/new)
    // station name/url for a single UI refresh, never an out-of-bounds
    // access. That is accepted as a minor, self-correcting cosmetic race
    // rather than full read-side thread-safety.
    taskENTER_CRITICAL(&s_status_lock);
    const size_t playing_index = s_radio.status.station_index;
    taskEXIT_CRITICAL(&s_status_lock);

    char playing_url[STATION_CATALOG_URL_MAX_LEN] = {0};
    bool was_playing = false;
    if (playing_index < s_radio.catalog->count) {
        snprintf(playing_url, sizeof(playing_url), "%s",
                 s_radio.catalog->entries[playing_index].url);
        was_playing = true;
    }

    *s_radio.catalog = *parsed;
    heap_caps_free(parsed);
    size_t new_index = SIZE_MAX;
    if (was_playing) {
        (void)station_catalog_find_by_url(s_radio.catalog, playing_url, &new_index);
    }
    const size_t resulting_count = s_radio.catalog->count;

    // Publish the re-resolved index, unless another task switched stations
    // while the catalog was being installed - that newer choice wins.
    bool must_stop_playing = false;
    taskENTER_CRITICAL(&s_status_lock);
    if (was_playing && s_radio.status.station_index == playing_index) {
        s_radio.status.station_index = new_index;
        must_stop_playing = new_index == SIZE_MAX;
    }
    taskEXIT_CRITICAL(&s_status_lock);

    // Stopping is left to the caller: internet_radio_stop() blocks for up to
    // RADIO_DIRECT_STOP_TIMEOUT_MS waiting for the decoder task, and this
    // function runs on the single esp_http_server worker task, which would
    // stall every other HTTP request and WebSocket broadcast meanwhile.
    if (must_stop_playing) {
        ESP_LOGW(TAG, "playing station removed from playlist; stop requested");
    }
    if (out_active_station_removed != NULL) {
        *out_active_station_removed = must_stop_playing;
    }
    if (out_count != NULL) {
        *out_count = resulting_count;
    }
    ESP_LOGI(TAG, "playlist replaced: %u stations", (unsigned int)resulting_count);
    return ESP_OK;
}

esp_err_t internet_radio_init(void)
{
    if (s_radio.initialized) return ESP_OK;
    s_radio.direct_done = xSemaphoreCreateBinary();
    if (s_radio.direct_done == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_radio.direct_io_mutex = xSemaphoreCreateMutex();
    if (s_radio.direct_io_mutex == NULL) {
        vSemaphoreDelete(s_radio.direct_done);
        s_radio.direct_done = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_radio.catalog = heap_caps_calloc(1U, sizeof(station_catalog_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_radio.catalog == NULL) {
        // A board without PSRAM, or one that has run out of it. The table is
        // the whole station list, so there is nothing to fall back to.
        s_radio.catalog = heap_caps_calloc(1U, sizeof(station_catalog_t),
                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_radio.catalog == NULL) {
        vSemaphoreDelete(s_radio.direct_io_mutex);
        s_radio.direct_io_mutex = NULL;
        vSemaphoreDelete(s_radio.direct_done);
        s_radio.direct_done = NULL;
        ESP_LOGE(TAG, "station catalog allocation failed (%u bytes)",
                 (unsigned int)sizeof(station_catalog_t));
        return ESP_ERR_NO_MEM;
    }
    taskENTER_CRITICAL(&s_status_lock);
    s_radio.status.state = INTERNET_RADIO_STATE_STOPPED;
    s_radio.status.station_index = SIZE_MAX;
    /* Empty rather than a placeholder: before the first station there is
     * nothing to name, and a non-empty string here was handed to everyone who
     * reads the status as if it were a station - the page showed it as the
     * title while the panel was showing the list. */
    s_radio.status.station[0] = '\0';
    taskEXIT_CRITICAL(&s_status_lock);
    radio_load_catalog(&s_radio);
    ESP_RETURN_ON_ERROR(radio_sync_output(&s_radio), TAG, "disable idle I2S output failed");
    s_radio.initialized = true;
    return ESP_OK;
}

esp_err_t internet_radio_start(void)
{
    if (s_radio.catalog->count == 0U) return ESP_ERR_NOT_FOUND;
    return internet_radio_start_station_index(0U) ? ESP_OK : ESP_FAIL;
}

bool internet_radio_start_saved_station(void)
{
    size_t index;
    if (!internet_radio_saved_station_index(&index)) {
        return false;
    }
    return internet_radio_start_station_index(index);
}

bool internet_radio_saved_station_index(size_t *index)
{
    char url[STATION_CATALOG_URL_MAX_LEN];
    return index != NULL && s_radio.initialized && s_radio.catalog->count > 0U &&
           station_resume_load_last_url(STATION_SETTINGS_PATH, url, sizeof(url)) &&
           station_catalog_find_by_url(s_radio.catalog, url, index);
}

/* Brings whatever was playing to a halt before something else starts. Both
 * branches exist because a source can be stopped without its task ever having
 * been created - a station that failed to open leaves the state machine out of
 * STOPPED with nothing running. */
static bool radio_stop_previous(void)
{
    taskENTER_CRITICAL(&s_status_lock);
    const bool have_direct_task = s_radio.direct_task != NULL;
    taskEXIT_CRITICAL(&s_status_lock);
    if (have_direct_task) {
        const esp_err_t stop_result = internet_radio_stop();
        if (stop_result != ESP_OK) {
            ESP_LOGE(TAG, "cannot start: previous decoder did not stop (%s)",
                     esp_err_to_name(stop_result));
            return false;
        }
        return true;
    }
    internet_radio_status_t previous_status;
    internet_radio_get_status(&previous_status);
    if (previous_status.state != INTERNET_RADIO_STATE_STOPPED) {
        if (internet_radio_stop() != ESP_OK) {
            ESP_LOGE(TAG, "cannot start: previous stream did not stop");
            return false;
        }
    }
    return true;
}

/* The half of starting that is the same for a catalog station and for a chain
 * of tracks: open the URL, create the decoder the answer calls for, and hand
 * both to the decode task.
 *
 * `station_index` is SIZE_MAX for a chain, which has no place in the catalog -
 * and that is also what keeps radio_direct_reconnect() from trying to reopen a
 * chain by index. */
static bool radio_start_stream(const char *url, const char *name, size_t station_index,
                               bool track_chain)
{
    const radio_stream_format_t stream_format = radio_stream_format_from_url(url);
    taskENTER_CRITICAL(&s_status_lock);
    const bool started = internet_radio_state_apply(&s_radio.status.state,
                                                     INTERNET_RADIO_EVENT_START);
    if (started) {
        s_radio.status.station_index = station_index;
        snprintf(s_radio.status.station, sizeof(s_radio.status.station), "%.*s",
                 (int)sizeof(s_radio.status.station) - 1, name);
        s_radio.status.title[0] = '\0';
    }
    taskEXIT_CRITICAL(&s_status_lock);
    if (!started) return false;
    s_radio.track_chain = track_chain;
    atomic_store_explicit(&s_radio.skip_requested, false, memory_order_release);
    s_radio.stream_format = stream_format;
    // Clear the stop flag before opening, not after. internet_radio_stop() has
    // already waited for the previous task to exit, so what is left here is a
    // stale true - and opening an HLS station reads a playlist over the
    // network, which honours the flag and would abort every attempt.
    atomic_store_explicit(&s_radio.direct_stop_requested, false, memory_order_release);
    atomic_store_explicit(&s_radio.direct_paused, false, memory_order_release);
    esp_err_t err = ESP_FAIL;
    for (unsigned int attempt = 0U; attempt < RADIO_HTTP_CONNECT_RETRIES; ++attempt) {
        err = radio_stream_open(&s_radio, url);
        if (err == ESP_OK) break;
        if (attempt + 1U < RADIO_HTTP_CONNECT_RETRIES) {
            ESP_LOGW(TAG, "HTTP connect retry %u/%u after %s",
                     attempt + 1U, RADIO_HTTP_CONNECT_RETRIES, esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    if (err != ESP_OK) {
        (void)radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_FATAL);
        (void)radio_sync_output(&s_radio);
        return false;
    }
    // radio_stream_open() has seen the response headers - or, for HLS, the
    // segment names - so the format may no longer be the one guessed from the
    // station URL. The decoder must follow that, not the guess.
    const radio_stream_format_t open_format = s_radio.stream_format;
    if (s_radio.direct_done != NULL) {
        (void)xSemaphoreTake(s_radio.direct_done, 0);
    }
    s_radio.decoder = radio_decoder_create(open_format);
    if (s_radio.decoder == NULL) {
        ESP_LOGE(TAG, "failed to create %s decoder",
                 radio_stream_format_codec_name(open_format));
        (void)radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_FATAL);
        (void)radio_sync_output(&s_radio);
        radio_stream_close(&s_radio);
        return false;
    }
    ESP_LOGI(TAG, "decoder created: internal_free=%u largest_block=%u",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                            MALLOC_CAP_8BIT));
    TaskHandle_t direct_task_handle = NULL;
    /* 10 KB rather than 8: a chain source resolves its next track on this very
     * task, and that is three TLS handshakes deep inside the decode loop.
     * Measured least-seen headroom fell from 5660 to 2676 bytes the first time
     * a chain advanced, and a stack overflow here is a reboot, not a glitch. */
    if (xTaskCreatePinnedToCore(radio_direct_task, "radio_decode", 10240, &s_radio, 6,
                                &direct_task_handle, 1) != pdPASS) {
        ESP_LOGE(TAG, "failed to create direct decoder task");
        radio_decoder_destroy(s_radio.decoder);
        s_radio.decoder = NULL;
        (void)radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_FATAL);
        (void)radio_sync_output(&s_radio);
        radio_stream_close(&s_radio);
        return false;
    }
    taskENTER_CRITICAL(&s_status_lock);
    s_radio.direct_task = direct_task_handle;
    taskEXIT_CRITICAL(&s_status_lock);
    return true;
}

bool internet_radio_skip_track(void)
{
    if (!s_radio.initialized || !s_radio.track_chain) return false;
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    /* Only while something is running. A skip on a stopped or failed chain
     * would set a flag no task is left to read, and the next start would then
     * throw away its own first track. */
    if (status.state != INTERNET_RADIO_STATE_PLAYING &&
        status.state != INTERNET_RADIO_STATE_PAUSED) {
        return false;
    }
    atomic_store_explicit(&s_radio.skip_requested, true, memory_order_release);
    return true;
}

void internet_radio_set_track_source(internet_radio_track_source_fn source)
{
    s_radio.track_source = source;
}

void internet_radio_set_track_reopen(internet_radio_track_reopen_fn reopen)
{
    s_radio.track_reopen = reopen;
}

bool internet_radio_start_track_chain(const char *display_name)
{
    if (!s_radio.initialized || s_radio.track_source == NULL) return false;
    if (!radio_stop_previous()) return false;

    /* The first link is fetched before anything is opened, so a station that
     * cannot produce a track fails here rather than after the screen has
     * already switched to a player that will never start. */
    char title[INTERNET_RADIO_TITLE_MAX_LEN];
    title[0] = '\0';
    const char *url = s_radio.track_source(title, sizeof(title));
    if (url == NULL) {
        ESP_LOGE(TAG, "station %s produced no first track", display_name);
        return false;
    }
    if (!radio_start_stream(url, display_name, SIZE_MAX, true)) return false;
    radio_set_title(&s_radio, title);
    return true;
}

bool internet_radio_start_station_index(size_t index)
{
    if (!s_radio.initialized) return false;
    if (index >= s_radio.catalog->count) return false;
    if (!radio_stop_previous()) return false;
    if (!radio_start_stream(s_radio.catalog->entries[index].url,
                            s_radio.catalog->entries[index].name, index, false)) {
        return false;
    }
    (void)station_resume_save_last_url(STATION_SETTINGS_PATH,
                                       s_radio.catalog->entries[index].url);
    return true;
}

/* A URL that is in no catalogue, played once. The station index stays
 * SIZE_MAX, which is what keeps a reconnect from trying to reopen it by index
 * and what tells everything above that this is not one of the saved stations;
 * the resume point is left alone, so a test does not become what PLAY brings
 * back after a reboot. */
/* Whether any station names this picture. Asked when the playlist is saved,
 * to find the files nothing refers to any more. */
bool internet_radio_icon_in_use(const char *icon)
{
    if (!s_radio.initialized || icon == NULL || icon[0] == '\0') return false;
    bool used = false;
    for (size_t index = 0U; index < s_radio.catalog->count && !used; ++index) {
        used = strcmp(s_radio.catalog->entries[index].icon, icon) == 0;
    }
    return used;
}

bool internet_radio_test_stream(const char *url, const char *name)
{
    if (!s_radio.initialized || url == NULL || url[0] == '\0') return false;
    if (!radio_stop_previous()) return false;
    return radio_start_stream(url, name == NULL ? "" : name, SIZE_MAX, false);
}

esp_err_t internet_radio_stop(void)
{
    if (!s_radio.initialized) return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_status_lock);
    const bool have_direct_task = s_radio.direct_task != NULL;
    taskEXIT_CRITICAL(&s_status_lock);
    if (have_direct_task) {
        atomic_store_explicit(&s_radio.direct_stop_requested, true, memory_order_release);
        atomic_store_explicit(&s_radio.direct_paused, false, memory_order_release);
        if (s_radio.direct_done == NULL ||
            xSemaphoreTake(s_radio.direct_done, pdMS_TO_TICKS(RADIO_DIRECT_STOP_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "timed out waiting for direct decoder task to stop");
            return ESP_ERR_TIMEOUT;
        }
    }
    taskENTER_CRITICAL(&s_status_lock);
    (void)internet_radio_state_apply(&s_radio.status.state, INTERNET_RADIO_EVENT_STOP);
    s_radio.status.station_index = SIZE_MAX;
    /* What the stream called itself, and what it was playing, belong to the
     * stream that just ended. Left behind, they are what the rotor and the
     * file player would be labelled with after a source switch, since both
     * fall back on these fields when they have nothing of their own yet. A
     * station stopped on its own still shows its name - that one comes from
     * the playlist, not from here. */
    s_radio.status.station[0] = '\0';
    s_radio.status.title[0] = '\0';
    /* Cleared only here, and only once the task has confirmed it is gone -
     * including when it had already ended on its own, in which case the
     * semaphore was waiting and the take above returned at once. */
    s_radio.direct_task = NULL;
    taskEXIT_CRITICAL(&s_status_lock);
    const esp_err_t output_result = radio_sync_output(&s_radio);
    radio_http_close(&s_radio);
    return output_result;
}

esp_err_t internet_radio_pause(void)
{
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    if (!s_radio.initialized || status.state != INTERNET_RADIO_STATE_PLAYING) {
        return ESP_ERR_INVALID_STATE;
    }
    taskENTER_CRITICAL(&s_status_lock);
    const bool direct_path = s_radio.direct_task != NULL;
    taskEXIT_CRITICAL(&s_status_lock);
    if (direct_path) {
        atomic_store_explicit(&s_radio.direct_paused, true, memory_order_release);
        xSemaphoreTake(s_radio.direct_io_mutex, portMAX_DELAY);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
    const bool applied = radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_PAUSE);
    const esp_err_t result = applied ? radio_sync_output(&s_radio) : ESP_ERR_INVALID_STATE;
    if (!applied) {
        atomic_store_explicit(&s_radio.direct_paused, false, memory_order_release);
    }
    if (direct_path) {
        xSemaphoreGive(s_radio.direct_io_mutex);
    }
    return result;
}

esp_err_t internet_radio_resume(void)
{
    internet_radio_status_t status;
    internet_radio_get_status(&status);
    if (!s_radio.initialized || status.state != INTERNET_RADIO_STATE_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    taskENTER_CRITICAL(&s_status_lock);
    const bool direct_path = s_radio.direct_task != NULL;
    taskEXIT_CRITICAL(&s_status_lock);
    if (direct_path) {
        xSemaphoreTake(s_radio.direct_io_mutex, portMAX_DELAY);
        s_radio.output_started = false;
        const bool applied = radio_status_apply(&s_radio, INTERNET_RADIO_EVENT_RESUME);
        if (applied) {
            atomic_store_explicit(&s_radio.direct_paused, false, memory_order_release);
        }
        xSemaphoreGive(s_radio.direct_io_mutex);
        return applied ? ESP_OK : ESP_ERR_INVALID_STATE;
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

void internet_radio_get_status(internet_radio_status_t *status)
{
    if (status == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_status_lock);
    *status = s_radio.status;
    taskEXIT_CRITICAL(&s_status_lock);
    status->buffer_percent = (uint8_t)atomic_load_explicit(&s_radio.input_fill_percent,
                                                           memory_order_relaxed);
}

size_t internet_radio_station_count(void)
{
    return s_radio.catalog->count;
}

const station_catalog_entry_t *internet_radio_station_at(size_t index)
{
    return index < s_radio.catalog->count ? &s_radio.catalog->entries[index] : NULL;
}

size_t internet_radio_current_station_index(void)
{
    size_t station_index;
    taskENTER_CRITICAL(&s_status_lock);
    station_index = s_radio.status.station_index;
    taskEXIT_CRITICAL(&s_status_lock);
    return station_index;
}
