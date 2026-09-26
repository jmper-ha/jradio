#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "board_parts.h"
#include "cJSON.h"

static char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    const long size = ftell(file);
    assert(size > 0);
    rewind(file);
    char *text = malloc((size_t)size + 1U);
    assert(text != NULL);
    assert(fread(text, 1U, (size_t)size, file) == (size_t)size);
    text[size] = '\0';
    fclose(file);
    if (length != NULL) *length = (size_t)size;
    return text;
}

/* As tests/test_web_hardware.js writes an issue, so the fixture's strings
 * compare as they are. */
static void normalise(const board_issue_t *issue, char *out, size_t size)
{
    const char *name = board_config_issue_name(issue->code);
    switch (issue->code) {
    case BOARD_ISSUE_PIN_CONFLICT:
        snprintf(out, size, "%s %d", name, issue->gpio);
        break;
    case BOARD_ISSUE_BUS_UNWIRED:
        snprintf(out, size, "%s %s %s", name, issue->key, issue->detail);
        break;
    case BOARD_ISSUE_BAD_VALUE:
    case BOARD_ISSUE_PIN_MISSING:
        snprintf(out, size, "%s %s", name, issue->key);
        break;
    default:
        snprintf(out, size, "%s %s %d", name, issue->key, issue->gpio);
        break;
    }
}

static int compare_strings(const void *left, const void *right)
{
    return strcmp((const char *)left, (const char *)right);
}

#define LINE 96

static void assert_issues(const char *name, const board_issue_t *issues, size_t count,
                          const cJSON *expected)
{
    char got[BOARD_CONFIG_ISSUES_MAX][LINE];
    for (size_t i = 0U; i < count; ++i) normalise(&issues[i], got[i], LINE);
    qsort(got, count, LINE, compare_strings);
    const int expected_count = cJSON_GetArraySize(expected);
    if ((size_t)expected_count != count) {
        fprintf(stderr, "%s: %u issues, expected %d\n", name, (unsigned)count, expected_count);
        for (size_t i = 0U; i < count; ++i) fprintf(stderr, "  got %s\n", got[i]);
    }
    assert((size_t)expected_count == count);
    for (size_t i = 0U; i < count; ++i) {
        const cJSON *item = cJSON_GetArrayItem(expected, (int)i);
        if (strcmp(got[i], item->valuestring) != 0) {
            fprintf(stderr, "%s: got \"%s\", expected \"%s\"\n", name, got[i], item->valuestring);
        }
        assert(strcmp(got[i], item->valuestring) == 0);
    }
}

/* The cases the wiring editor's test runs too: the firmware and the page
 * give one board one answer. */
static void test_the_cases_shared_with_the_editor(void)
{
    char *json = read_file("tests/fixtures/board_cases.json", NULL);
    cJSON *root = cJSON_Parse(json);
    assert(root != NULL);
    const char *base = cJSON_GetObjectItem(root, "base")->valuestring;

    // The base is the editor's README board, and this writes it back the same.
    board_config_t config;
    board_config_clear(&config);
    board_config_parse_result_t parsed;
    board_config_parse_csv(base, strlen(base), &config, &parsed);
    assert(parsed.bad_lines == 0U && parsed.unknown_keys == 0U);
    char written[2048];
    const size_t length = board_config_to_csv(&config, written, sizeof(written));
    assert(length < sizeof(written));
    assert(strcmp(written, base) == 0);

    const cJSON *cases = cJSON_GetObjectItem(root, "cases");
    const cJSON *item = NULL;
    size_t ran = 0U;
    cJSON_ArrayForEach(item, cases)
    {
        const char *name = cJSON_GetObjectItem(item, "name")->valuestring;
        const char *csv = cJSON_GetObjectItem(item, "csv")->valuestring;
        board_config_clear(&config);
        board_config_parse_csv(base, strlen(base), &config, NULL);
        board_config_parse_csv(csv, strlen(csv), &config, &parsed);
        board_config_report_t report;
        board_config_validate(&config, &report);
        assert(report.dropped == 0U);
        assert_issues(name, report.errors, report.error_count, cJSON_GetObjectItem(item, "errors"));
        assert_issues(name, report.warnings, report.warning_count,
                      cJSON_GetObjectItem(item, "warnings"));
        assert(parsed.unknown_keys == (size_t)cJSON_GetObjectItem(item, "unknown_keys")->valueint);
        assert(parsed.bad_lines == (size_t)cJSON_GetObjectItem(item, "bad_lines")->valueint);
        ++ran;
    }
    assert(ran >= 19U);
    cJSON_Delete(root);
    free(json);
}

static void test_what_the_values_become(void)
{
    board_config_t config;
    board_config_clear(&config);
    const char text[] = "display,ili9488_480_320\n"
                        "tft_reset,rst\n"
                        "bluetooth,jradio_bt\n"
                        "uart1_tx,13\n"
                        "board_name,  Kitchen radio  \n"
                        "button_next,none\n";
    board_config_parse_csv(text, sizeof(text) - 1U, &config, NULL);
    assert(config.display == DISPLAY_ILI9488_480_320);
    assert(config.tft_reset == BOARD_PIN_RESET);
    assert(config.bluetooth == BLUETOOTH_JRADIO_BT);
    assert(config.uart1_tx == 13);
    assert(config.dac == DAC_PCM5102);
    assert(strcmp(config.board_name, "Kitchen radio") == 0);
    assert(config.button_next == BOARD_PIN_NONE);
    // SPI2 cannot be moved by a file.
    board_config_parse_csv("spi2_sclk,5\n", 12U, &config, NULL);
    assert(config.spi2_sclk == 12);
}

static void test_a_bad_line_is_counted_and_the_value_kept(void)
{
    board_config_t config;
    board_config_clear(&config);
    config.tft_cs = 10;
    board_config_parse_result_t parsed;
    const char text[] = "# key,value\n\nno comma here\ntft_cs,ten\ntft_dc,47\n";
    board_config_parse_csv(text, sizeof(text) - 1U, &config, &parsed);
    assert(parsed.bad_lines == 2U);
    assert(parsed.first_bad_line == 3U);
    assert(config.tft_cs == 10);
    assert(config.tft_dc == 47);
}

static void test_the_compiled_board_writes_and_reads_back(void)
{
    /* Whatever board_options.h holds on this machine: what the compiled
     * board writes, read over a cleared one, writes the same again. */
    board_config_t compiled;
    board_config_compiled(&compiled);
    char first[2048];
    char second[2048];
    assert(board_config_to_csv(&compiled, first, sizeof(first)) < sizeof(first));
    board_config_t read_back;
    board_config_clear(&read_back);
    board_config_parse_result_t parsed;
    board_config_parse_csv(first, strlen(first), &read_back, &parsed);
    assert(parsed.bad_lines == 0U && parsed.unknown_keys == 0U);
    assert(board_config_to_csv(&read_back, second, sizeof(second)) < sizeof(second));
    assert(strcmp(first, second) == 0);
}

static void test_a_short_buffer_is_cut_and_says_how_much_it_needed(void)
{
    board_config_t config;
    board_config_compiled(&config);
    char whole[2048];
    const size_t needed = board_config_to_csv(&config, whole, sizeof(whole));
    char small[64];
    assert(board_config_to_csv(&config, small, sizeof(small)) == needed);
    assert(strlen(small) == sizeof(small) - 1U);
    assert(board_config_to_csv(&config, NULL, 0U) == needed);
}

static void test_the_crc_is_zlibs(void)
{
    assert(board_config_crc32((const uint8_t *)"123456789", 9U) == 0xCBF43926U);
    assert(board_config_crc32(NULL, 0U) == 0U);
}

static void test_the_partition_blob(void)
{
    static uint8_t blob[BOARD_BLOB_MAX];
    const char text[] = "tft_cs,10\n";
    const size_t length = board_config_blob_write(text, sizeof(text) - 1U, blob, sizeof(blob));
    assert(length == BOARD_BLOB_HEADER_SIZE + sizeof(text) - 1U);
    assert(memcmp(blob, "JRBD", 4U) == 0);

    const char *found = NULL;
    size_t found_length = 0U;
    assert(board_config_blob_read(blob, sizeof(blob), &found, &found_length) == BOARD_BLOB_OK);
    assert(found_length == sizeof(text) - 1U && memcmp(found, text, found_length) == 0);

    // A torn write: the text changed under its CRC.
    blob[BOARD_BLOB_HEADER_SIZE] ^= 1U;
    assert(board_config_blob_read(blob, sizeof(blob), &found, &found_length) == BOARD_BLOB_DAMAGED);
    assert(found == NULL);
    blob[BOARD_BLOB_HEADER_SIZE] ^= 1U;

    // A length past the partition.
    blob[10] = 0xFFU;
    assert(board_config_blob_read(blob, sizeof(blob), NULL, NULL) == BOARD_BLOB_DAMAGED);
    blob[10] = 0U;

    blob[4] = 2U;
    assert(board_config_blob_read(blob, sizeof(blob), NULL, NULL) == BOARD_BLOB_VERSION_UNKNOWN);

    // An erased partition.
    memset(blob, 0xFF, sizeof(blob));
    assert(board_config_blob_read(blob, sizeof(blob), NULL, NULL) == BOARD_BLOB_EMPTY);

    // Too long for the partition is refused, not cut.
    static char long_text[BOARD_BLOB_MAX];
    memset(long_text, 'x', sizeof(long_text));
    assert(board_config_blob_write(long_text, sizeof(long_text), blob, sizeof(blob)) == 0U);
}

int main(void)
{
    test_the_cases_shared_with_the_editor();
    test_what_the_values_become();
    test_a_bad_line_is_counted_and_the_value_kept();
    test_the_compiled_board_writes_and_reads_back();
    test_a_short_buffer_is_cut_and_says_how_much_it_needed();
    test_the_crc_is_zlibs();
    test_the_partition_blob();
    puts("board_config tests passed");
    return 0;
}
