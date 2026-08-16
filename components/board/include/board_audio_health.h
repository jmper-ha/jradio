#pragma once

#include <stddef.h>
#include <stdint.h>

/* Accounting for one I2S output reporting window.
 *
 * Audible silence that is not a crash leaves no other trace in this firmware:
 * the player task keeps running, the writes keep returning ESP_OK, and the
 * status API reports "playing" throughout. `peak` is the field that makes such
 * a fault diagnosable at all - it separates "we are still handing the DAC
 * digital silence" (a decoder or source fault) from "we are handing the DAC
 * real audio and nobody hears it" (a clock, wiring or DAC fault). */
typedef struct {
    size_t bytes;
    uint16_t peak;
    unsigned int writes;
    unsigned int short_writes;
    /* Blocks that were entirely digital silence. `peak` alone cannot see these
     * because it is a maximum over the whole window: a second of silence inside
     * ten seconds of music leaves the window peak untouched, while the listener
     * hears the fault plainly. This counter is what makes that case visible. */
    unsigned int silent_writes;
    /* Longest run of consecutive all-zero frames in the window, measured
     * across block boundaries. The PCM5102 mutes its analog output after 1024
     * of them, so this is the one number that says whether the firmware itself
     * triggered the DAC's mute. */
    uint32_t max_zero_run;
} board_audio_health_t;

/* The DAC mutes itself after this many consecutive zero frames. */
#define AUDIO_ZERO_DETECT_FRAMES 1024U

void board_audio_health_reset(board_audio_health_t *health);

void board_audio_health_note_zero_run(board_audio_health_t *health, uint32_t frames);

/* `requested` is what the caller offered, `written` what I2S accepted; the two
 * differ when a write times out with the DMA queue still full. */
void board_audio_health_add(board_audio_health_t *health, size_t written,
                            size_t requested, uint16_t peak);

/* Bytes actually clocked out, as a percentage of what `elapsed_ms` of audio
 * needs at `sample_rate`. 100% is a pipeline keeping up; a collapse means the
 * writes stopped, which no other counter shows directly. */
unsigned int board_audio_health_realtime_percent(size_t bytes, uint32_t sample_rate,
                                                 uint32_t elapsed_ms);
