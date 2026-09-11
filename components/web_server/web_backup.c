#include "web_backup.h"

#ifdef ESP_PLATFORM

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "miniz.h"

#include "config_archive.h"
#include "wifi_settings.h"
#include "yandex_token_store.h"

#define WEB_BACKUP_CONFIG_DIR "/littlefs/config"
/* One file at a time is held in RAM. wifi.json is the largest of the four at
 * about 3.3 KB with five networks, so this is headroom rather than a limit
 * anything real approaches - it is here to stop an upload, not a backup. */
#define WEB_BACKUP_MEMBER_MAX_LEN 8192U
#define WEB_BACKUP_UPLOAD_MAX_LEN \
    CONFIG_ARCHIVE_CAPACITY(CONFIG_ARCHIVE_MEMBER_MAX * WEB_BACKUP_MEMBER_MAX_LEN)
/* Long enough for the directory and the longest member name. */
#define WEB_BACKUP_PATH_MAX 64
/* Enough for the answer with all four names in both lists. */
#define WEB_BACKUP_REPLY_MAX 256
/* The browser has to receive the answer before the device goes away, and the
 * socket is closed by the restart, not by a handshake. */
#define WEB_BACKUP_REBOOT_DELAY_MS 700

static const char *TAG = "web_backup";

static void web_backup_secure_zero(void *memory, size_t size)
{
    volatile unsigned char *bytes = memory;
    while (size-- > 0U) {
        *bytes++ = 0U;
    }
}

static void web_backup_path(char *path, size_t size, config_archive_member_t member)
{
    snprintf(path, size, WEB_BACKUP_CONFIG_DIR "/%s", config_archive_member_file(member));
}

/* Bytes read, or 0 for "no such file" as well as for "empty" - which are the
 * same thing to a backup: a device that was never linked to Yandex has no
 * yandex.json, and neither case belongs in the archive. */
static size_t web_backup_read_member(config_archive_member_t member, void *buffer, size_t capacity)
{
    char path[WEB_BACKUP_PATH_MAX];
    web_backup_path(path, sizeof(path), member);
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0U;
    const size_t read = fread(buffer, 1U, capacity, file);
    /* A file that fills the buffer exactly may have more behind it, and half a
     * configuration in a backup is worse than none: it restores silently. */
    const bool truncated = read == capacity && fgetc(file) != EOF;
    fclose(file);
    if (truncated) {
        ESP_LOGE(TAG, "%s is larger than %u bytes and was left out of the backup", path,
                 (unsigned)capacity);
        return 0U;
    }
    return read;
}

static void web_backup_stamp(uint16_t *date, uint16_t *time_of_day)
{
    /* No RTC: before SNTP has answered the year is 1970, which a zip entry
     * cannot express, and config_archive_dos_date() answers with the 1980 floor
     * every reader accepts. */
    const time_t now = time(NULL);
    struct tm local;
    if (localtime_r(&now, &local) == NULL) {
        *date = CONFIG_ARCHIVE_DOS_DATE_MIN;
        *time_of_day = 0U;
        return;
    }
    *date = config_archive_dos_date(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    *time_of_day = config_archive_dos_time(local.tm_hour, local.tm_min, local.tm_sec);
}

static void web_backup_filename(char *name, size_t size)
{
    const time_t now = time(NULL);
    struct tm local;
    if (localtime_r(&now, &local) != NULL && local.tm_year + 1900 >= 2020) {
        snprintf(name, size, "attachment; filename=\"jradio-%04d%02d%02d.zip\"",
                 local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
        return;
    }
    /* Without a clock every backup would be called the same thing, so the date
     * is left off rather than made up: two files called jradio-19800101.zip
     * are harder to tell apart than two called jradio-backup.zip. */
    snprintf(name, size, "attachment; filename=\"jradio-backup.zip\"");
}

esp_err_t web_backup_get(httpd_req_t *request)
{
    uint8_t *archive = malloc(WEB_BACKUP_UPLOAD_MAX_LEN);
    uint8_t *member = malloc(WEB_BACKUP_MEMBER_MAX_LEN);
    if (archive == NULL || member == NULL) {
        free(archive);
        free(member);
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    uint16_t date = CONFIG_ARCHIVE_DOS_DATE_MIN;
    uint16_t time_of_day = 0U;
    web_backup_stamp(&date, &time_of_day);
    config_archive_writer_t writer;
    config_archive_writer_init(&writer, archive, WEB_BACKUP_UPLOAD_MAX_LEN, date, time_of_day);
    for (config_archive_member_t kind = CONFIG_ARCHIVE_MEMBER_FIRST;
         kind <= CONFIG_ARCHIVE_MEMBER_LAST; ++kind) {
        const size_t size = web_backup_read_member(kind, member, WEB_BACKUP_MEMBER_MAX_LEN);
        if (size == 0U) continue;
        if (!config_archive_writer_add(&writer, config_archive_member_file(kind), member, size)) {
            ESP_LOGE(TAG, "%s did not fit the archive", config_archive_member_file(kind));
            break;
        }
    }

    size_t length = 0U;
    const bool empty = writer.count == 0U;
    const bool complete = !empty && config_archive_writer_finish(&writer, &length);
    web_backup_secure_zero(member, WEB_BACKUP_MEMBER_MAX_LEN);
    free(member);
    if (!complete) {
        web_backup_secure_zero(archive, WEB_BACKUP_UPLOAD_MAX_LEN);
        free(archive);
        if (empty) {
            /* Not an error worth a 500: a device that has never been set up
             * has nothing to hand out, and saying so is the honest answer. */
            ESP_LOGW(TAG, "no configuration to back up");
            httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Nothing to back up");
        } else {
            /* Told apart from the empty case on purpose: this one means the
             * files outgrew a buffer that is derived from their own caps, so
             * it is a bug here, not a device with nothing saved. */
            ESP_LOGE(TAG, "the archive did not fit %u bytes",
                     (unsigned)WEB_BACKUP_UPLOAD_MAX_LEN);
            httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Cannot build the archive");
        }
        return ESP_FAIL;
    }

    char disposition[WEB_BACKUP_PATH_MAX];
    web_backup_filename(disposition, sizeof(disposition));
    httpd_resp_set_type(request, "application/zip");
    httpd_resp_set_hdr(request, "Content-Disposition", disposition);
    /* The Wi-Fi password is in here. Nothing between the device and the
     * browser should keep a copy, and the file changes whenever any setting
     * does, so a cached one would be wrong as well as private. */
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t sent = httpd_resp_send(request, (const char *)archive, length);
    web_backup_secure_zero(archive, WEB_BACKUP_UPLOAD_MAX_LEN);
    free(archive);
    ESP_LOGI(TAG, "backup sent: %u files, %u bytes", (unsigned)writer.count, (unsigned)length);
    return sent;
}

/* One file on its way to the flash. `owned` is the inflated copy, which has to
 * be freed whether the restore goes through or is refused halfway. */
typedef struct {
    config_archive_member_t member;
    const uint8_t *data;
    size_t size;
    uint8_t *owned;
} web_backup_pending_t;

static void web_backup_release(web_backup_pending_t *pending, size_t count)
{
    for (size_t index = 0U; index < count; ++index) {
        if (pending[index].owned != NULL) {
            web_backup_secure_zero(pending[index].owned, pending[index].size);
            free(pending[index].owned);
            pending[index].owned = NULL;
        }
    }
}

static uint8_t *web_backup_inflate(const config_archive_entry_t *entry)
{
    if (entry->original_size == 0U || entry->original_size > WEB_BACKUP_MEMBER_MAX_LEN) {
        return NULL;
    }
    uint8_t *out = malloc(entry->original_size);
    /* On the heap, not the stack, and not through the ROM's mem_to_mem
     * helper either: the decompressor is eleven kilobytes of tables and that
     * helper keeps it as a local, which the HTTP worker's six-kilobyte stack
     * answers with StoreProhibited on the first deflated archive. */
    tinfl_decompressor *decompressor = malloc(sizeof(*decompressor));
    if (out == NULL || decompressor == NULL) {
        free(out);
        free(decompressor);
        return NULL;
    }
    tinfl_init(decompressor);
    size_t consumed = entry->size;
    size_t produced = entry->original_size;
    /* The whole stream is in memory and the whole file fits the buffer, so
     * this is one call with no window of its own - and the size it is allowed
     * to produce is the one the archive declared, which is what keeps an
     * upload from expanding without limit. */
    const tinfl_status status =
        tinfl_decompress(decompressor, entry->data, &consumed, out, out, &produced,
                         TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(decompressor);
    if (status != TINFL_STATUS_DONE || produced != entry->original_size ||
        config_archive_crc32(out, produced) != entry->crc) {
        web_backup_secure_zero(out, entry->original_size);
        free(out);
        return NULL;
    }
    return out;
}

static bool web_backup_write_member(config_archive_member_t member, const void *data, size_t size)
{
    char path[WEB_BACKUP_PATH_MAX];
    char temp[WEB_BACKUP_PATH_MAX];
    web_backup_path(path, sizeof(path), member);
    snprintf(temp, sizeof(temp), WEB_BACKUP_CONFIG_DIR "/restore.tmp");
    /* Written beside the file and renamed over it, the way every other writer
     * of these files does: a power cut in the middle then leaves the old
     * configuration intact instead of half of the new one. */
    FILE *file = fopen(temp, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "cannot open %s: %s", temp, strerror(errno));
        return false;
    }
    const size_t written = fwrite(data, 1U, size, file);
    const bool flushed = fflush(file) == 0;
    fclose(file);
    if (written != size || !flushed) {
        ESP_LOGE(TAG, "cannot write %s: %s", temp, strerror(errno));
        unlink(temp);
        return false;
    }
    if (rename(temp, path) != 0) {
        ESP_LOGE(TAG, "cannot replace %s: %s", path, strerror(errno));
        unlink(temp);
        return false;
    }
    return true;
}

/* Reads the file back through the component that owns it. A file that passed
 * the shape check can still be rejected by the loader - a token with a
 * character it will not accept, a networks array that is empty - and that only
 * shows up after the reboot, as a device on the setup access point with no
 * explanation. Saying it here costs one read. */
static bool web_backup_member_loads(config_archive_member_t member)
{
    if (member == CONFIG_ARCHIVE_MEMBER_WIFI) {
        wifi_settings_t *settings = malloc(sizeof(*settings));
        if (settings == NULL) return true;
        const bool loaded =
            wifi_settings_load(settings) == ESP_OK && settings->count > 0U;
        web_backup_secure_zero(settings, sizeof(*settings));
        free(settings);
        return loaded;
    }
    if (member == CONFIG_ARCHIVE_MEMBER_YANDEX) {
        yandex_token_t *token = malloc(sizeof(*token));
        if (token == NULL) return true;
        const bool loaded =
            yandex_token_store_load(token) == ESP_OK && token->token[0] != '\0';
        web_backup_secure_zero(token, sizeof(*token));
        free(token);
        return loaded;
    }
    /* settings.csv has no loader that can fail: every key it does not
     * understand is skipped and every key it wants has a default. weather.json
     * degrades the same way - a key it cannot read is a key it has not got,
     * and the page says so. */
    return true;
}

static void web_backup_append(char *reply, size_t size, size_t *length, const char *text)
{
    const size_t remaining = size - *length;
    const int written = snprintf(reply + *length, remaining, "%s", text);
    if (written > 0 && (size_t)written < remaining) *length += (size_t)written;
}

/* Refusals answer with a code rather than a sentence: the page says it in
 * Russian, and matching on an English sentence is how a message reworded in a
 * log turns into a browser showing nothing at all. */
static esp_err_t web_backup_fail(httpd_req_t *request, const char *status, const char *code,
                                 const char *reason)
{
    ESP_LOGW(TAG, "restore refused (%s): %s", code, reason);
    char body[64];
    const int length = snprintf(body, sizeof(body), "{\"error\":\"%s\"}", code);
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    /* The answer is complete, so the connection may stay open: returning
     * ESP_FAIL here would close it and cost the page its next request. */
    return httpd_resp_send(request, body, length > 0 ? (size_t)length : 0U);
}

static esp_err_t web_backup_reject(httpd_req_t *request, const char *code, const char *reason)
{
    return web_backup_fail(request, "400 Bad Request", code, reason);
}

esp_err_t web_backup_restore_post(httpd_req_t *request)
{
    if (request->content_len <= 0 ||
        (size_t)request->content_len > WEB_BACKUP_UPLOAD_MAX_LEN) {
        return web_backup_reject(request, "size", "upload is empty or larger than the cap");
    }
    /* The name the browser sends is only ever used to recognise a single file:
     * an archive says what is in it. */
    char query[WEB_BACKUP_PATH_MAX + 16U];
    char name[WEB_BACKUP_PATH_MAX];
    name[0] = '\0';
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        name[0] = '\0';
    }

    uint8_t *body = malloc((size_t)request->content_len);
    if (body == NULL) {
        return web_backup_fail(request, "500 Internal Server Error", "memory",
                               "no room for the upload");
    }
    size_t received = 0U;
    while (received < (size_t)request->content_len) {
        const int read = httpd_req_recv(request, (char *)body + received,
                                        (size_t)request->content_len - received);
        if (read <= 0) {
            web_backup_secure_zero(body, (size_t)request->content_len);
            free(body);
            return web_backup_reject(request, "incomplete", "the upload stopped early");
        }
        received += (size_t)read;
    }

    web_backup_pending_t pending[CONFIG_ARCHIVE_MEMBER_MAX] = {0};
    size_t count = 0U;
    const char *refusal = NULL;
    const char *refusal_reason = NULL;

    if (config_archive_looks_like_zip(body, received)) {
        config_archive_reader_t reader;
        config_archive_reader_init(&reader, body, received);
        for (;;) {
            config_archive_entry_t entry;
            const config_archive_read_t status = config_archive_reader_next(&reader, &entry);
            if (status == CONFIG_ARCHIVE_READ_END) break;
            if (status == CONFIG_ARCHIVE_READ_COMPRESSED) {
                refusal = "compressed";
                refusal_reason = "packed with neither stored nor deflate";
                break;
            }
            if (status != CONFIG_ARCHIVE_READ_OK) {
                refusal = status == CONFIG_ARCHIVE_READ_CRC ? "damaged" : "malformed";
                refusal_reason = "the archive could not be walked";
                break;
            }
            const config_archive_member_t member = config_archive_member_from_file(entry.name);
            if (member == CONFIG_ARCHIVE_MEMBER_UNKNOWN) continue;
            /* An archive that names the same file twice is somebody's edit, not
             * something this device wrote; the first copy wins so that the
             * result does not depend on the order a tool happened to pack. */
            bool duplicate = false;
            for (size_t index = 0U; index < count; ++index) {
                if (pending[index].member == member) duplicate = true;
            }
            if (duplicate || count >= CONFIG_ARCHIVE_MEMBER_MAX) continue;
            pending[count].member = member;
            if (entry.deflated) {
                pending[count].owned = web_backup_inflate(&entry);
                if (pending[count].owned == NULL) {
                    refusal = "damaged";
                    refusal_reason = "an entry did not inflate to what it declared";
                    break;
                }
                pending[count].data = pending[count].owned;
                pending[count].size = entry.original_size;
            } else {
                pending[count].data = entry.data;
                pending[count].size = entry.size;
            }
            ++count;
        }
        if (refusal == NULL && count == 0U) {
            refusal = "empty";
            refusal_reason = "no wifi.json, settings.csv, yandex.json or weather.json inside";
        }
    } else {
        const config_archive_member_t member = config_archive_member_from_file(name);
        if (member == CONFIG_ARCHIVE_MEMBER_UNKNOWN) {
            refusal = "unknown-file";
            refusal_reason = "neither an archive nor a file this device keeps";
        } else {
            pending[0].member = member;
            pending[0].data = body;
            pending[0].size = received;
            count = 1U;
        }
    }

    /* Every file is checked before any file is written: a restore that stops
     * halfway leaves the device with somebody else's networks and its own
     * token, which is a state neither backup describes. */
    for (size_t index = 0U; refusal == NULL && index < count; ++index) {
        if (pending[index].size > WEB_BACKUP_MEMBER_MAX_LEN) {
            refusal = "size";
            refusal_reason = "a file inside is larger than the cap";
        } else if (!config_archive_member_is_plausible(pending[index].member, pending[index].data,
                                                       pending[index].size)) {
            refusal = "contents";
            refusal_reason = config_archive_member_file(pending[index].member);
        }
    }

    if (refusal != NULL) {
        web_backup_release(pending, count);
        web_backup_secure_zero(body, received);
        free(body);
        return web_backup_reject(request, refusal, refusal_reason);
    }

    char reply[WEB_BACKUP_REPLY_MAX];
    size_t length = 0U;
    web_backup_append(reply, sizeof(reply), &length, "{\"restored\":[");
    char warnings[WEB_BACKUP_REPLY_MAX];
    size_t warnings_length = 0U;
    warnings[0] = '\0';
    size_t written = 0U;
    for (size_t index = 0U; index < count; ++index) {
        const char *file = config_archive_member_file(pending[index].member);
        if (!web_backup_write_member(pending[index].member, pending[index].data,
                                     pending[index].size)) {
            web_backup_release(pending, count);
            web_backup_secure_zero(body, received);
            free(body);
            return web_backup_fail(request, "500 Internal Server Error", "write", file);
        }
        if (written > 0U) web_backup_append(reply, sizeof(reply), &length, ",");
        web_backup_append(reply, sizeof(reply), &length, "\"");
        web_backup_append(reply, sizeof(reply), &length, file);
        web_backup_append(reply, sizeof(reply), &length, "\"");
        ++written;
        if (!web_backup_member_loads(pending[index].member)) {
            ESP_LOGW(TAG, "%s was restored but the device cannot read it back", file);
            if (warnings_length > 0U) {
                web_backup_append(warnings, sizeof(warnings), &warnings_length, ",");
            }
            web_backup_append(warnings, sizeof(warnings), &warnings_length, "\"");
            web_backup_append(warnings, sizeof(warnings), &warnings_length, file);
            web_backup_append(warnings, sizeof(warnings), &warnings_length, "\"");
        }
        ESP_LOGI(TAG, "restored %s, %u bytes", file, (unsigned)pending[index].size);
    }
    web_backup_append(reply, sizeof(reply), &length, "],\"warnings\":[");
    web_backup_append(reply, sizeof(reply), &length, warnings);
    web_backup_append(reply, sizeof(reply), &length, "],\"reboot\":true}");

    web_backup_release(pending, count);
    web_backup_secure_zero(body, received);
    free(body);

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t sent = httpd_resp_send(request, reply, length);

    /* Restarting rather than reloading each component in place: the settings
     * are cached by three tasks, the Wi-Fi list is read once at boot, and the
     * token is held by the Yandex client. A reboot is the one path where all
     * of that is guaranteed to come from the files that were just written. */
    ESP_LOGW(TAG, "restarting to apply the restored configuration");
    vTaskDelay(pdMS_TO_TICKS(WEB_BACKUP_REBOOT_DELAY_MS));
    esp_restart();
    return sent;
}

#endif
