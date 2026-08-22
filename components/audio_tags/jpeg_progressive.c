#include "jpeg_progressive.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
static const char *TAG = "jpeg_prog";
#define JPEG_PROGRESSIVE_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define JPEG_PROGRESSIVE_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#else
#define JPEG_PROGRESSIVE_LOGI(...) ((void)0)
#define JPEG_PROGRESSIVE_LOGW(...) ((void)0)
#endif

/* Every allocation stb makes goes through here, which is the point of wiring
 * its allocator at all: the internal heap has tens of kilobytes free on a
 * running device and this needs megabytes, so a decode that fell back on plain
 * malloc() would fail on the device while passing everywhere else.
 *
 * Inline because stb insists on being given a complete set of allocators while
 * the JPEG path never calls realloc, and an unused static function is an error
 * under this project's flags. */
static inline void *jpeg_progressive_malloc(size_t size)
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

static inline void jpeg_progressive_release(void *block)
{
#ifdef ESP_PLATFORM
    heap_caps_free(block);
#else
    free(block);
#endif
}

static inline void *jpeg_progressive_realloc(void *block, size_t size)
{
#ifdef ESP_PLATFORM
    return heap_caps_realloc(block, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return realloc(block, size);
#endif
}

#define STBI_ONLY_JPEG
// No file access and no floating-point paths: the picture is already in the
// tag scratch buffer, and this device has neither a filesystem stb should know
// about nor a use for HDR.
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MALLOC jpeg_progressive_malloc
#define STBI_FREE jpeg_progressive_release
#define STBI_REALLOC jpeg_progressive_realloc
#define STB_IMAGE_IMPLEMENTATION
/* Vendored unmodified, so the one warning it trips under this project's flags
 * is silenced here rather than by editing it - see stb/LICENSE.txt. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "stb_image.h"
#pragma GCC diagnostic pop

bool jpeg_is_progressive(const uint8_t *data, size_t length)
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

bool jpeg_progressive_decode(const uint8_t *data, size_t length, uint16_t **pixels,
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
        JPEG_PROGRESSIVE_LOGI("cover header unreadable: %s", stbi_failure_reason());
        return false;
    }
    if (source_width <= 0 || source_height <= 0) return false;
    if ((size_t)source_width * (size_t)source_height > JPEG_PROGRESSIVE_PIXELS_MAX) {
        JPEG_PROGRESSIVE_LOGW("cover %dx%d is larger than this decoder's budget",
                              source_width, source_height);
        return false;
    }

    // Three channels whatever the file has, so the conversion below has one
    // shape: stb expands greyscale for us.
    uint8_t *rgb = stbi_load_from_memory(data, (int)length, &source_width, &source_height,
                                         &components, 3);
    if (rgb == NULL) {
        JPEG_PROGRESSIVE_LOGW("cover failed to decode: %s", stbi_failure_reason());
        return false;
    }

    const size_t count = (size_t)source_width * (size_t)source_height;
    uint16_t *converted = jpeg_progressive_malloc(count * sizeof(uint16_t));
    if (converted == NULL) {
        JPEG_PROGRESSIVE_LOGW("no memory for a %dx%d cover", source_width, source_height);
        stbi_image_free(rgb);
        return false;
    }
    /* To RGB565 in the byte order tjpgd emits (JD_FORMAT 1), which is what the
     * scaler and LVGL both already expect - the two decoders have to be
     * interchangeable from here on. */
    for (size_t index = 0U; index < count; ++index) {
        const uint8_t red = rgb[index * 3U];
        const uint8_t green = rgb[index * 3U + 1U];
        const uint8_t blue = rgb[index * 3U + 2U];
        converted[index] = (uint16_t)(((uint16_t)(red & 0xF8U) << 8) |
                                      ((uint16_t)(green & 0xFCU) << 3) | (blue >> 3));
    }
    stbi_image_free(rgb);

    *pixels = converted;
    if (width != NULL) *width = (uint16_t)source_width;
    if (height != NULL) *height = (uint16_t)source_height;
    JPEG_PROGRESSIVE_LOGI("progressive cover %dx%d decoded", source_width, source_height);
    return true;
}

void jpeg_progressive_free(uint16_t *pixels)
{
    jpeg_progressive_release(pixels);
}
