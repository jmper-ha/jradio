#include "audio_tags.h"

#include <string.h>

void audio_tags_clear(audio_tags_t *tags)
{
    if (tags == NULL) return;
    memset(tags, 0, sizeof(*tags));
}

bool audio_tags_have_text(const audio_tags_t *tags)
{
    if (tags == NULL) return false;
    return tags->title[0] != '\0' || tags->artist[0] != '\0' || tags->album[0] != '\0';
}

/* Windows-1251 puts the Cyrillic alphabet in 0xC0..0xFF and the two letters Ё
 * and ё at 0xA8 and 0xB8; nothing else in the upper half is a letter. */
static bool cp1251_letter_byte(uint8_t value)
{
    return value == 0xA8U || value == 0xB8U || value >= 0xC0U;
}

bool audio_tags_looks_like_cp1251(const uint8_t *input, size_t length)
{
    if (input == NULL) return false;
    size_t high_bytes = 0U;
    for (size_t index = 0U; index < length; ++index) {
        const uint8_t value = input[index];
        if (value < 0x80U) continue;
        // One byte outside the Cyrillic block and this is ordinary Latin-1:
        // a currency sign or a fraction has no business in a Russian title.
        if (!cp1251_letter_byte(value)) return false;
        ++high_bytes;
    }
    /* Four is the smallest count that is not a plausible accident. Latin-1
     * text does reach into 0xC0..0xFF - "Motörhead", "Café" - but one or two
     * accents at a time, whereas even the shortest Russian word clears four. */
    return high_bytes >= 4U;
}

static uint32_t cp1251_to_unicode(uint8_t value)
{
    if (value == 0xA8U) return 0x0401U;
    if (value == 0xB8U) return 0x0451U;
    // 0xC0..0xFF maps straight onto U+0410..U+044F with no gaps.
    return 0x0410U + (uint32_t)(value - 0xC0U);
}

/* Appends one code point, or reports that it did not fit. Refusing a partial
 * sequence is the whole point: the caller stops at the last character that fit
 * rather than leaving a byte of the next one behind. */
static bool append_utf8(char *output, size_t capacity, size_t *used, uint32_t code_point)
{
    uint8_t encoded[4];
    size_t width;
    if (code_point < 0x80U) {
        encoded[0] = (uint8_t)code_point;
        width = 1U;
    } else if (code_point < 0x800U) {
        encoded[0] = (uint8_t)(0xC0U | (code_point >> 6));
        encoded[1] = (uint8_t)(0x80U | (code_point & 0x3FU));
        width = 2U;
    } else if (code_point < 0x10000U) {
        encoded[0] = (uint8_t)(0xE0U | (code_point >> 12));
        encoded[1] = (uint8_t)(0x80U | ((code_point >> 6) & 0x3FU));
        encoded[2] = (uint8_t)(0x80U | (code_point & 0x3FU));
        width = 3U;
    } else {
        encoded[0] = (uint8_t)(0xF0U | (code_point >> 18));
        encoded[1] = (uint8_t)(0x80U | ((code_point >> 12) & 0x3FU));
        encoded[2] = (uint8_t)(0x80U | ((code_point >> 6) & 0x3FU));
        encoded[3] = (uint8_t)(0x80U | (code_point & 0x3FU));
        width = 4U;
    }
    // One byte is always kept back for the terminator.
    if (*used + width + 1U > capacity) return false;
    memcpy(output + *used, encoded, width);
    *used += width;
    return true;
}

/* Line breaks and stray control codes do reach tag fields, and a label draws
 * them as boxes or wraps where it should not. */
static uint32_t sanitise(uint32_t code_point)
{
    if (code_point < 0x20U || code_point == 0x7FU) return ' ';
    return code_point;
}

static size_t trim(char *output, size_t used)
{
    while (used > 0U && (output[used - 1U] == ' ' || output[used - 1U] == '\t')) --used;
    output[used] = '\0';
    return used;
}

static size_t decode_single_byte(const uint8_t *input, size_t length, char *output,
                                 size_t capacity)
{
    const bool cp1251 = audio_tags_looks_like_cp1251(input, length);
    size_t used = 0U;
    for (size_t index = 0U; index < length; ++index) {
        const uint8_t value = input[index];
        // A field can be shorter than the frame that holds it; the rest is
        // padding, not text.
        if (value == 0U) break;
        const uint32_t code_point =
            value < 0x80U ? value : (cp1251 ? cp1251_to_unicode(value) : value);
        if (!append_utf8(output, capacity, &used, sanitise(code_point))) break;
    }
    return trim(output, used);
}

static size_t decode_utf16(const uint8_t *input, size_t length, bool little_endian,
                           char *output, size_t capacity)
{
    size_t used = 0U;
    size_t index = 0U;
    // An odd trailing byte is truncation in the file; the pair before it is
    // still a character.
    while (index + 1U < length) {
        const uint32_t unit = little_endian
                                  ? (uint32_t)input[index] | ((uint32_t)input[index + 1U] << 8)
                                  : ((uint32_t)input[index] << 8) | (uint32_t)input[index + 1U];
        index += 2U;
        if (unit == 0U) break;
        uint32_t code_point = unit;
        if (unit >= 0xD800U && unit <= 0xDBFFU && index + 1U < length) {
            const uint32_t low =
                little_endian ? (uint32_t)input[index] | ((uint32_t)input[index + 1U] << 8)
                              : ((uint32_t)input[index] << 8) | (uint32_t)input[index + 1U];
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code_point = 0x10000U + ((unit - 0xD800U) << 10) + (low - 0xDC00U);
                index += 2U;
            }
        }
        // A lone surrogate is not a character; drawing it would produce a
        // three-byte sequence no font has.
        if (code_point >= 0xD800U && code_point <= 0xDFFFU) continue;
        if (!append_utf8(output, capacity, &used, sanitise(code_point))) break;
    }
    return trim(output, used);
}

size_t audio_tags_text_to_utf8(uint8_t encoding, const uint8_t *input, size_t length,
                               char *output, size_t capacity)
{
    if (output == NULL || capacity == 0U) return 0U;
    output[0] = '\0';
    if (input == NULL || length == 0U) return 0U;

    switch (encoding) {
    case AUDIO_TAGS_ENCODING_UTF16_BOM: {
        bool little_endian = true;
        if (length >= 2U && input[0] == 0xFFU && input[1] == 0xFEU) {
            input += 2U;
            length -= 2U;
        } else if (length >= 2U && input[0] == 0xFEU && input[1] == 0xFFU) {
            little_endian = false;
            input += 2U;
            length -= 2U;
        } else if (length >= 2U) {
            /* The mark is mandatory for this encoding and still goes missing,
             * and read the wrong way round a title comes out as a column of
             * CJK glyphs. Every alphabet a tag is likely to use lives below
             * U+0900, so the byte that stays small is the high one - which is
             * the second byte when the text is little-endian. */
            if (input[0] == 0U && input[1] != 0U) {
                little_endian = false;
            } else if (input[0] <= 0x08U && input[1] > 0x08U) {
                little_endian = false;
            }
        }
        return decode_utf16(input, length, little_endian, output, capacity);
    }
    case AUDIO_TAGS_ENCODING_UTF16_BE:
        return decode_utf16(input, length, false, output, capacity);
    case AUDIO_TAGS_ENCODING_UTF8: {
        // Already the target encoding, but still has to be bounded, trimmed
        // and cut on a character boundary rather than memcpy'd.
        size_t used = 0U;
        size_t index = 0U;
        while (index < length && input[index] != 0U) {
            const uint8_t lead = input[index];
            size_t width = 1U;
            if (lead >= 0xF0U) width = 4U;
            else if (lead >= 0xE0U) width = 3U;
            else if (lead >= 0xC0U) width = 2U;
            // A truncated or invalid sequence ends the field: guessing at the
            // intent of malformed bytes is how mojibake gets rendered.
            if (index + width > length) break;
            if (width == 1U) {
                if (lead >= 0x80U) break;
                if (!append_utf8(output, capacity, &used, sanitise(lead))) break;
            } else {
                bool valid = true;
                for (size_t extra = 1U; extra < width; ++extra) {
                    if ((input[index + extra] & 0xC0U) != 0x80U) valid = false;
                }
                if (!valid) break;
                if (used + width + 1U > capacity) break;
                memcpy(output + used, input + index, width);
                used += width;
            }
            index += width;
        }
        return trim(output, used);
    }
    case AUDIO_TAGS_ENCODING_ISO8859_1:
    default:
        return decode_single_byte(input, length, output, capacity);
    }
}
