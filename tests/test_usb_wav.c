#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "usb_wav.h"

static size_t put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8) & 0xFFU);
    out[2] = (uint8_t)((value >> 16) & 0xFFU);
    out[3] = (uint8_t)((value >> 24) & 0xFFU);
    return 4U;
}

static size_t put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8) & 0xFFU);
    return 2U;
}

/* Builds a minimal RIFF/WAVE file: header, one "fmt " chunk and an empty
 * "data" chunk, optionally preceded by a filler chunk to exercise chunk
 * walking. */
static size_t build_wav(uint8_t *out, uint16_t format_tag, uint16_t channels,
                        uint32_t sample_rate, uint16_t bits, const char *filler,
                        uint32_t filler_size)
{
    size_t at = 0U;
    memcpy(out + at, "RIFF", 4U); at += 4U;
    at += put_u32(out + at, 0U);
    memcpy(out + at, "WAVE", 4U); at += 4U;

    if (filler != NULL) {
        memcpy(out + at, filler, 4U); at += 4U;
        at += put_u32(out + at, filler_size);
        memset(out + at, 0xAA, filler_size + (filler_size & 1U));
        at += filler_size + (filler_size & 1U);
    }

    const uint16_t block_align = (uint16_t)(channels * (bits / 8U));
    const uint32_t fmt_size = format_tag == 0xFFFEU ? 40U : 16U;
    memcpy(out + at, "fmt ", 4U); at += 4U;
    at += put_u32(out + at, fmt_size);
    const size_t fmt_body = at;
    at += put_u16(out + at, format_tag);
    at += put_u16(out + at, channels);
    at += put_u32(out + at, sample_rate);
    at += put_u32(out + at, sample_rate * block_align);
    at += put_u16(out + at, block_align);
    at += put_u16(out + at, bits);
    if (format_tag == 0xFFFEU) {
        at += put_u16(out + at, 22U);          /* cbSize */
        at += put_u16(out + at, bits);         /* valid bits */
        at += put_u32(out + at, 3U);           /* channel mask */
        at += put_u16(out + at, 0x0001U);      /* sub-format tag: PCM */
        memset(out + at, 0, 14U); at += 14U;   /* rest of the GUID */
    }
    assert(at - fmt_body == fmt_size);

    memcpy(out + at, "data", 4U); at += 4U;
    at += put_u32(out + at, 8U);
    memset(out + at, 0, 8U); at += 8U;
    return at;
}

static void test_plain_pcm(void)
{
    uint8_t file[256];
    const size_t length = build_wav(file, 0x0001U, 2U, 44100U, 16U, NULL, 0U);
    usb_wav_info_t info;
    assert(usb_wav_parse_header(file, length, &info) == USB_WAV_OK);
    assert(info.sample_rate == 44100U);
    assert(info.channels == 2U);
    assert(info.bits_per_sample == 16U);
    assert(info.data_length == 8U);
    /* The samples begin right after the "data" chunk header. */
    assert(memcmp(file + info.data_offset - 8U, "data", 4U) == 0);
}

static void test_mono_is_accepted(void)
{
    uint8_t file[256];
    const size_t length = build_wav(file, 0x0001U, 1U, 22050U, 16U, NULL, 0U);
    usb_wav_info_t info;
    assert(usb_wav_parse_header(file, length, &info) == USB_WAV_OK);
    assert(info.channels == 1U);
    assert(info.sample_rate == 22050U);
}

static void test_extensible_pcm(void)
{
    uint8_t file[256];
    const size_t length = build_wav(file, 0xFFFEU, 2U, 48000U, 16U, NULL, 0U);
    usb_wav_info_t info;
    assert(usb_wav_parse_header(file, length, &info) == USB_WAV_OK);
    assert(info.sample_rate == 48000U);
}

static void test_skips_chunks_before_data(void)
{
    /* Editors put LIST/INFO metadata between "fmt " and "data"; an odd-sized
     * chunk is padded to a word boundary and the pad byte is not in the size. */
    uint8_t file[512];
    const size_t length = build_wav(file, 0x0001U, 2U, 44100U, 16U, "LIST", 27U);
    usb_wav_info_t info;
    assert(usb_wav_parse_header(file, length, &info) == USB_WAV_OK);
    assert(info.sample_rate == 44100U);
    assert(memcmp(file + info.data_offset - 8U, "data", 4U) == 0);
}

static void test_partial_reads_ask_for_more(void)
{
    uint8_t file[512];
    const size_t length = build_wav(file, 0x0001U, 2U, 44100U, 16U, "LIST", 200U);
    usb_wav_info_t info;
    assert(usb_wav_parse_header(file, 0U, &info) == USB_WAV_NEED_MORE_DATA);
    assert(usb_wav_parse_header(file, 11U, &info) == USB_WAV_NEED_MORE_DATA);
    /* Stops short of "data" because the filler chunk has not been read yet. */
    assert(usb_wav_parse_header(file, 40U, &info) == USB_WAV_NEED_MORE_DATA);
    assert(usb_wav_parse_header(file, length, &info) == USB_WAV_OK);
}

static void test_rejects_unplayable(void)
{
    uint8_t file[256];
    usb_wav_info_t info;

    /* Not a WAV at all. */
    memcpy(file, "ID3\x03\x00\x00\x00\x00\x00\x00\x00\x00", 12U);
    assert(usb_wav_parse_header(file, 12U, &info) == USB_WAV_INVALID);

    /* IMA ADPCM: a container we can read but not decode. */
    size_t length = build_wav(file, 0x0011U, 2U, 44100U, 16U, NULL, 0U);
    assert(usb_wav_parse_header(file, length, &info) == USB_WAV_UNSUPPORTED);

    /* 24-bit would need a conversion pass the I2S path does not have. */
    length = build_wav(file, 0x0001U, 2U, 44100U, 24U, NULL, 0U);
    assert(usb_wav_parse_header(file, length, &info) == USB_WAV_UNSUPPORTED);

    /* More channels than the stereo I2S slots can carry. */
    length = build_wav(file, 0x0001U, 6U, 44100U, 16U, NULL, 0U);
    assert(usb_wav_parse_header(file, length, &info) == USB_WAV_UNSUPPORTED);

    assert(usb_wav_parse_header(NULL, 32U, &info) == USB_WAV_INVALID);
    assert(usb_wav_parse_header(file, length, NULL) == USB_WAV_INVALID);
}

static void test_rejects_data_without_format(void)
{
    uint8_t file[64];
    size_t at = 0U;
    memcpy(file + at, "RIFF", 4U); at += 4U;
    at += put_u32(file + at, 0U);
    memcpy(file + at, "WAVE", 4U); at += 4U;
    memcpy(file + at, "data", 4U); at += 4U;
    at += put_u32(file + at, 4U);
    memset(file + at, 0, 4U); at += 4U;
    usb_wav_info_t info;
    assert(usb_wav_parse_header(file, at, &info) == USB_WAV_INVALID);
}

int main(void)
{
    test_plain_pcm();
    test_mono_is_accepted();
    test_extensible_pcm();
    test_skips_chunks_before_data();
    test_partial_reads_ask_for_more();
    test_rejects_unplayable();
    test_rejects_data_without_format();
    printf("usb_wav tests passed\n");
    return 0;
}
