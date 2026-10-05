#include "settings_csv.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

#define SETTINGS_CSV_LINE_MAX 512

#ifdef ESP_PLATFORM
static const char *TAG = "settings_csv";
/* Created once from app_main, before the tasks that write settings exist, so
 * there is no race on the creation itself. */
static SemaphoreHandle_t s_lock;

static bool settings_csv_lock(void)
{
    if (s_lock == NULL) {
        /* Refuse rather than fall back to an unsynchronised write: silently
         * running unlocked is exactly the bug this guards against, and it
         * would only show up as a corrupted file much later. */
        ESP_LOGE(TAG, "settings_csv_init() was never called");
        return false;
    }
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void settings_csv_unlock(void)
{
    if (s_lock != NULL) xSemaphoreGive(s_lock);
}

void settings_csv_init(void)
{
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
}
#else
/* The host tests are single-threaded. */
static bool settings_csv_lock(void) { return true; }
static void settings_csv_unlock(void) {}
void settings_csv_init(void) {}
#endif

static bool valid_field(const char *text, size_t max_length)
{
    if (text == NULL || text[0] == '\0' || strlen(text) > max_length) return false;
    return strchr(text, ',') == NULL && strpbrk(text, "\r\n") == NULL;
}

static bool parse_line(const char *line, char *key, size_t key_size,
                       char *value, size_t value_size)
{
    const char *comma = strchr(line, ',');
    if (comma == NULL || comma == line) return false;
    const size_t key_length = (size_t)(comma - line);
    if (key_length == 0 || key_length >= key_size) return false;
    const char *value_start = comma + 1;
    const char *end = value_start + strcspn(value_start, "\r\n");
    const size_t value_length = (size_t)(end - value_start);
    if (value_length == 0 || value_length >= value_size) return false;
    memcpy(key, line, key_length);
    key[key_length] = '\0';
    memcpy(value, value_start, value_length);
    value[value_length] = '\0';
    return true;
}

bool settings_csv_get(const char *path, const char *key, char *value, size_t value_size)
{
    if (path == NULL || !valid_field(key, SETTINGS_CSV_KEY_MAX_LEN) ||
        value == NULL || value_size == 0) return false;
    if (!settings_csv_lock()) return false;
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        settings_csv_unlock();
        return false;
    }
    char line[SETTINGS_CSV_LINE_MAX];
    char parsed_key[SETTINGS_CSV_KEY_MAX_LEN + 1];
    char parsed_value[SETTINGS_CSV_VALUE_MAX_LEN + 1];
    while (fgets(line, sizeof(line), file) != NULL) {
        if (parse_line(line, parsed_key, sizeof(parsed_key), parsed_value, sizeof(parsed_value)) &&
            strcmp(parsed_key, key) == 0) {
            const size_t length = strlen(parsed_value);
            if (length >= value_size) {
                fclose(file);
                settings_csv_unlock();
                return false;
            }
            memcpy(value, parsed_value, length + 1);
            fclose(file);
            settings_csv_unlock();
            return true;
        }
    }
    fclose(file);
    settings_csv_unlock();
    return false;
}

void settings_csv_snapshot_load(settings_csv_snapshot_t *snapshot, const char *path)
{
    if (snapshot == NULL) return;
    *snapshot = (settings_csv_snapshot_t){.text = NULL, .path = path};
    if (path == NULL || !settings_csv_lock()) return;
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        settings_csv_unlock();
        snapshot->text = calloc(1U, 1U);
        return;
    }
    char *text = NULL;
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    if (size >= 0 && (unsigned long)size <= SETTINGS_CSV_SNAPSHOT_MAX &&
        fseek(file, 0, SEEK_SET) == 0) {
        text = malloc((size_t)size + 1U);
        if (text != NULL) {
            const size_t got = fread(text, 1U, (size_t)size, file);
            text[got] = '\0';
        }
    }
    fclose(file);
    settings_csv_unlock();
    snapshot->text = text;
}

/* The next line as fgets() into a SETTINGS_CSV_LINE_MAX buffer would hand it
 * over: through the newline, or cut at the buffer's length, so a file reads
 * the same here as through get(). NULL at the end. */
static const char *snapshot_next_line(const char *cursor, char *line)
{
    if (cursor == NULL || *cursor == '\0') return NULL;
    size_t length = 0U;
    while (length < SETTINGS_CSV_LINE_MAX - 1U && cursor[length] != '\0') {
        if (cursor[length++] == '\n') break;
    }
    memcpy(line, cursor, length);
    line[length] = '\0';
    return cursor + length;
}

bool settings_csv_snapshot_get(const settings_csv_snapshot_t *snapshot, const char *key,
                               char *value, size_t value_size)
{
    if (snapshot == NULL) return false;
    if (snapshot->text == NULL) return settings_csv_get(snapshot->path, key, value, value_size);
    if (!valid_field(key, SETTINGS_CSV_KEY_MAX_LEN) || value == NULL || value_size == 0) {
        return false;
    }
    char line[SETTINGS_CSV_LINE_MAX];
    char parsed_key[SETTINGS_CSV_KEY_MAX_LEN + 1];
    char parsed_value[SETTINGS_CSV_VALUE_MAX_LEN + 1];
    const char *cursor = snapshot->text;
    while ((cursor = snapshot_next_line(cursor, line)) != NULL) {
        if (parse_line(line, parsed_key, sizeof(parsed_key), parsed_value, sizeof(parsed_value)) &&
            strcmp(parsed_key, key) == 0) {
            const size_t length = strlen(parsed_value);
            if (length >= value_size) return false;
            memcpy(value, parsed_value, length + 1);
            return true;
        }
    }
    return false;
}

void settings_csv_snapshot_free(settings_csv_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    free(snapshot->text);
    snapshot->text = NULL;
}

bool settings_csv_set(const char *path, const char *key, const char *value)
{
    if (path == NULL || !valid_field(key, SETTINGS_CSV_KEY_MAX_LEN) ||
        !valid_field(value, SETTINGS_CSV_VALUE_MAX_LEN)) return false;
    char temp_path[SETTINGS_CSV_LINE_MAX];
    const int path_length = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (path_length < 0 || (size_t)path_length >= sizeof(temp_path)) return false;

    /* Held across the whole read-modify-write, not just the rename: two
     * writers that each read the old file would otherwise both write back
     * their own copy, and whichever renamed last would drop the other's key. */
    if (!settings_csv_lock()) return false;

    bool ok = false;
    FILE *input = NULL;
    FILE *output = fopen(temp_path, "w");
    if (output == NULL) goto done;
    errno = 0;
    input = fopen(path, "r");
    if (input == NULL && errno != ENOENT) goto done;

    bool replaced = false;
    char line[SETTINGS_CSV_LINE_MAX];
    if (input != NULL) {
        while (fgets(line, sizeof(line), input) != NULL) {
            char parsed_key[SETTINGS_CSV_KEY_MAX_LEN + 1];
            char parsed_value[SETTINGS_CSV_VALUE_MAX_LEN + 1];
            const bool matches = parse_line(line, parsed_key, sizeof(parsed_key),
                                            parsed_value, sizeof(parsed_value)) &&
                                 strcmp(parsed_key, key) == 0;
            if (matches) {
                if (!replaced && fprintf(output, "%s,%s\n", key, value) < 0) goto done;
                replaced = true;
            } else if (fputs(line, output) == EOF) {
                goto done;
            }
        }
        if (ferror(input)) goto done;
        fclose(input);
        input = NULL;
    }
    if (!replaced && fprintf(output, "%s,%s\n", key, value) < 0) goto done;
    if (fclose(output) != 0) {
        output = NULL;
        goto done;
    }
    output = NULL;
    ok = rename(temp_path, path) == 0;

done:
    if (input != NULL) fclose(input);
    if (output != NULL) fclose(output);
    if (!ok) remove(temp_path);
    settings_csv_unlock();
    return ok;
}
