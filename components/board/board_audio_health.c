#include "board_audio_health.h"

#include "board_config.h"

void board_audio_health_reset(board_audio_health_t *health)
{
    if (health == NULL) {
        return;
    }
    health->bytes = 0U;
    health->peak = 0U;
    health->writes = 0U;
    health->short_writes = 0U;
    health->silent_writes = 0U;
    health->max_zero_run = 0U;
}

void board_audio_health_note_zero_run(board_audio_health_t *health, uint32_t frames)
{
    if (health == NULL || frames <= health->max_zero_run) {
        return;
    }
    health->max_zero_run = frames;
}

void board_audio_health_add(board_audio_health_t *health, size_t written,
                            size_t requested, uint16_t peak)
{
    if (health == NULL) {
        return;
    }
    health->bytes += written;
    ++health->writes;
    if (written < requested) {
        ++health->short_writes;
    }
    if (peak > health->peak) {
        health->peak = peak;
    }
    /* Exactly zero, not a threshold: real music practically never produces an
     * all-zero block, so this stays unambiguous evidence that the pipeline
     * handed the DAC nothing rather than something quiet. */
    if (peak == 0U && written > 0U) {
        ++health->silent_writes;
    }
}

unsigned int board_audio_health_realtime_percent(size_t bytes, uint32_t sample_rate,
                                                 uint32_t elapsed_ms)
{
    if (sample_rate == 0U || elapsed_ms == 0U) {
        return 0U;
    }
    /* 64-bit throughout: at 48 kHz a 10 s window is already 1.9 MB, and the
     * x100 for the percentage would overflow 32 bits on a longer one. */
    const uint64_t expected = ((uint64_t)sample_rate * (uint64_t)BOARD_AUDIO_CHANNEL_COUNT *
                               (uint64_t)(BOARD_AUDIO_BITS_PER_SAMPLE / 8U) *
                               (uint64_t)elapsed_ms) / 1000U;
    if (expected == 0U) {
        return 0U;
    }
    return (unsigned int)(((uint64_t)bytes * 100U) / expected);
}
