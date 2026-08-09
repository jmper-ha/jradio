#pragma once

#include <stddef.h>

size_t board_audio_startup_silence_bytes(size_t dma_descriptor_count,
                                         size_t dma_descriptor_bytes);
