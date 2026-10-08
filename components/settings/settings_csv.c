#include "settings_csv.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
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

/* Writes waiting for the background writer - see settings_csv_set_later().
 * Small on purpose: what goes through here is a volume or a brightness, a
 * few short values that change while a hand is on a control. */
#define PENDING_MAX 8U
#define PENDING_PATH_MAX 64U
#define PENDING_KEY_MAX 32U
#define PENDING_VALUE_MAX 32U

typedef struct {
    bool used;
    uint32_t serial;
    char path[PENDING_PATH_MAX];
    char key[PENDING_KEY_MAX];
    char value[PENDING_VALUE_MAX];
} pending_t;

static pending_t s_pending[PENDING_MAX];
static uint32_t s_pending_serial;

#ifdef ESP_PLATFORM
/* A spinlock, not the file mutex: a reader asks the table before it takes
 * the file, and must not wait behind a write that is the whole point of the
 * table. Held only to copy a few dozen bytes. */
static portMUX_TYPE s_pending_mux = portMUX_INITIALIZER_UNLOCKED;
#define PENDING_ENTER() portENTER_CRITICAL(&s_pending_mux)
#define PENDING_EXIT() portEXIT_CRITICAL(&s_pending_mux)
#else
#define PENDING_ENTER() do { } while (0)
#define PENDING_EXIT() do { } while (0)
#endif

/* A value set but not yet on the card answers in place of the file: the
 * screen reloads its settings when the web changes one, and a reload in the
 * moment before the write would otherwise put the old volume back. */
static bool pending_lookup(const char *path, const char *key, char *value, size_t value_size)
{
    bool found = false;
    PENDING_ENTER();
    for (size_t index = 0U; index < PENDING_MAX; ++index) {
        const pending_t *entry = &s_pending[index];
        if (entry->used && strcmp(entry->path, path) == 0 && strcmp(entry->key, key) == 0) {
            const size_t length = strlen(entry->value);
            if (length < value_size) {
                memcpy(value, entry->value, length + 1U);
                found = true;
            }
            break;
        }
    }
    PENDING_EXIT();
    return found;
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
    if (pending_lookup(path, key, value, value_size)) return true;
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
    if (snapshot->path != NULL && pending_lookup(snapshot->path, key, value, value_size)) {
        return true;
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

/* Takes the oldest waiting write, leaving it in the table so readers still
 * see it while it is written. */
static bool pending_take(pending_t *out)
{
    bool found = false;
    PENDING_ENTER();
    const pending_t *oldest = NULL;
    for (size_t index = 0U; index < PENDING_MAX; ++index) {
        const pending_t *entry = &s_pending[index];
        if (entry->used && (oldest == NULL || entry->serial - oldest->serial > UINT32_MAX / 2U)) {
            oldest = entry;
        }
    }
    if (oldest != NULL) {
        *out = *oldest;
        found = true;
    }
    PENDING_EXIT();
    return found;
}

/* Gone from the table once written - unless it was set again meanwhile, in
 * which case the newer value waits for its own turn. */
static void pending_done(const pending_t *written)
{
    PENDING_ENTER();
    for (size_t index = 0U; index < PENDING_MAX; ++index) {
        pending_t *entry = &s_pending[index];
        if (entry->used && entry->serial == written->serial) {
            entry->used = false;
            break;
        }
    }
    PENDING_EXIT();
}

size_t settings_csv_flush_pending(void)
{
    size_t written = 0U;
    pending_t entry;
    while (pending_take(&entry)) {
        /* A write that fails is not retried: the value still stands in
         * memory and the next change writes it again. */
        if (settings_csv_set(entry.path, entry.key, entry.value)) ++written;
        pending_done(&entry);
    }
    return written;
}

/* Queues the write, replacing one for the same key that has not gone out
 * yet. False when the table has no room or the fields do not fit it. */
static bool pending_put(const char *path, const char *key, const char *value)
{
    if (strlen(path) >= PENDING_PATH_MAX || strlen(key) >= PENDING_KEY_MAX ||
        strlen(value) >= PENDING_VALUE_MAX) {
        return false;
    }
    bool queued = false;
    PENDING_ENTER();
    pending_t *slot = NULL;
    for (size_t index = 0U; index < PENDING_MAX; ++index) {
        pending_t *entry = &s_pending[index];
        if (entry->used && strcmp(entry->path, path) == 0 && strcmp(entry->key, key) == 0) {
            slot = entry;
            break;
        }
        if (!entry->used && slot == NULL) slot = entry;
    }
    if (slot != NULL) {
        slot->used = true;
        slot->serial = ++s_pending_serial;
        memcpy(slot->path, path, strlen(path) + 1U);
        memcpy(slot->key, key, strlen(key) + 1U);
        memcpy(slot->value, value, strlen(value) + 1U);
        queued = true;
    }
    PENDING_EXIT();
    return queued;
}

#ifdef ESP_PLATFORM
/* Writes the card off the caller's task. A settings write is a
 * read-modify-write of the whole file on LittleFS, measured at 80 to 720 ms,
 * and the screen saved the volume itself: for that long after every turn of
 * the encoder the meter and the marquee stood still.
 *
 * The writer exists only while there is something to write. Its stack is
 * internal - flash is written - and 6 KB of internal RAM held for good is
 * more than a write a minute is worth. */
/* 6 KB: LittleFS compacting a directory inside the rename took all but 24
 * bytes of 4 KB, and the next write overflowed it - into the heap, which
 * the next allocation found broken. */
#define SETTINGS_WRITER_STACK 6144

static bool s_writer_running;

static void settings_writer(void *arg)
{
    (void)arg;
    for (;;) {
        (void)settings_csv_flush_pending();
        bool more = false;
        PENDING_ENTER();
        for (size_t index = 0U; index < PENDING_MAX && !more; ++index) {
            more = s_pending[index].used;
        }
        if (!more) s_writer_running = false;
        PENDING_EXIT();
        if (!more) break;
    }
    vTaskDelete(NULL);
}

bool settings_csv_set_later(const char *path, const char *key, const char *value)
{
    if (path == NULL || !valid_field(key, SETTINGS_CSV_KEY_MAX_LEN) ||
        !valid_field(value, SETTINGS_CSV_VALUE_MAX_LEN)) return false;
    if (!pending_put(path, key, value)) return settings_csv_set(path, key, value);
    PENDING_ENTER();
    const bool start = !s_writer_running;
    s_writer_running = true;
    PENDING_EXIT();
    if (!start) return true;
    /* Below the screen (4) and the web server (3), so the write waits for
     * them rather than they for it. */
    if (xTaskCreatePinnedToCore(settings_writer, "settings_wr", SETTINGS_WRITER_STACK, NULL, 2,
                                NULL, tskNO_AFFINITY) != pdPASS) {
        PENDING_ENTER();
        s_writer_running = false;
        PENDING_EXIT();
        ESP_LOGW(TAG, "no internal RAM for the writer; writing %s now", key);
        (void)settings_csv_flush_pending();
    }
    return true;
}
#else
/* The host has no writer task: the write waits in the table until a test
 * flushes it, which is what lets a test see a value answered from the table
 * before it is on disk. */
bool settings_csv_set_later(const char *path, const char *key, const char *value)
{
    if (path == NULL || !valid_field(key, SETTINGS_CSV_KEY_MAX_LEN) ||
        !valid_field(value, SETTINGS_CSV_VALUE_MAX_LEN)) return false;
    if (!pending_put(path, key, value)) return settings_csv_set(path, key, value);
    return true;
}
#endif
