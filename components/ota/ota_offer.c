#include "ota_offer.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define OTA_OFFER_FORMAT_KNOWN 1

static bool copy_string(char *out, size_t size, const cJSON *item)
{
    if (!cJSON_IsString(item) || item->valuestring == NULL) return false;
    const size_t length = strlen(item->valuestring);
    if (length == 0U || length >= size) return false;
    memcpy(out, item->valuestring, length + 1U);
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_file(const cJSON *item, ota_offer_file_t *file)
{
    if (!cJSON_IsObject(item)) return false;
    if (!copy_string(file->url, sizeof(file->url), cJSON_GetObjectItemCaseSensitive(item, "url"))) {
        return false;
    }
    /* HTTPS to GitHub in a release; plain HTTP is allowed so a bench can
     * serve one from a laptop. Nothing else is a download. */
    if (strncmp(file->url, "https://", 8U) != 0 && strncmp(file->url, "http://", 7U) != 0) {
        return false;
    }
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(item, "size");
    if (!cJSON_IsNumber(size) || size->valuedouble < 1.0 || size->valuedouble > 16.0e6) {
        return false;
    }
    file->size = (size_t)size->valuedouble;
    const cJSON *digest = cJSON_GetObjectItemCaseSensitive(item, "sha256");
    if (!cJSON_IsString(digest) || digest->valuestring == NULL ||
        strlen(digest->valuestring) != 64U) {
        return false;
    }
    for (size_t index = 0U; index < 32U; ++index) {
        const int high = hex_value(digest->valuestring[2U * index]);
        const int low = hex_value(digest->valuestring[2U * index + 1U]);
        if (high < 0 || low < 0) return false;
        file->sha256[index] = (uint8_t)(high << 4 | low);
    }
    return true;
}

static char *dup_or_empty(const cJSON *item)
{
    const char *text = cJSON_IsString(item) && item->valuestring != NULL ? item->valuestring : "";
    const size_t length = strlen(text);
    char *copy = malloc(length + 1U);
    if (copy != NULL) memcpy(copy, text, length + 1U);
    return copy;
}

/* The change lists newer than `running`, in the manifest's order. An entry
 * that is not a version, or no newer, is left out; so is everything past
 * the table. */
static ota_offer_result_t read_history(const cJSON *history, const char *running,
                                       ota_offer_t *offer)
{
    if (!cJSON_IsArray(history)) return OTA_OFFER_MALFORMED;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, history) {
        if (offer->notes_count == OTA_OFFER_HISTORY_MAX) break;
        ota_offer_notes_t *notes = &offer->notes[offer->notes_count];
        if (!copy_string(notes->version, sizeof(notes->version),
                         cJSON_GetObjectItemCaseSensitive(entry, "version"))) {
            continue;
        }
        bool known = false;
        const int order = ota_version_compare(notes->version, running, &known);
        if (known && order <= 0) continue;
        notes->ru = dup_or_empty(cJSON_GetObjectItemCaseSensitive(entry, "ru"));
        notes->en = dup_or_empty(cJSON_GetObjectItemCaseSensitive(entry, "en"));
        ++offer->notes_count;
        if (notes->ru == NULL || notes->en == NULL) return OTA_OFFER_MEMORY;
    }
    return OTA_OFFER_OK;
}

ota_offer_result_t ota_offer_parse(const char *json, size_t length, const char *display,
                                   const char *running, ota_offer_t *offer)
{
    memset(offer, 0, sizeof(*offer));
    if (json == NULL || display == NULL) return OTA_OFFER_MALFORMED;
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (root == NULL) return OTA_OFFER_MALFORMED;
    ota_offer_result_t result = OTA_OFFER_OK;
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(root, "format");
    const cJSON *firmware = cJSON_GetObjectItemCaseSensitive(root, "firmware");
    if (!cJSON_IsNumber(format) || !cJSON_IsObject(firmware)) {
        result = OTA_OFFER_MALFORMED;
    } else if (format->valueint != OTA_OFFER_FORMAT_KNOWN) {
        result = OTA_OFFER_FORMAT;
    } else if (!copy_string(offer->version, sizeof(offer->version),
                            cJSON_GetObjectItemCaseSensitive(root, "version"))) {
        result = OTA_OFFER_MALFORMED;
    } else if (!cJSON_HasObjectItem(firmware, display)) {
        result = OTA_OFFER_NO_DISPLAY;
    } else if (!read_file(cJSON_GetObjectItemCaseSensitive(firmware, display), &offer->app) ||
               !read_file(cJSON_GetObjectItemCaseSensitive(root, "www"), &offer->www)) {
        result = OTA_OFFER_MALFORMED;
    } else {
        result = read_history(cJSON_GetObjectItemCaseSensitive(root, "history"),
                              running != NULL ? running : "", offer);
    }
    cJSON_Delete(root);
    if (result != OTA_OFFER_OK) ota_offer_free(offer);
    return result;
}

void ota_offer_free(ota_offer_t *offer)
{
    for (size_t index = 0U; index < offer->notes_count; ++index) {
        free(offer->notes[index].ru);
        free(offer->notes[index].en);
    }
    memset(offer, 0, sizeof(*offer));
}

typedef struct {
    unsigned long part[3];
    unsigned long ahead;  /* commits after the tag */
} version_t;

static bool read_number(const char **at, unsigned long *value)
{
    const char *p = *at;
    if (*p < '0' || *p > '9') return false;
    unsigned long result = 0UL;
    while (*p >= '0' && *p <= '9') {
        if (result > 1000000UL) return false;
        result = result * 10UL + (unsigned long)(*p - '0');
        ++p;
    }
    *value = result;
    *at = p;
    return true;
}

static bool parse_version(const char *text, version_t *version)
{
    memset(version, 0, sizeof(*version));
    if (text == NULL || text[0] != 'v') return false;
    const char *p = text + 1;
    for (int index = 0; index < 3; ++index) {
        if (index > 0 && *p++ != '.') return false;
        if (!read_number(&p, &version->part[index])) return false;
    }
    /* -N-gHASH: N commits past the tag. -dirty says nothing about order. */
    if (p[0] == '-' && p[1] >= '0' && p[1] <= '9') {
        ++p;
        if (!read_number(&p, &version->ahead) || strncmp(p, "-g", 2U) != 0) return false;
    }
    return true;
}

int ota_version_compare(const char *left, const char *right, bool *known)
{
    version_t a;
    version_t b;
    const bool both = parse_version(left, &a) && parse_version(right, &b);
    if (known != NULL) *known = both;
    if (!both) return 0;
    for (int index = 0; index < 3; ++index) {
        if (a.part[index] != b.part[index]) return a.part[index] < b.part[index] ? -1 : 1;
    }
    if (a.ahead != b.ahead) return a.ahead < b.ahead ? -1 : 1;
    return 0;
}

bool ota_offer_is_wanted(const char *running, const char *latest, const char *skipped)
{
    if (latest == NULL || latest[0] == '\0') return false;
    if (skipped != NULL && strcmp(skipped, latest) == 0) return false;
    version_t ignored;
    if (!parse_version(latest, &ignored)) return false;
    bool known = false;
    const int order = ota_version_compare(latest, running, &known);
    return known ? order > 0 : true;
}

const char *ota_offer_result_code(ota_offer_result_t result)
{
    switch (result) {
    case OTA_OFFER_OK: return "ok";
    case OTA_OFFER_MALFORMED: return "malformed";
    case OTA_OFFER_FORMAT: return "format";
    case OTA_OFFER_NO_DISPLAY: return "no_display";
    case OTA_OFFER_MEMORY: return "memory";
    }
    return "malformed";
}
