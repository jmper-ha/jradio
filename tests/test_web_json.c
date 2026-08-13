#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "web_json.h"

static void test_a_document_is_written_verbatim_within_its_bounds(void)
{
    char buffer[64];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_literal(&writer, "{\"count\":");
    web_json_format(&writer, "%u", 42U);
    web_json_literal(&writer, "}");

    assert(web_json_valid(&writer));
    assert(strcmp(buffer, "{\"count\":42}") == 0);
    assert(web_json_length(&writer) == strlen(buffer));
}

static void test_control_characters_and_quotes_are_escaped(void)
{
    char buffer[128];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_string(&writer, "a\"b\\c\nd\te\x01");

    /* Quote and backslash get their short forms; every control character goes
     * out as \uXXXX, which is valid JSON and keeps one rule for all of them. */
    assert(web_json_valid(&writer));
    assert(strcmp(buffer, "\"a\\\"b\\\\c\\u000ad\\u0009e\\u0001\"") == 0);
}

static void test_a_control_character_is_charged_its_full_escaped_width(void)
{
    /* Six bytes are written for \uXXXX, so six must be charged: an earlier
     * version charged two for a newline and let the field exceed its budget. */
    char buffer[64];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_string_bounded(&writer, "\n\n", 6U);

    assert(web_json_valid(&writer));
    assert(strcmp(buffer, "\"\\u000a\"") == 0);
}

static void test_a_file_name_with_a_quote_cannot_break_the_document(void)
{
    /* FAT allows names this shape, and an unescaped quote would end the string
     * early and produce a document the browser silently mis-parses. */
    char buffer[128];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_literal(&writer, "{\"label\":");
    web_json_string(&writer, "12\" mix.mp3");
    web_json_literal(&writer, "}");

    assert(web_json_valid(&writer));
    assert(strcmp(buffer, "{\"label\":\"12\\\" mix.mp3\"}") == 0);
}

static void test_valid_utf8_passes_through_untouched(void)
{
    char buffer[128];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_string(&writer, "Пляж");

    assert(web_json_valid(&writer));
    assert(strcmp(buffer, "\"Пляж\"") == 0);
}

static void test_invalid_utf8_is_replaced_rather_than_forwarded(void)
{
    /* Names come off a FAT volume, so nothing guarantees they decode. A raw
     * invalid byte would make the whole response unparseable. */
    char buffer[128];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_string(&writer, "bad\xFFtail");

    assert(web_json_valid(&writer));
    assert(strcmp(buffer, "\"bad\xEF\xBF\xBDtail\"") == 0);
}

static void test_a_truncated_multibyte_sequence_is_replaced(void)
{
    char buffer[128];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    /* First byte of a two-byte sequence with its continuation missing. */
    web_json_string(&writer, "\xD0");

    assert(web_json_valid(&writer));
    assert(strcmp(buffer, "\"\xEF\xBF\xBD\"") == 0);
}

static void test_overflowing_the_buffer_invalidates_instead_of_truncating(void)
{
    char buffer[8];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_literal(&writer, "0123456789012345");

    /* A half-written document must never look like a complete short one. */
    assert(!web_json_valid(&writer));
}

static void test_the_limit_bounds_output_below_the_buffer_size(void)
{
    /* The socket path passes a frame cap smaller than its buffer; the cap has
     * to bite first, or an oversized frame reaches the client. */
    char buffer[64];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), 10U);

    web_json_literal(&writer, "0123456789ABCDEF");
    assert(!web_json_valid(&writer));

    web_json_init(&writer, buffer, sizeof(buffer), 10U);
    web_json_literal(&writer, "0123456789");
    assert(web_json_valid(&writer));
    assert(web_json_length(&writer) == 10U);
}

static void test_a_field_budget_caps_one_string_not_the_document(void)
{
    char buffer[64];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_string_bounded(&writer, "abcdefghij", 4U);
    /* The string stops at the budget, and the document stays well-formed. */
    assert(web_json_valid(&writer));
    assert(strcmp(buffer, "\"abcd\"") == 0);
}

static void test_a_budget_never_splits_a_multibyte_character(void)
{
    /* Cutting mid-sequence would emit a byte the client cannot decode; the
     * writer must drop the whole character instead. */
    char buffer[64];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_string_bounded(&writer, "аб", 3U);

    assert(web_json_valid(&writer));
    assert(strcmp(buffer, "\"а\"") == 0);
}

static void test_a_rejected_document_is_emptied_not_left_half_written(void)
{
    char buffer[16];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_literal(&writer, "{\"a\":1");
    web_json_invalidate(&writer);
    assert(!web_json_valid(&writer));

    web_json_truncate(&writer);
    assert(buffer[0] == '\0');
    assert(web_json_length(&writer) == 0U);
}

static void test_a_null_string_writes_an_empty_one(void)
{
    char buffer[16];
    web_json_writer_t writer;
    web_json_init(&writer, buffer, sizeof(buffer), sizeof(buffer));

    web_json_string(&writer, NULL);

    assert(web_json_valid(&writer));
    assert(strcmp(buffer, "\"\"") == 0);
}

static void test_the_writer_tolerates_being_handed_nothing(void)
{
    web_json_writer_t writer;
    web_json_init(&writer, NULL, 0U, 0U);
    assert(!web_json_valid(&writer));

    web_json_literal(&writer, "x");
    web_json_string(&writer, "x");
    web_json_truncate(&writer);
    assert(web_json_length(&writer) == 0U);

    /* And that no call dereferences a missing writer. */
    web_json_init(NULL, NULL, 0U, 0U);
    web_json_literal(NULL, "x");
    web_json_string(NULL, "x");
    web_json_format(NULL, "%u", 1U);
    web_json_invalidate(NULL);
    web_json_truncate(NULL);
    assert(!web_json_valid(NULL));
}

int main(void)
{
    test_a_document_is_written_verbatim_within_its_bounds();
    test_control_characters_and_quotes_are_escaped();
    test_a_control_character_is_charged_its_full_escaped_width();
    test_a_file_name_with_a_quote_cannot_break_the_document();
    test_valid_utf8_passes_through_untouched();
    test_invalid_utf8_is_replaced_rather_than_forwarded();
    test_a_truncated_multibyte_sequence_is_replaced();
    test_overflowing_the_buffer_invalidates_instead_of_truncating();
    test_the_limit_bounds_output_below_the_buffer_size();
    test_a_field_budget_caps_one_string_not_the_document();
    test_a_budget_never_splits_a_multibyte_character();
    test_a_rejected_document_is_emptied_not_left_half_written();
    test_a_null_string_writes_an_empty_one();
    test_the_writer_tolerates_being_handed_nothing();
    puts("web_json tests passed");
    return 0;
}
