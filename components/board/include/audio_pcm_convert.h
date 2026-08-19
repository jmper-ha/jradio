#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* radio_decoder.cpp is C++ and is one of the two callers. */
#ifdef __cplusplus
extern "C" {
#endif

/* Puts decoded PCM into the one layout the output stage accepts.
 *
 * The I2S channel is configured once, at 16-bit stereo slots, and is never
 * reconfigured per track - see board_audio_format.h. So every decoder has to
 * hand over interleaved stereo, and a mono source has to be duplicated into
 * both channels before it gets there.
 *
 * Getting that wrong is not a subtle fault. A mono block handed over as-is is
 * read as alternating left and right samples, so half as many frames come out
 * of the same bytes: the track plays at double speed *and* drains the output
 * twice as fast as the network fills it. Measured on a mono MP3 station, that
 * was 869 I2S underruns in a ten-second window and playback at 0% of realtime.
 *
 * Only a source that decodes to mono needs this. A stream whose bitstream
 * header says one channel does not necessarily decode to one channel -
 * HE-AAC v2 declares mono in ADTS and emits stereo through parametric stereo,
 * and esp_audio_simple_dec was measured doing exactly that. Drive this from
 * what the decoder reports about its own output, never from the container. */

/* Writes `source_bytes` of interleaved 16-bit PCM out as stereo.
 *
 * `channels` is what the decoder produced: 1 is duplicated into both channels,
 * 2 is copied through unchanged. `written` receives the byte count produced,
 * which is twice `source_bytes` for mono.
 *
 * False when the arguments cannot describe whole 16-bit frames or the
 * destination is too small, and nothing is written in that case - a partial
 * block would be a burst of noise plus a permanent channel swap for everything
 * after it. Source and destination must not overlap: mono grows on the way
 * out, so writing in place would overwrite samples not yet read. */
bool audio_pcm_to_stereo_s16(const uint8_t *source, size_t source_bytes, uint8_t channels,
                             uint8_t *destination, size_t destination_capacity,
                             size_t *written);

/* Bytes `audio_pcm_to_stereo_s16` would produce, for sizing a buffer or
 * checking one before decoding into it. Zero when the arguments are not a
 * whole number of frames, which is the same input this refuses to convert. */
size_t audio_pcm_stereo_bytes(size_t source_bytes, uint8_t channels);

/* Doubles mono 16-bit PCM into stereo within one buffer.
 *
 * For the decoder that writes its output straight into the caller's PCM
 * buffer: there is no second buffer to convert out of, and allocating one
 * per block to avoid the overlap would cost more than the conversion. Safe
 * because it fills from the end backwards - the sample being read always sits
 * at or below the pair being written, so nothing is overwritten before it has
 * been used.
 *
 * `mono_bytes` is what the buffer currently holds and `capacity` is how big
 * the buffer is; twice `mono_bytes` has to fit. False leaves the buffer
 * untouched. */
bool audio_pcm_mono_to_stereo_inplace_s16(uint8_t *buffer, size_t mono_bytes,
                                          size_t capacity, size_t *written);

#ifdef __cplusplus
}
#endif
