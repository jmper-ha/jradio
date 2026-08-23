#include "image_decode.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
static const char *TAG = "image_decode";
#define IMAGE_DECODE_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define IMAGE_DECODE_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#else
#define IMAGE_DECODE_LOGI(...) ((void)0)
#define IMAGE_DECODE_LOGW(...) ((void)0)
#endif

/* Every allocation stb makes goes through here, which is the point of wiring
 * its allocator at all: the internal heap has tens of kilobytes free on a
 * running device and this needs megabytes, so a decode that fell back on plain
 * malloc() would fail on the device while passing everywhere else.
 *
 * Inline because stb insists on being given a complete set of allocators while
 * the JPEG path never calls realloc, and an unused static function is an error
 * under this project's flags. */
static inline void *image_decode_malloc(size_t size)
{
#ifdef ESP_PLATFORM
    void *block = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (block == NULL) {
        block = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return block;
#else
    return malloc(size);
#endif
}

static inline void image_decode_release(void *block)
{
#ifdef ESP_PLATFORM
    heap_caps_free(block);
#else
    free(block);
#endif
}

static inline void *image_decode_realloc(void *block, size_t size)
{
#ifdef ESP_PLATFORM
    return heap_caps_realloc(block, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return realloc(block, size);
#endif
}

#define STBI_ONLY_JPEG
/* PNG is here for folder covers - "Cover.png" beside the track - which is the
 * one picture the device reads from a file of its own rather than out of a
 * tag. It costs stb's inflate, which nothing else in this build needs. */
#define STBI_ONLY_PNG
// No file access and no floating-point paths: the picture is already in the
// tag scratch buffer, and this device has neither a filesystem stb should know
// about nor a use for HDR.
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MALLOC image_decode_malloc
#define STBI_FREE image_decode_release
#define STBI_REALLOC image_decode_realloc
#define STB_IMAGE_IMPLEMENTATION
/* Vendored unmodified, so the one warning it trips under this project's flags
 * is silenced here rather than by editing it - see stb/LICENSE.txt. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "stb_image.h"
#pragma GCC diagnostic pop

bool image_decode_is_progressive_jpeg(const uint8_t *data, size_t length)
{
    if (data == NULL || length < 4U || data[0] != 0xFFU || data[1] != 0xD8U) return false;

    size_t offset = 2U;
    while (offset + 1U < length) {
        // Markers may be preceded by any number of 0xFF fill bytes.
        if (data[offset] != 0xFFU) return false;
        while (offset + 1U < length && data[offset + 1U] == 0xFFU) ++offset;
        if (offset + 1U >= length) return false;
        const uint8_t marker = data[offset + 1U];
        offset += 2U;

        // Standalone markers carry no payload; everything else states its own
        // length, which is what lets this walk skip whole segments.
        if (marker == 0x01U || (marker >= 0xD0U && marker <= 0xD9U)) continue;
        // Start of scan: the frame header is always in front of it, so a file
        // that reaches here without one is not going to say what it is.
        if (marker == 0xDAU) return false;
        if (offset + 1U >= length) return false;
        const size_t segment = ((size_t)data[offset] << 8) | data[offset + 1U];
        if (segment < 2U || offset + segment > length) return false;

        /* The three progressive start-of-frame markers: Huffman, extended
         * Huffman and arithmetic. Their sequential counterparts (C0, C1, C9)
         * are what tjpgd handles, and the rest of the Cx range is either a
         * table (C4, CC) or a lossless mode nothing writes. */
        if (marker == 0xC2U || marker == 0xC6U || marker == 0xCAU) return true;
        if (marker == 0xC0U || marker == 0xC1U || marker == 0xC3U || marker == 0xC5U ||
            marker == 0xC7U || marker == 0xC9U || marker == 0xCBU || marker == 0xCDU ||
            marker == 0xCEU || marker == 0xCFU) {
            return false;
        }
        offset += segment;
    }
    return false;
}

size_t image_decode_working_bytes(size_t pixels, bool png)
{
    /* Measured on the host with an instrumented allocator, on the pictures
     * this was built for: a 250x250 progressive JPEG peaked at 12.75 bytes a
     * pixel, a 1076x978 PNG at 6.01. JPEG is the expensive one because a
     * progressive scan is held as coefficients and as samples at the same
     * time; PNG only ever holds the inflated rows and the output.
     *
     * Rounded to whole bytes and still an estimate - a PNG with an alpha
     * channel holds four bytes a pixel where this assumes three, and the file
     * itself is in memory as well. The reserve below is what absorbs that; a
     * decode that goes over anyway fails on a null from the allocator, which
     * costs a cover rather than anything worse. */
    return pixels * (png ? 6U : 13U);
}

/* Whether there is room to try at all.
 *
 * The device answer is the honest one: free PSRAM changes with what is
 * playing, and a fixed pixel limit would be either too mean for a normal
 * folder cover or too generous on a busy device. The reserve is what is left
 * alone - a decode that took the last of PSRAM would be paid for by whatever
 * asked next.
 *
 * Half a megabyte, not the whole one it started as. A 1076x978 PNG - the size
 * a CD rip actually produces - needs 6.3 MB to decode, and against 8 MB of
 * PSRAM the old reserve left it passing only while nothing else was using
 * any: the moment LVGL's heap moved into PSRAM and a FLAC station took its
 * buffers there too, that cover started coming back as "not enough free
 * memory" and the tile kept its placeholder. What the reserve has to protect
 * is the next allocation after this one, and the largest of those is a decoder
 * buffer of a few hundred kilobytes. */
#define IMAGE_DECODE_RESERVE_BYTES (512U * 1024U)

static bool enough_memory_for(size_t pixels, bool png)
{
#ifdef ESP_PLATFORM
    const size_t needed = image_decode_working_bytes(pixels, png) + IMAGE_DECODE_RESERVE_BYTES;
    const size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (free_bytes < needed) {
        // Both numbers, because "not enough memory" alone says nothing about
        // whether the picture is too big or the device is too busy.
        IMAGE_DECODE_LOGW("cover needs %u bytes of PSRAM, %u free",
                          (unsigned int)needed, (unsigned int)free_bytes);
        return false;
    }
    return true;
#else
    // The host has an ordinary heap and no reason to guess at it.
    (void)pixels;
    (void)png;
    return true;
#endif
}

bool image_decode_is_png(const uint8_t *data, size_t length)
{
    static const uint8_t signature[8] = {0x89U, 0x50U, 0x4EU, 0x47U,
                                         0x0DU, 0x0AU, 0x1AU, 0x0AU};
    return data != NULL && length >= sizeof(signature) &&
           memcmp(data, signature, sizeof(signature)) == 0;
}

static bool decode_into_rgb565(const uint8_t *data, size_t length, uint16_t **pixels,
                               uint16_t *width, uint16_t *height)
{
    if (pixels == NULL) return false;
    *pixels = NULL;
    // stb counts bytes in an int, and nothing that arrives here is anywhere
    // near that, but the cast has to be safe rather than merely unlikely.
    if (data == NULL || length == 0U || length > (size_t)INT32_MAX) return false;

    int source_width = 0;
    int source_height = 0;
    int components = 0;
    if (!stbi_info_from_memory(data, (int)length, &source_width, &source_height,
                               &components)) {
        IMAGE_DECODE_LOGI("cover header unreadable: %s", stbi_failure_reason());
        return false;
    }
    if (source_width <= 0 || source_height <= 0) return false;
    const size_t pixels_needed = (size_t)source_width * (size_t)source_height;
    if (pixels_needed > IMAGE_DECODE_PIXELS_MAX) {
        IMAGE_DECODE_LOGW("cover %dx%d is larger than this decoder will attempt",
                          source_width, source_height);
        return false;
    }
    if (!enough_memory_for(pixels_needed, image_decode_is_png(data, length))) {
        IMAGE_DECODE_LOGW("not enough free memory to decode a %dx%d cover", source_width,
                          source_height);
        return false;
    }

    // Three channels whatever the file has, so the conversion below has one
    // shape: stb expands greyscale for us.
    uint8_t *rgb = stbi_load_from_memory(data, (int)length, &source_width, &source_height,
                                         &components, 3);
    if (rgb == NULL) {
        IMAGE_DECODE_LOGW("cover failed to decode: %s", stbi_failure_reason());
        return false;
    }

    /* To RGB565 in the byte order tjpgd emits (JD_FORMAT 1), which is what the
     * scaler and LVGL both already expect - the two decoders have to be
     * interchangeable from here on.
     *
     * Written back over the picture it is reading rather than into a buffer of
     * its own: two bytes are written where three were read, so the writer
     * never catches up with the reader, and a full-size folder cover is two
     * megabytes that would otherwise have to be free at the same time as
     * everything stb is still holding. */
    const size_t count = (size_t)source_width * (size_t)source_height;
    uint16_t *converted = (uint16_t *)(void *)rgb;
    for (size_t index = 0U; index < count; ++index) {
        const uint8_t red = rgb[index * 3U];
        const uint8_t green = rgb[index * 3U + 1U];
        const uint8_t blue = rgb[index * 3U + 2U];
        converted[index] = (uint16_t)(((uint16_t)(red & 0xF8U) << 8) |
                                      ((uint16_t)(green & 0xFCU) << 3) | (blue >> 3));
    }

    *pixels = converted;
    if (width != NULL) *width = (uint16_t)source_width;
    if (height != NULL) *height = (uint16_t)source_height;
    IMAGE_DECODE_LOGI("cover %dx%d decoded", source_width, source_height);
    return true;
}

#ifdef ESP_PLATFORM

/* Why the decode gets a task of its own, and why that task's stack is in
 * PSRAM.
 *
 * stb's inflate holds both Huffman tables as locals - some four kilobytes -
 * and the PNG reader keeps a 1 KB palette beside them, so reading a picture
 * needs around six kilobytes of stack more than any caller here has to spare.
 * The player task found that out the hard way: the first folder cover decoded,
 * published, and then overflowed usb_play on the way back out.
 *
 * Growing the callers is not on offer. Task stacks come out of internal RAM,
 * and this device runs with some 18 KB of it free and no block larger than
 * eight; PSRAM is the only place a stack this size can live. That is what
 * CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM permits, and the restriction it
 * carries - the stack must never be touched while the flash cache is off -
 * holds here: this task is arithmetic over PSRAM and nothing else, it runs no
 * interrupt handler, and a flash operation stalls it before the cache goes.
 */
#define IMAGE_DECODE_TASK_STACK 16384

typedef struct {
    const uint8_t *data;
    size_t length;
    uint16_t **pixels;
    uint16_t *width;
    uint16_t *height;
    bool decoded;
    SemaphoreHandle_t finished;
} image_decode_job_t;

static void image_decode_task(void *argument)
{
    image_decode_job_t *job = argument;
    job->decoded = decode_into_rgb565(job->data, job->length, job->pixels, job->width,
                                      job->height);
    xSemaphoreGive(job->finished);
    /* Parked rather than self-deleted: deleting itself would have FreeRTOS
     * spawn a cleanup task, out of the internal RAM this arrangement exists to
     * avoid. The caller is already awake and does it instead. */
    vTaskSuspend(NULL);
}

bool image_decode_rgb565(const uint8_t *data, size_t length, uint16_t **pixels,
                         uint16_t *width, uint16_t *height)
{
    if (pixels == NULL) return false;
    *pixels = NULL;

    image_decode_job_t job = {
        .data = data,
        .length = length,
        .pixels = pixels,
        .width = width,
        .height = height,
        .decoded = false,
        .finished = xSemaphoreCreateBinary(),
    };
    if (job.finished == NULL) return false;

    TaskHandle_t worker = NULL;
    // At the caller's priority: it is the caller's work, done on a bigger
    // stack, and the caller is blocked on it throughout.
    const BaseType_t created = xTaskCreatePinnedToCoreWithCaps(
        image_decode_task, "cover", IMAGE_DECODE_TASK_STACK, &job, uxTaskPriorityGet(NULL),
        &worker, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
    if (created != pdPASS) {
        IMAGE_DECODE_LOGW("no room for the decoder task");
        vSemaphoreDelete(job.finished);
        return false;
    }

    xSemaphoreTake(job.finished, portMAX_DELAY);
    vTaskDeleteWithCaps(worker);
    vSemaphoreDelete(job.finished);
    return job.decoded;
}

#else

bool image_decode_rgb565(const uint8_t *data, size_t length, uint16_t **pixels,
                         uint16_t *width, uint16_t *height)
{
    // The host has a megabyte of stack and no reason to spend a task on this.
    return decode_into_rgb565(data, length, pixels, width, height);
}

#endif /* ESP_PLATFORM */

void image_decode_free(uint16_t *pixels)
{
    image_decode_release(pixels);
}
