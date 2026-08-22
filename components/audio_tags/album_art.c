#include "album_art.h"

#ifdef ESP_PLATFORM

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "image_scale.h"
#include "image_decode.h"
#include "tjpgd.h"

static const char *TAG = "album_art";

/* TJpgDec's scratch: the huffman and dequantiser tables plus one MCU. ChaN
 * puts the requirement near three kilobytes at this optimisation level, and
 * the spare one costs nothing where it is allocated. */
#define ALBUM_ART_WORK_POOL 4096U
/* The decoder descales by halves, so it stops one halving above the tile and
 * the intermediate is normally under 192 pixels a side. A source large enough
 * to exceed this even after 1/8 is refused rather than decoded: the answer
 * would still be 96 pixels across, and the track's first sample is waiting. */
#define ALBUM_ART_DECODED_PIXELS_MAX (256U * 256U)

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

static uint8_t *alloc_buffer(size_t size)
{
    /* PSRAM first for the same reason as the audio buffers, and here it is not
     * only about size: the largest contiguous *internal* block is what mbedtls
     * needs for AES, and a cover that ate into it would take HTTPS stations
     * down with it. */
    uint8_t *buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
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

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
    uint16_t *pixels;
    uint16_t width;
    uint16_t height;
} album_art_decode_t;

static size_t decode_input(JDEC *decoder, uint8_t *buffer, size_t length)
{
    album_art_decode_t *context = decoder->device;
    const size_t remaining = context->length - context->offset;
    const size_t take = length < remaining ? length : remaining;
    /* A null buffer means seek rather than read, which is how the decoder
     * steps over what a tagger leaves in front of the image - the covers this
     * was built for carry 34 KB of Exif and colour profile there. */
    if (buffer != NULL) memcpy(buffer, context->data + context->offset, take);
    context->offset += take;
    return take;
}

static int decode_output(JDEC *decoder, void *bitmap, JRECT *rect)
{
    album_art_decode_t *context = decoder->device;
    const uint16_t *source = bitmap;
    const size_t row_pixels = (size_t)(rect->right - rect->left) + 1U;

    for (uint16_t y = rect->top; y <= rect->bottom; ++y) {
        if (y >= context->height) break;
        size_t width = row_pixels;
        // Clipped rather than trusted. The decoder's own arithmetic keeps
        // every rectangle inside the descaled image, and a version that got
        // that wrong would write past this buffer rather than draw badly.
        if ((size_t)rect->left + width > context->width) {
            width = context->width > rect->left ? (size_t)(context->width - rect->left) : 0U;
        }
        if (width == 0U) break;
        memcpy(context->pixels + (size_t)y * context->width + rect->left,
               source + (size_t)(y - rect->top) * row_pixels, width * sizeof(uint16_t));
    }
    return 1;
}

/* How far to let the decoder descale. It halves, so this stops at the last
 * halving that still leaves both sides at or above the tile: the averaging
 * pass that follows needs something left to average, and going further would
 * mean enlarging a picture whose detail had just been discarded. */
static uint8_t scale_for(uint16_t width, uint16_t height)
{
    uint8_t scale = 0U;
    while (scale < 3U && (width >> (scale + 1U)) >= ALBUM_ART_SIZE &&
           (height >> (scale + 1U)) >= ALBUM_ART_SIZE) {
        ++scale;
    }
    return scale;
}

/* The other decoder's whole job, in the two cases tjpgd cannot serve.
 *
 * It has no descaling, so the picture arrives at its own size and the reducer
 * does all of the work - which is fine, since it is the same reducer the
 * baseline path ends with and the size limit is enforced before any of it is
 * allocated. */
static bool decode_with_stb_and_publish(const uint8_t *data, size_t length)
{
    uint16_t *pixels = NULL;
    uint16_t width = 0U;
    uint16_t height = 0U;
    if (!image_decode_rgb565(data, length, &pixels, &width, &height)) return false;

    const image_rect_t rect = image_fit_square(width, height, (uint16_t)ALBUM_ART_SIZE);
    const bool published = reduce_into_cover(pixels, width, height, &rect);
    if (published) {
        ESP_LOGI(TAG, "cover %ux%u shown as %ux%u", (unsigned int)width, (unsigned int)height,
                 (unsigned int)rect.width, (unsigned int)rect.height);
    }
    image_decode_free(pixels);
    return published;
}

static bool decode_and_publish(const uint8_t *data, size_t length)
{
    // PNG never had a decoder here, so there is nothing to try first.
    if (image_decode_is_png(data, length)) {
        return decode_with_stb_and_publish(data, length);
    }

    uint8_t *pool = alloc_buffer(ALBUM_ART_WORK_POOL);
    if (pool == NULL) {
        ESP_LOGW(TAG, "no memory for the decoder's work pool");
        return false;
    }

    album_art_decode_t context = {
        .data = data,
        .length = length,
        .offset = 0U,
        .pixels = NULL,
        .width = 0U,
        .height = 0U,
    };
    JDEC decoder;
    bool published = false;

    JRESULT result = jd_prepare(&decoder, decode_input, pool, ALBUM_ART_WORK_POOL, &context);
    if (result != JDR_OK) {
        /* Progressive JPEG arrives here - this decoder is baseline only - and
         * used to end the attempt, which left whole albums showing the
         * placeholder over a cover their files really carried. The second
         * decoder is asked only now and only for that one reason: it needs
         * megabytes where this one needs four kilobytes. */
        ESP_LOGI(TAG, "cover cannot be decoded: tjpgd result=%d", (int)result);
        if (image_decode_is_progressive_jpeg(data, length)) {
            heap_caps_free(pool);
            return decode_with_stb_and_publish(data, length);
        }
    } else {
        const uint8_t scale = scale_for(decoder.width, decoder.height);
        context.width = (uint16_t)(decoder.width >> scale);
        context.height = (uint16_t)(decoder.height >> scale);
        const uint32_t pixels = (uint32_t)context.width * context.height;

        if (pixels == 0U || pixels > ALBUM_ART_DECODED_PIXELS_MAX) {
            ESP_LOGW(TAG, "cover %ux%u is outside what is worth decoding",
                     (unsigned int)decoder.width, (unsigned int)decoder.height);
        } else if ((context.pixels = (uint16_t *)alloc_buffer(pixels * sizeof(uint16_t))) ==
                   NULL) {
            ESP_LOGW(TAG, "no memory for a %ux%u cover", (unsigned int)context.width,
                     (unsigned int)context.height);
        } else {
            result = jd_decomp(&decoder, decode_output, scale);
            if (result != JDR_OK) {
                ESP_LOGW(TAG, "cover failed to decode: tjpgd result=%d", (int)result);
            } else {
                const image_rect_t rect =
                    image_fit_square(context.width, context.height, (uint16_t)ALBUM_ART_SIZE);
                if (reduce_into_cover(context.pixels, context.width, context.height, &rect)) {
                    published = true;
                    ESP_LOGI(TAG, "cover %ux%u decoded at 1/%u and shown as %ux%u",
                             (unsigned int)decoder.width, (unsigned int)decoder.height,
                             1U << scale, (unsigned int)rect.width, (unsigned int)rect.height);
                }
            }
            heap_caps_free(context.pixels);
        }
    }

    heap_caps_free(pool);
    return published;
}

bool album_art_set_image(const uint8_t *data, size_t length)
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
