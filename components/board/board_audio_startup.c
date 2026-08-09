#include "board_audio_startup.h"

#include <stdint.h>

size_t board_audio_startup_silence_bytes(size_t dma_descriptor_count,
                                         size_t dma_descriptor_bytes)
{
    if (dma_descriptor_count == 0U || dma_descriptor_bytes == 0U ||
        dma_descriptor_count > SIZE_MAX / dma_descriptor_bytes) {
        return 0U;
    }
    return dma_descriptor_count * dma_descriptor_bytes;
}
