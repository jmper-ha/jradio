#include "web_json.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void web_json_init(web_json_writer_t *writer, char *output, size_t output_size,
                   size_t limit)
{
    if (writer == NULL) {
        return;
    }
    writer->output = output;
    writer->capacity = output_size;
    writer->limit = limit;
    writer->length = 0U;
    writer->valid = output != NULL && output_size > 0U;
    if (writer->valid) {
        output[0] = '\0';
    }
}

bool web_json_valid(const web_json_writer_t *writer)
{
    return writer != NULL && writer->valid;
}

size_t web_json_length(const web_json_writer_t *writer)
{
    return writer == NULL ? 0U : writer->length;
}

void web_json_invalidate(web_json_writer_t *writer)
{
    if (writer != NULL) {
        writer->valid = false;
    }
}

void web_json_truncate(web_json_writer_t *writer)
{
    if (writer == NULL || writer->output == NULL || writer->capacity == 0U) {
        return;
    }
    writer->output[0] = '\0';
    writer->length = 0U;
}

void web_json_bytes(web_json_writer_t *writer, const char *bytes, size_t length)
{
    if (writer == NULL) {
        return;
    }
    // The capacity test keeps one byte for the terminator, which is why it is
    // >= rather than >.
    if (!writer->valid || bytes == NULL || length > writer->limit - writer->length ||
        length >= writer->capacity - writer->length) {
        writer->valid = false;
        return;
    }
    memcpy(writer->output + writer->length, bytes, length);
    writer->length += length;
    writer->output[writer->length] = '\0';
}

void web_json_literal(web_json_writer_t *writer, const char *literal)
{
    if (literal == NULL) {
        if (writer != NULL) {
            writer->valid = false;
        }
        return;
    }
    web_json_bytes(writer, literal, strlen(literal));
}

void web_json_format(web_json_writer_t *writer, const char *format, ...)
{
    char value[32];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(value, sizeof(value), format, args);
    va_end(args);
    if (length < 0 || (size_t)length >= sizeof(value)) {
        if (writer != NULL) {
            writer->valid = false;
        }
        return;
    }
    web_json_bytes(writer, value, (size_t)length);
}

static bool utf8_sequence(const unsigned char *value, size_t length,
                          size_t *consumed)
{
    const unsigned char first = value[0];
    if (first < 0x80U) {
        *consumed = 1U;
        return true;
    }

    size_t expected;
    uint32_t codepoint;
    uint32_t minimum;
    if (first >= 0xC2U && first <= 0xDFU) {
        expected = 2U;
        codepoint = first & 0x1FU;
        minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        expected = 3U;
        codepoint = first & 0x0FU;
        minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        expected = 4U;
        codepoint = first & 0x07U;
        minimum = 0x10000U;
    } else {
        *consumed = 1U;
        return false;
    }

    size_t continuation_count = 0U;
    while (continuation_count + 1U < expected &&
           continuation_count + 1U < length &&
           (value[continuation_count + 1U] & 0xC0U) == 0x80U) {
        ++continuation_count;
    }
    *consumed = 1U + continuation_count;
    if (*consumed != expected) {
        return false;
    }
    for (size_t index = 1U; index < expected; ++index) {
        codepoint = (codepoint << 6) | (uint32_t)(value[index] & 0x3FU);
    }
    return codepoint >= minimum && codepoint <= 0x10FFFFU &&
           !(codepoint >= 0xD800U && codepoint <= 0xDFFFU);
}

void web_json_string_bounded(web_json_writer_t *writer, const char *text,
                             size_t content_budget)
{
    static const char replacement[] = "\xEF\xBF\xBD";
    if (writer == NULL) {
        return;
    }
    web_json_literal(writer, "\"");
    if (text == NULL) {
        text = "";
    }
    const size_t length = strlen(text);
    size_t offset = 0U;
    size_t encoded = 0U;
    while (offset < length && writer->valid) {
        const unsigned char value = (unsigned char)text[offset];
        const char *escaped = NULL;
        size_t escaped_length = 0U;
        size_t consumed = 1U;
        if (value == '"') {
            escaped = "\\\"";
            escaped_length = 2U;
        } else if (value == '\\') {
            escaped = "\\\\";
            escaped_length = 2U;
        } else if (value < 0x20U || value == 0x7FU) {
            /* Every control character goes out as \uXXXX, including the ones
             * JSON gives short forms to. Six is what actually gets written, so
             * six is what the budget must be charged - an earlier version
             * charged two for \n and \t while emitting six, which let a
             * bounded field overrun its budget. */
            escaped_length = 6U;
        } else if (value < 0x80U) {
            escaped = text + offset;
            escaped_length = 1U;
        } else {
            if (utf8_sequence((const unsigned char *)text + offset,
                              length - offset, &consumed)) {
                escaped = text + offset;
                escaped_length = consumed;
            } else {
                escaped = replacement;
                escaped_length = sizeof(replacement) - 1U;
            }
        }
        if (escaped_length > content_budget - encoded) {
            break;
        }
        if (value < 0x20U || value == 0x7FU) {
            web_json_format(writer, "\\u%04x", (unsigned)value);
        } else {
            web_json_bytes(writer, escaped, escaped_length);
        }
        encoded += escaped_length;
        offset += consumed;
    }
    web_json_literal(writer, "\"");
}

void web_json_string(web_json_writer_t *writer, const char *text)
{
    // No separate cap: the writer's own limit already bounds the document, and
    // a field budget larger than that can never bite first.
    web_json_string_bounded(writer, text, writer == NULL ? 0U : writer->limit);
}
