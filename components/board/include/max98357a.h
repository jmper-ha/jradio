#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "audio_decimate.h"

typedef struct {
    uint32_t bus_hz;
    unsigned up, down;
} max98357a_rate_t;

bool max98357a_rate_plan(uint32_t source_hz, max98357a_rate_t *plan);
/* A fixed output policy, independent of the listener's volume. 0 is unity. */
uint16_t max98357a_ceiling_gain(int dbfs);

typedef struct {
    max98357a_rate_t rate;
    audio_decimate_t decimator;
    int16_t previous[2];
    uint16_t gain;
    size_t pending_at, pending_bytes;
} max98357a_output_t;

void max98357a_output_reset(max98357a_output_t *output, max98357a_rate_t rate,
                            uint16_t gain);
/* Zero for invalid frames or arithmetic overflow. The caller owns the buffer. */
size_t max98357a_output_capacity(size_t input_bytes, unsigned up);
/* Refuses a new block until all pending bytes have been acknowledged. State
 * advances once when input is copied/converted into the caller's owned buffer. */
bool max98357a_output_prepare(max98357a_output_t *output, const uint8_t *input,
                              size_t bytes, uint8_t *buffer, size_t capacity);
bool max98357a_output_advance(max98357a_output_t *output, size_t sent);
