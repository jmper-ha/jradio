#include "lvgl_pool.h"

#include "esp_heap_caps.h"

void *lvgl_pool_alloc(size_t size)
{
    // PSRAM first, internal as the fallback, so a board without PSRAM behaves
    // exactly as it did when the pool was a static array.
    return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
