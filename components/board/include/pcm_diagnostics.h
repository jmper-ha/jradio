#pragma once

#include <stddef.h>
#include <stdint.h>

uint16_t pcm_s16le_peak(const uint8_t *pcm, size_t length);
size_t pcm_s16le_stereo_square_fill(uint8_t *pcm, size_t length, uint32_t sample_rate,
                                    uint32_t frequency, int16_t amplitude, uint32_t *phase);
