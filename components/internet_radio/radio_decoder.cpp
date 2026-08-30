#include "radio_decoder.h"

#include <algorithm>
#include <cstring>
#include <new>

#include "audio_pcm_convert.h"
#include "esp_heap_caps.h"
#include "decoder/impl/esp_aac_dec.h"
#include "decoder/impl/esp_flac_dec.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include "simple_dec/impl/esp_ogg_dec.h"
#include "micro_flac/flac_decoder.h"
#include "micro_mp3/mp3_decoder.h"

struct radio_decoder {
    explicit radio_decoder(radio_stream_format_t stream_format) : format(stream_format) {}

    radio_stream_format_t format;
    esp_audio_simple_dec_handle_t simple = nullptr;
    bool simple_info_ready = false;
    micro_mp3::Mp3Decoder mp3;
    micro_flac::FLACDecoder flac;
    int16_t *mp3_pcm = nullptr;
    int32_t *flac_pcm = nullptr;
    size_t flac_pcm_samples = 0;
    uint8_t *simple_overflow_pcm = nullptr;
    size_t simple_overflow_capacity = 0;
    radio_decoder_info_t info{};
};

static void free_mp3_buffer(radio_decoder_t *decoder)
{
    if (decoder->mp3_pcm != nullptr) {
        heap_caps_free(decoder->mp3_pcm);
        decoder->mp3_pcm = nullptr;
    }
}

static bool allocate_mp3_buffer(radio_decoder_t *decoder)
{
    decoder->mp3_pcm = static_cast<int16_t *>(heap_caps_malloc(
        micro_mp3::MP3_MIN_OUTPUT_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (decoder->mp3_pcm == nullptr) {
        decoder->mp3_pcm = static_cast<int16_t *>(heap_caps_malloc(
            micro_mp3::MP3_MIN_OUTPUT_BUFFER_BYTES,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    return decoder->mp3_pcm != nullptr;
}

static void free_simple_overflow_buffer(radio_decoder_t *decoder)
{
    if (decoder->simple_overflow_pcm != nullptr) {
        heap_caps_free(decoder->simple_overflow_pcm);
        decoder->simple_overflow_pcm = nullptr;
        decoder->simple_overflow_capacity = 0U;
    }
}

// esp_audio_simple_dec_process() signals ESP_AUDIO_ERR_BUFF_NOT_ENOUGH when the
// caller's output buffer is too small for a decoded frame; the documented
// recovery is to reallocate a bigger buffer and retry the same call. The
// caller's PCM buffer (radio->pcm) has a fixed size, so a dedicated overflow
// buffer sized to `needed` is grown on demand instead.
static bool ensure_simple_overflow_buffer(radio_decoder_t *decoder, size_t needed)
{
    if (decoder->simple_overflow_capacity >= needed) {
        return true;
    }
    free_simple_overflow_buffer(decoder);
    uint8_t *buffer = static_cast<uint8_t *>(
        heap_caps_malloc(needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t *>(
            heap_caps_malloc(needed, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (buffer == nullptr) {
        return false;
    }
    decoder->simple_overflow_pcm = buffer;
    decoder->simple_overflow_capacity = needed;
    return true;
}

static void free_flac_buffer(radio_decoder_t *decoder)
{
    if (decoder->flac_pcm != nullptr) {
        heap_caps_free(decoder->flac_pcm);
        decoder->flac_pcm = nullptr;
        decoder->flac_pcm_samples = 0;
    }
}

static bool allocate_flac_buffer(radio_decoder_t *decoder)
{
    const size_t samples = decoder->flac.get_output_buffer_size_samples();
    if (samples == 0U || samples > (128U * 1024U)) {
        return false;
    }
    decoder->flac_pcm = static_cast<int32_t *>(heap_caps_malloc(
        samples * sizeof(int32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (decoder->flac_pcm == nullptr) {
        decoder->flac_pcm = static_cast<int32_t *>(heap_caps_malloc(
            samples * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (decoder->flac_pcm == nullptr) {
        return false;
    }
    decoder->flac_pcm_samples = samples;
    return true;
}

static void update_flac_info(radio_decoder_t *decoder)
{
    const micro_flac::FLACStreamInfo &stream_info = decoder->flac.get_stream_info();
    decoder->info.sample_rate = stream_info.sample_rate();
    decoder->info.channels = static_cast<uint8_t>(stream_info.num_channels());
    decoder->info.bits_per_sample = static_cast<uint8_t>(stream_info.bits_per_sample());
    decoder->info.bitrate_kbps = 0U;
    // Whatever the file's channel count, the output is stereo 16-bit.
    decoder->info.pcm_frame_bytes =
        static_cast<uint32_t>(stream_info.max_block_size()) * 2U * sizeof(int16_t);
}

static void update_mp3_info(radio_decoder_t *decoder)
{
    decoder->info.sample_rate = decoder->mp3.get_sample_rate();
    decoder->info.channels = decoder->mp3.get_channels();
    decoder->info.bits_per_sample = 16U;
    decoder->info.bitrate_kbps = decoder->mp3.get_bitrate();
    // An MPEG frame is 1152 samples at most, well inside any caller's buffer.
    decoder->info.pcm_frame_bytes = 0U;
}

static bool normalize_pcm(const int32_t *input, size_t sample_count, uint8_t channels,
                          uint8_t bits_per_sample, uint8_t *output, size_t capacity,
                          size_t *written)
{
    if (input == nullptr || output == nullptr || written == nullptr ||
        (channels != 1U && channels != 2U) || bits_per_sample == 0U || bits_per_sample > 32U) {
        return false;
    }
    const size_t output_samples = sample_count / channels * 2U;
    if (output_samples * sizeof(int16_t) > capacity) {
        return false;
    }
    auto *destination = reinterpret_cast<int16_t *>(output);
    const size_t frames = sample_count / channels;
    for (size_t frame = 0; frame < frames; ++frame) {
        const int32_t left = input[frame * channels];
        const int32_t right = channels == 2U ? input[frame * channels + 1U] : left;
        destination[frame * 2U] = static_cast<int16_t>(left >> 16);
        destination[frame * 2U + 1U] = static_cast<int16_t>(right >> 16);
    }
    *written = output_samples * sizeof(int16_t);
    return true;
}

extern "C" bool radio_decoder_is_supported(radio_stream_format_t format)
{
    return format == RADIO_STREAM_FORMAT_MP3 || format == RADIO_STREAM_FORMAT_AAC ||
           format == RADIO_STREAM_FORMAT_FLAC || format == RADIO_STREAM_FORMAT_OGG_FLAC;
}

extern "C" radio_decoder_t *radio_decoder_create(radio_stream_format_t format)
{
    if (!radio_decoder_is_supported(format)) {
        return nullptr;
    }
    auto *decoder = new (std::nothrow) radio_decoder(format);
    if (decoder == nullptr) {
        return nullptr;
    }
    if (format == RADIO_STREAM_FORMAT_AAC) {
        // The simple decoder is only a parser/adapter. Its underlying AAC
        // implementation must be registered explicitly before opening it.
        // Keep the registration for the process lifetime; subsequent opens
        // legitimately report that the implementation already exists.
        const esp_audio_err_t register_result = esp_aac_dec_register();
        if (register_result != ESP_AUDIO_ERR_OK &&
            register_result != ESP_AUDIO_ERR_ALREADY_EXIST) {
            delete decoder;
            return nullptr;
        }
        esp_aac_dec_cfg_t aac_cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
        esp_audio_simple_dec_cfg_t simple_cfg = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC,
            .dec_cfg = &aac_cfg,
            .cfg_size = sizeof(aac_cfg),
            .use_frame_dec = false,
        };
        if (esp_audio_simple_dec_open(&simple_cfg, &decoder->simple) != ESP_AUDIO_ERR_OK) {
            delete decoder;
            return nullptr;
        }
    } else if (format == RADIO_STREAM_FORMAT_OGG_FLAC) {
        const esp_audio_err_t flac_register_result = esp_flac_dec_register();
        const esp_audio_err_t ogg_register_result = esp_ogg_dec_register();
        if ((flac_register_result != ESP_AUDIO_ERR_OK &&
             flac_register_result != ESP_AUDIO_ERR_ALREADY_EXIST) ||
            (ogg_register_result != ESP_AUDIO_ERR_OK &&
             ogg_register_result != ESP_AUDIO_ERR_ALREADY_EXIST)) {
            delete decoder;
            return nullptr;
        }
        esp_audio_simple_dec_cfg_t simple_cfg = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_OGG,
            .dec_cfg = nullptr,
            .cfg_size = 0U,
            .use_frame_dec = false,
        };
        if (esp_audio_simple_dec_open(&simple_cfg, &decoder->simple) != ESP_AUDIO_ERR_OK) {
            delete decoder;
            return nullptr;
        }
    } else if (format == RADIO_STREAM_FORMAT_FLAC) {
        decoder->flac.set_crc_check_enabled(true);
    }
    return decoder;
}

extern "C" void radio_decoder_destroy(radio_decoder_t *decoder)
{
    if (decoder == nullptr) {
        return;
    }
    free_mp3_buffer(decoder);
    free_flac_buffer(decoder);
    free_simple_overflow_buffer(decoder);
    if (decoder->simple != nullptr) {
        esp_audio_simple_dec_close(decoder->simple);
        decoder->simple = nullptr;
    }
    delete decoder;
}

extern "C" void radio_decoder_reset(radio_decoder_t *decoder)
{
    if (decoder == nullptr) {
        return;
    }
    free_mp3_buffer(decoder);
    free_flac_buffer(decoder);
    free_simple_overflow_buffer(decoder);
    decoder->mp3.reset();
    decoder->flac.reset();
    if (decoder->simple != nullptr) {
        (void)esp_audio_simple_dec_reset(decoder->simple);
    }
    decoder->simple_info_ready = false;
    decoder->info = {};
}

extern "C" radio_decoder_result_t radio_decoder_decode(
    radio_decoder_t *decoder, const uint8_t *input, size_t input_length, uint8_t *pcm_output,
    size_t pcm_capacity, size_t *bytes_consumed, size_t *pcm_bytes, radio_decoder_info_t *info)
{
    if (decoder == nullptr || input == nullptr || input_length == 0U || bytes_consumed == nullptr ||
        pcm_bytes == nullptr) {
        return RADIO_DECODER_ERROR;
    }
    *bytes_consumed = 0U;
    *pcm_bytes = 0U;

    if (decoder->format == RADIO_STREAM_FORMAT_AAC ||
        decoder->format == RADIO_STREAM_FORMAT_OGG_FLAC) {
        if (decoder->simple == nullptr || pcm_output == nullptr || pcm_capacity == 0U) {
            return RADIO_DECODER_ERROR;
        }
        esp_audio_simple_dec_raw_t raw = {
            .buffer = const_cast<uint8_t *>(input),
            .len = static_cast<uint32_t>(input_length),
            .eos = false,
            .consumed = 0U,
            .frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
        };
        esp_audio_simple_dec_out_t frame = {
            .buffer = pcm_output,
            .len = static_cast<uint32_t>(pcm_capacity),
            .needed_size = 0U,
            .decoded_size = 0U,
        };
        esp_audio_err_t result = esp_audio_simple_dec_process(decoder->simple, &raw, &frame);
        uint8_t *decode_buffer = pcm_output;

        if (result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            // Caller's fixed-size PCM buffer was too small for this frame.
            // Retry once into a dedicated overflow buffer sized to what the
            // decoder reported it actually needs, per the SDK's documented
            // "reallocate and try again" contract.
            constexpr size_t kMaxOverflowPcmBytes = 128U * 1024U;
            const size_t needed = frame.needed_size;
            if (needed > 0U && needed <= kMaxOverflowPcmBytes &&
                ensure_simple_overflow_buffer(decoder, needed)) {
                raw = {
                    .buffer = const_cast<uint8_t *>(input),
                    .len = static_cast<uint32_t>(input_length),
                    .eos = false,
                    .consumed = 0U,
                    .frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
                };
                frame = {
                    .buffer = decoder->simple_overflow_pcm,
                    .len = static_cast<uint32_t>(decoder->simple_overflow_capacity),
                    .needed_size = 0U,
                    .decoded_size = 0U,
                };
                decode_buffer = decoder->simple_overflow_pcm;
                result = esp_audio_simple_dec_process(decoder->simple, &raw, &frame);
            }
        }

        *bytes_consumed = raw.consumed;

        esp_audio_simple_dec_info_t simple_info = {};
        const bool have_info =
            esp_audio_simple_dec_get_info(decoder->simple, &simple_info) == ESP_AUDIO_ERR_OK;
        if (have_info) {
            decoder->info.sample_rate = simple_info.sample_rate;
            decoder->info.channels = simple_info.channel;
            decoder->info.bits_per_sample = simple_info.bits_per_sample;
            decoder->info.bitrate_kbps = simple_info.bitrate / 1000U;
            if (info != nullptr) {
                *info = decoder->info;
            }
        }
        if (result != ESP_AUDIO_ERR_OK) {
            return RADIO_DECODER_ERROR;
        }
        if (frame.decoded_size > 0U) {
            // A FLAC station is free to broadcast at 24 bits, and six of the
            // stations here do; the decoder hands those over as packed
            // three-byte samples. Narrowing happens first, in the decoder's own
            // buffer, because everything below - the fit check, the mono
            // expansion, the output stage - counts in 16-bit samples.
            //
            // The depth is what the decoder reports about its own output, not
            // what the container claimed. Zero means it has not said yet, which
            // only happens before the first frame; 16 is then the safe reading
            // because that is what every other path here produces.
            size_t decoded = frame.decoded_size;
            const uint8_t decoded_bits =
                decoder->info.bits_per_sample == 0U ? 16U : decoder->info.bits_per_sample;
            if (!audio_pcm_narrow_to_s16(decode_buffer, decoded, decoded_bits, &decoded)) {
                return RADIO_DECODER_ERROR;
            }

            // What the caller's buffer has to hold for this stream, which is
            // not the 16 KB it starts with: a 4608-sample block is 18432 bytes
            // even at 16 bits, and every FLAC station using one failed here
            // until the size was reported back so the buffer could grow. The
            // micro-flac path fills this from the stream header; this decoder
            // never states a block size, so it is measured from a frame.
            const size_t output_bytes = decoder->info.channels == 1U ? decoded * 2U : decoded;
            decoder->info.pcm_frame_bytes = static_cast<uint32_t>(output_bytes);
            if (info != nullptr) {
                *info = decoder->info;
            }
            decoder->simple_info_ready = decoder->simple_info_ready || have_info;
            if (output_bytes > pcm_capacity || pcm_output == nullptr) {
                // This one frame is dropped - about 100 ms, once per station -
                // so the caller can grow its buffer and take every frame after
                // it. Decoding on into a buffer that can never be handed back
                // is what used to end the stream with an error.
                return RADIO_DECODER_HEADER_READY;
            }

            // Mono has to be duplicated here for the same reason as on the MP3
            // path: the output stage only takes stereo. Measured, because the
            // reported channel count was not obviously trustworthy: a mono AAC
            // frame decodes to 2048 bytes and a stereo one to 4096, both at
            // 1024 frames, and `channel` matched the size in each case.
            //
            // A mono stream whose server runs ahead of realtime hides this
            // completely - the decoder simply keeps up with the doubled drain,
            // every counter reads healthy, and only the pitch is wrong. That
            // is why the fault has to be reasoned about rather than looked for
            // in the health log.
            if (decoder->info.channels == 1U) {
                const bool expanded =
                    decode_buffer == pcm_output
                        ? audio_pcm_mono_to_stereo_inplace_s16(pcm_output, decoded, pcm_capacity,
                                                               pcm_bytes)
                        : audio_pcm_to_stereo_s16(decode_buffer, decoded, 1U, pcm_output,
                                                  pcm_capacity, pcm_bytes);
                if (!expanded) {
                    return RADIO_DECODER_ERROR;
                }
            } else {
                // Anything else is passed through as it always was, including
                // the channel counts no station here has ever produced: a
                // guess at how to fold them would be worse than the noise.
                if (decode_buffer != pcm_output) {
                    memcpy(pcm_output, decode_buffer, decoded);
                }
                *pcm_bytes = decoded;
            }
            return RADIO_DECODER_PCM_READY;
        }
        if (have_info && !decoder->simple_info_ready) {
            decoder->simple_info_ready = true;
            return RADIO_DECODER_HEADER_READY;
        }
        return RADIO_DECODER_NEED_MORE_DATA;
    }

    size_t consumed = 0U;
    size_t samples = 0U;
    if (decoder->format == RADIO_STREAM_FORMAT_MP3) {
        if (decoder->mp3_pcm == nullptr && !allocate_mp3_buffer(decoder)) {
            return RADIO_DECODER_ERROR;
        }
        size_t mp3_consumed = 0U;
        size_t mp3_samples = 0U;
        const micro_mp3::Mp3Result result = decoder->mp3.decode(
            input, input_length, reinterpret_cast<uint8_t *>(decoder->mp3_pcm),
            micro_mp3::MP3_MIN_OUTPUT_BUFFER_BYTES, mp3_consumed, mp3_samples);
        *bytes_consumed = mp3_consumed;
        if (result == micro_mp3::MP3_STREAM_INFO_READY ||
            result == micro_mp3::MP3_STREAM_INFO_CHANGED) {
            update_mp3_info(decoder);
            if (info != nullptr) {
                *info = decoder->info;
            }
            return RADIO_DECODER_HEADER_READY;
        }
        if (result == micro_mp3::MP3_NEED_MORE_DATA) {
            return RADIO_DECODER_NEED_MORE_DATA;
        }
        if (result == micro_mp3::MP3_DECODE_ERROR) {
            return RADIO_DECODER_NEED_MORE_DATA;
        }
        if (result < 0) {
            return RADIO_DECODER_ERROR;
        }
        if (mp3_samples == 0U) {
            // MP3_OK with zero samples is a normal outcome (e.g. a Xing/LAME
            // header frame carries no audio); not an error.
            return RADIO_DECODER_NEED_MORE_DATA;
        }
        update_mp3_info(decoder);
        const size_t pcm_size = mp3_samples * sizeof(int16_t) * decoder->info.channels;
        // A mono MP3 decodes to mono PCM - there is no parametric stereo in
        // MP3 to hide it - and the output stage only takes stereo, so the
        // duplication has to happen here. Handing mono over untouched played
        // the station at double speed and starved I2S; see audio_pcm_convert.h.
        if (pcm_output == nullptr ||
            !audio_pcm_to_stereo_s16(reinterpret_cast<const uint8_t *>(decoder->mp3_pcm),
                                     pcm_size, decoder->info.channels, pcm_output,
                                     pcm_capacity, pcm_bytes)) {
            return RADIO_DECODER_ERROR;
        }
        if (info != nullptr) {
            *info = decoder->info;
        }
        return RADIO_DECODER_PCM_READY;
    }
    if (decoder->flac_pcm == nullptr) {
        const auto result = decoder->flac.decode(input, input_length, static_cast<int32_t *>(nullptr),
                                                 0U, consumed, samples);
        *bytes_consumed = consumed;
        if (result == micro_flac::FLAC_DECODER_HEADER_READY) {
            update_flac_info(decoder);
            if (!allocate_flac_buffer(decoder)) {
                return RADIO_DECODER_ERROR;
            }
            if (info != nullptr) {
                *info = decoder->info;
            }
            return RADIO_DECODER_HEADER_READY;
        }
        if (result == micro_flac::FLAC_DECODER_NEED_MORE_DATA) {
            return RADIO_DECODER_NEED_MORE_DATA;
        }
        return result == micro_flac::FLAC_DECODER_END_OF_STREAM ? RADIO_DECODER_END_OF_STREAM
                                                                 : RADIO_DECODER_ERROR;
    }

    const auto result = decoder->flac.decode(input, input_length, decoder->flac_pcm,
                                             decoder->flac_pcm_samples, consumed, samples);
    *bytes_consumed = consumed;
    update_flac_info(decoder);
    if (info != nullptr) {
        *info = decoder->info;
    }
    if (result == micro_flac::FLAC_DECODER_NEED_MORE_DATA) {
        return RADIO_DECODER_NEED_MORE_DATA;
    }
    if (result == micro_flac::FLAC_DECODER_END_OF_STREAM) {
        return RADIO_DECODER_END_OF_STREAM;
    }
    if (result != micro_flac::FLAC_DECODER_SUCCESS || samples == 0U ||
        !normalize_pcm(decoder->flac_pcm, samples, decoder->info.channels,
                       decoder->info.bits_per_sample, pcm_output, pcm_capacity, pcm_bytes)) {
        return RADIO_DECODER_ERROR;
    }
    return RADIO_DECODER_PCM_READY;
}
