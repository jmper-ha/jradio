#include "album_art.h"

#ifdef ESP_PLATFORM

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "image_scale.h"

static const char *TAG = "album_art";

/* Anything larger is refused rather than decoded. The result is 96 pixels
 * across either way, while a 1000x1000 cover costs two megabytes of PSRAM and
 * most of a second of the playback task - which is time the first sample of
 * the track is waiting for. */
#define ALBUM_ART_SOURCE_PIXELS_MAX (800U * 800U)

static uint16_t *s_cover;
static SemaphoreHandle_t s_lock;
static unsigned int s_generation;
static bool s_present;
static uint16_t s_width;
static uint16_t s_height;
/* Identifies the picture that produced the published cover. Every track of an
 * album carries the same bytes, so this is what keeps the tile from being
 * rebuilt - and visibly blinking - on every track change. */
static uint32_t s_source_checksum;
static size_t s_source_length;

static uint32_t checksum_of(const uint8_t *data, size_t length)
{
    // FNV-1a: the picture is already in cache from the read, and this costs
    // less than a millisecond on the 60 KB that a cover runs to.
    uint32_t hash = 2166136261U;
    for (size_t index = 0U; index < length; ++index) {
        hash ^= data[index];
        hash *= 16777619U;
    }
    return hash;
}

static void *alloc_aligned(size_t size)
{
    // PSRAM first for the same reason as the audio buffers: internal SRAM is
    // the scarce one, and a full-size cover is over a hundred kilobytes.
    void *buffer = heap_caps_aligned_alloc(16U, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_aligned_alloc(16U, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buffer;
}

esp_err_t album_art_init(void)
{
    if (s_lock != NULL) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    s_cover = heap_caps_malloc(ALBUM_ART_PIXELS * sizeof(uint16_t),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_cover == NULL) {
        s_cover = heap_caps_malloc(ALBUM_ART_PIXELS * sizeof(uint16_t),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_cover == NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* Reduces straight into the published buffer rather than into a temporary.
 * The temporary would be 18 KB, and the task that calls this has eight of
 * stack; holding the lock for the few milliseconds the resampling takes costs
 * nothing but a poll of the screen. */
static bool reduce_into_cover(const uint16_t *source, uint16_t source_width,
                              uint16_t source_height, const image_rect_t *rect)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool scaled = image_scale_rgb565(source, source_width, source_height, s_cover,
                                           rect->width, rect->height, rect->width);
    if (scaled) {
        s_width = rect->width;
        s_height = rect->height;
        s_present = true;
    } else if (s_present) {
        // Half a cover is not a cover; the screen goes back to its placeholder.
        s_present = false;
    }
    ++s_generation;
    xSemaphoreGive(s_lock);
    return scaled;
}

void album_art_clear(void)
{
    if (s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_present) {
        s_present = false;
        ++s_generation;
    }
    // Forgotten too, so the same cover coming back is decoded rather than
    // assumed to be still on the screen.
    s_source_length = 0U;
    s_source_checksum = 0U;
    xSemaphoreGive(s_lock);
}

album_art_status_t album_art_status(void)
{
    album_art_status_t status = {0U, false, 0U, 0U};
    if (s_lock == NULL) return status;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    status.generation = s_generation;
    status.present = s_present;
    status.width = s_width;
    status.height = s_height;
    xSemaphoreGive(s_lock);
    return status;
}

bool album_art_copy(uint16_t *destination, size_t capacity_pixels, uint16_t *width,
                    uint16_t *height)
{
    if (s_lock == NULL || destination == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t pixels = (size_t)s_width * s_height;
    const bool usable = s_present && pixels > 0U && pixels <= capacity_pixels;
    if (usable) {
        memcpy(destination, s_cover, pixels * sizeof(uint16_t));
        if (width != NULL) *width = s_width;
        if (height != NULL) *height = s_height;
    }
    xSemaphoreGive(s_lock);
    return usable;
}

static bool decode_and_publish(const uint8_t *data, size_t length)
{
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    /* Little-endian RGB565 is what the display takes and what LVGL is built
     * for here, so the decoder writes the final pixel format directly and
     * nothing converts a second time. Scaling is left to us: the decoder can
     * only scale by whole eighths and only when both sides are multiples of
     * eight, which a 250-pixel cover is not. */
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;

    jpeg_dec_handle_t decoder = NULL;
    if (jpeg_dec_open(&config, &decoder) != JPEG_ERR_OK) {
        ESP_LOGW(TAG, "cannot open the JPEG decoder");
        return false;
    }

    // The decoder takes a writable pointer but only reads through it; the
    // bytes belong to the caller's read buffer.
    jpeg_dec_io_t io = {
        .inbuf = (uint8_t *)data,
        .inbuf_len = (int)length,
        .inbuf_remain = 0,
        .outbuf = NULL,
        .out_size = 0,
    };
    jpeg_dec_header_info_t info = {0};
    bool published = false;
    uint8_t *pixels = NULL;

    if (jpeg_dec_parse_header(decoder, &io, &info) != JPEG_ERR_OK) {
        // Progressive JPEG lands here: the decoder is baseline only, and a
        // tagger that saved one is not a fault worth logging as an error.
        ESP_LOGI(TAG, "cover is not a baseline JPEG");
    } else if (info.width == 0U || info.height == 0U ||
               (uint32_t)info.width * info.height > ALBUM_ART_SOURCE_PIXELS_MAX) {
        ESP_LOGW(TAG, "cover %ux%u is outside what is worth decoding",
                 (unsigned int)info.width, (unsigned int)info.height);
    } else {
        int output_length = 0;
        if (jpeg_dec_get_outbuf_len(decoder, &output_length) == JPEG_ERR_OK &&
            output_length > 0) {
            pixels = alloc_aligned((size_t)output_length);
        }
        if (pixels == NULL) {
            ESP_LOGW(TAG, "no memory for a %ux%u cover", (unsigned int)info.width,
                     (unsigned int)info.height);
        } else {
            io.outbuf = pixels;
            if (jpeg_dec_process(decoder, &io) != JPEG_ERR_OK) {
                ESP_LOGW(TAG, "cover failed to decode");
            } else {
                const image_rect_t rect =
                    image_fit_square(info.width, info.height, (uint16_t)ALBUM_ART_SIZE);
                if (reduce_into_cover((const uint16_t *)pixels, info.width, info.height,
                                      &rect)) {
                    published = true;
                    ESP_LOGI(TAG, "cover %ux%u shown as %ux%u", (unsigned int)info.width,
                             (unsigned int)info.height, (unsigned int)rect.width,
                             (unsigned int)rect.height);
                }
            }
        }
    }

    heap_caps_free(pixels);
    (void)jpeg_dec_close(decoder);
    return published;
}

bool album_art_set_jpeg(const uint8_t *data, size_t length)
{
    if (s_lock == NULL || data == NULL || length == 0U) return false;

    const uint32_t checksum = checksum_of(data, length);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool unchanged = s_present && length == s_source_length && checksum == s_source_checksum;
    xSemaphoreGive(s_lock);
    if (unchanged) return true;

    if (!decode_and_publish(data, length)) {
        album_art_clear();
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_source_length = length;
    s_source_checksum = checksum;
    xSemaphoreGive(s_lock);
    return true;
}

#endif /* ESP_PLATFORM */
