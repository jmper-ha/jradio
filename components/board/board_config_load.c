#include "board_config.h"

#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

#include "board_parts.h"

static const char *TAG = "board_config";

static board_config_t s_config;
static board_config_source_t s_source = BOARD_CONFIG_SOURCE_COMPILED;
static bool s_loaded;

static void log_issues(const board_config_report_t *report)
{
    for (size_t i = 0U; i < report->error_count; ++i) {
        const board_issue_t *issue = &report->errors[i];
        ESP_LOGE(TAG, "  error %s: %s%s%s gpio=%d", board_config_issue_name(issue->code),
                 issue->key != NULL ? issue->key : "", issue->detail != NULL ? " " : "",
                 issue->detail != NULL ? issue->detail : "", issue->gpio);
    }
    for (size_t i = 0U; i < report->warning_count; ++i) {
        const board_issue_t *issue = &report->warnings[i];
        ESP_LOGW(TAG, "  warning %s: %s gpio=%d", board_config_issue_name(issue->code),
                 issue->key != NULL ? issue->key : "", issue->gpio);
    }
    if (report->dropped > 0U) ESP_LOGW(TAG, "  and %u more", (unsigned)report->dropped);
}

/* The file is taken whole or not at all: a wiring the rules refuse is not
 * obeyed in part, since a pin taken from the file beside a pin taken from
 * the header can be the very conflict the rules were there to catch. */
void board_config_load(void)
{
    if (s_loaded) return;
    s_loaded = true;
    board_config_compiled(&s_config);
    s_source = BOARD_CONFIG_SOURCE_COMPILED;

    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "board");
    if (partition == NULL) {
        ESP_LOGI(TAG, "no board partition; the compiled board");
        return;
    }
    const size_t size = partition->size < BOARD_BLOB_MAX ? partition->size : BOARD_BLOB_MAX;
    // Freed before returning; PSRAM is up by the time app_main runs.
    uint8_t *blob = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (blob == NULL) blob = malloc(size);
    if (blob == NULL) {
        ESP_LOGW(TAG, "no memory to read the board partition; the compiled board");
        return;
    }
    if (esp_partition_read(partition, 0, blob, size) != ESP_OK) {
        ESP_LOGW(TAG, "board partition unreadable; the compiled board");
        free(blob);
        return;
    }
    const char *text = NULL;
    size_t length = 0U;
    const board_blob_status_t status = board_config_blob_read(blob, size, &text, &length);
    if (status != BOARD_BLOB_OK) {
        ESP_LOGI(TAG, "board partition %s; the compiled board",
                 status == BOARD_BLOB_EMPTY             ? "empty"
                 : status == BOARD_BLOB_VERSION_UNKNOWN ? "from a newer format"
                                                        : "damaged");
        free(blob);
        return;
    }

    board_config_t candidate;
    board_config_compiled(&candidate);
    board_config_parse_result_t parsed;
    board_config_parse_csv(text, length, &candidate, &parsed);
    free(blob);
    if (parsed.bad_lines > 0U) {
        ESP_LOGW(TAG, "board.csv: %u bad line%s, the first at line %u", (unsigned)parsed.bad_lines,
                 parsed.bad_lines == 1U ? "" : "s", (unsigned)parsed.first_bad_line);
    }
    if (parsed.unknown_keys > 0U) {
        ESP_LOGW(TAG, "board.csv: %u key%s this firmware does not know",
                 (unsigned)parsed.unknown_keys, parsed.unknown_keys == 1U ? "" : "s");
    }

    board_config_report_t report;
    board_config_validate(&candidate, &report);
    /* The layouts are compiled for one panel shape, and the I2S format for
     * one DAC: a file for another is for another binary. */
    board_config_t compiled;
    board_config_compiled(&compiled);
    const bool display_matches = candidate.display == compiled.display;
    const bool dac_matches = candidate.dac == compiled.dac;
    if (report.error_count > 0U || !display_matches || !dac_matches) {
        ESP_LOGE(TAG, "board.csv refused (%u error%s%s%s); the compiled board",
                 (unsigned)report.error_count, report.error_count == 1U ? "" : "s",
                 display_matches ? "" : ", built for another display",
                 dac_matches ? "" : ", built for another DAC");
        log_issues(&report);
        return;
    }
    s_config = candidate;
    s_source = BOARD_CONFIG_SOURCE_PARTITION;
    if (s_config.board_name[0] != '\0') {
        ESP_LOGI(TAG, "board.csv from the partition: \"%s\"", s_config.board_name);
    } else {
        ESP_LOGI(TAG, "board.csv from the partition");
    }
    log_issues(&report);
}

const board_config_t *board_config_get(void)
{
    if (!s_loaded) board_config_load();
    return &s_config;
}

board_config_source_t board_config_source(void)
{
    return s_source;
}

bool board_has_usb(void)
{
    return board_config_get()->usb_dp >= 0;
}

bool board_has_sd_card(void)
{
    return board_config_get()->sd_cs >= 0;
}

bool board_has_ir(void)
{
    return board_config_get()->ir_receiver >= 0;
}

bool board_has_bluetooth(void)
{
    return board_config_get()->bluetooth == BLUETOOTH_JRADIO_BT;
}
