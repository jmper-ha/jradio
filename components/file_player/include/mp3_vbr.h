#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What a VBR MP3 says about itself in its first frame.
 *
 * Every MP3 frame states its own bitrate, and for a constant-rate file one
 * frame's rate times the file size is the length. A VBR file's frames run
 * from 32 to 320 kbit/s - the bitrate shown jumped with them, and the length
 * and every jump computed from it moved too. The encoder writes the truth into
 * a silent first frame instead: a "Xing" (or LAME's "Info") header with the
 * frame count, the byte count and a 100-point table of where each percent of
 * the track starts, or Fraunhofer's "VBRI" with the counts. */

#define MP3_VBR_TOC_POINTS 100U

typedef struct {
    // Audio frames in the file, not counting the header frame itself.
    uint32_t frames;
    // Bytes of audio, from the header frame on; 0 when the header has none.
    uint32_t bytes;
    uint32_t sample_rate;
    uint16_t samples_per_frame;
    // Where the header frame starts in what was parsed: the audio begins there.
    size_t frame_offset;
    bool has_toc;
    uint8_t toc[MP3_VBR_TOC_POINTS];
} mp3_vbr_t;

/* The length of an ID3v2 tag at the start of `data`, footer included, or 0
 * when there is none. The audio starts after it. */
size_t mp3_id3v2_length(const uint8_t *data, size_t length);

/* Finds the first MPEG audio Layer III frame in `data` - which starts where
 * the audio does, after any ID3v2 tag - and reads a Xing, Info or VBRI header
 * from it. False when there is none or it carries no frame count. */
bool mp3_vbr_parse(const uint8_t *data, size_t length, mp3_vbr_t *vbr);

// The track's length in milliseconds, 0 when it cannot be told.
uint64_t mp3_vbr_duration_ms(const mp3_vbr_t *vbr);

/* Where `target_ms` starts, in bytes from the header frame: by the table of
 * contents when there is one, and in proportion to the byte count when not.
 * 0 when there is nothing to go by. */
uint64_t mp3_vbr_seek_offset(const mp3_vbr_t *vbr, uint64_t target_ms);

#ifdef __cplusplus
}
#endif
