#include "settings_csv.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_CSV_LINE_MAX 512

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
    FILE *file = fopen(path, "r");
    if (file == NULL) return false;
    char line[SETTINGS_CSV_LINE_MAX];
    char parsed_key[SETTINGS_CSV_KEY_MAX_LEN + 1];
    char parsed_value[SETTINGS_CSV_VALUE_MAX_LEN + 1];
    while (fgets(line, sizeof(line), file) != NULL) {
        if (parse_line(line, parsed_key, sizeof(parsed_key), parsed_value, sizeof(parsed_value)) &&
            strcmp(parsed_key, key) == 0) {
            const size_t length = strlen(parsed_value);
            if (length >= value_size) {
                fclose(file);
                return false;
            }
            memcpy(value, parsed_value, length + 1);
            fclose(file);
            return true;
        }
    }
    fclose(file);
    return false;
}

bool settings_csv_set(const char *path, const char *key, const char *value)
{
    if (path == NULL || !valid_field(key, SETTINGS_CSV_KEY_MAX_LEN) ||
        !valid_field(value, SETTINGS_CSV_VALUE_MAX_LEN)) return false;
    char temp_path[SETTINGS_CSV_LINE_MAX];
    const int path_length = snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    if (path_length < 0 || (size_t)path_length >= sizeof(temp_path)) return false;
    FILE *output = fopen(temp_path, "w");
    if (output == NULL) return false;
    errno = 0;
    FILE *input = fopen(path, "r");
    if (input == NULL && errno != ENOENT) {
        fclose(output);
        remove(temp_path);
        return false;
    }
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
                if (!replaced && fprintf(output, "%s,%s\n", key, value) < 0) {
                    fclose(input); fclose(output); remove(temp_path); return false;
                }
                replaced = true;
            } else if (fputs(line, output) == EOF) {
                fclose(input); fclose(output); remove(temp_path); return false;
            }
        }
        if (ferror(input)) {
            fclose(input); fclose(output); remove(temp_path); return false;
        }
        fclose(input);
    }
    if (!replaced && fprintf(output, "%s,%s\n", key, value) < 0) {
        fclose(output); remove(temp_path); return false;
    }
    if (fclose(output) != 0 || rename(temp_path, path) != 0) {
        remove(temp_path);
        return false;
    }
    return true;
}
