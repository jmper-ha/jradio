#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// WAV needs no decoder - the payload is already PCM - so it never goes through
// radio_decoder. All this has to do is find where the samples start and prove
// they are in a layout the I2S path can take.

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    // Offset of the first sample byte from the start of the file.
    size_t data_offset;
    // Bytes of sample data. Zero means the header declared none, which some
    // recorders write for a stream they never finished; the caller should then
    // read to the end of the file.
    size_t data_length;
} file_wav_info_t;

typedef enum {
    FILE_WAV_OK = 0,
    // The header runs past what was read so far. WAV files can carry large
    // LIST/INFO chunks before "data", so the caller should read more and retry
    // rather than treat this as a broken file.
    FILE_WAV_NEED_MORE_DATA,
    FILE_WAV_INVALID,
    FILE_WAV_UNSUPPORTED,
} file_wav_result_t;

// Parses from the start of the file. `length` is how much of the file is in
// `data` so far, not the file size.
file_wav_result_t file_wav_parse_header(const uint8_t *data, size_t length, file_wav_info_t *info);

#ifdef __cplusplus
}
#endif
