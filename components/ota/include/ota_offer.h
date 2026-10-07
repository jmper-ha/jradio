#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What a release offers this radio: ota.json as tools/ota_manifest.py writes
 * it, cut down to the firmware for one display and the web files. Pure, so the
 * host tests read real and broken manifests; the fetching is ota_check.c's. */

#define OTA_OFFER_VERSION_MAX 32U
#define OTA_OFFER_URL_MAX 192U

typedef struct {
    char url[OTA_OFFER_URL_MAX];
    size_t size;
    uint8_t sha256[32];
} ota_offer_file_t;

typedef struct {
    char version[OTA_OFFER_VERSION_MAX];
    /* malloc'd, possibly empty, freed by ota_offer_free(). */
    char *notes_ru;
    char *notes_en;
    ota_offer_file_t app;
    ota_offer_file_t www;
} ota_offer_t;

typedef enum {
    OTA_OFFER_OK = 0,
    OTA_OFFER_MALFORMED,
    /* A manifest from a later format than this firmware reads. */
    OTA_OFFER_FORMAT,
    /* The release has no firmware for this display. */
    OTA_OFFER_NO_DISPLAY,
    OTA_OFFER_MEMORY,
} ota_offer_result_t;

ota_offer_result_t ota_offer_parse(const char *json, size_t length, const char *display,
                                   ota_offer_t *offer);
void ota_offer_free(ota_offer_t *offer);

/* Orders two versions as git describe writes them - v1.5.5, v1.5.5-4-gabc1234,
 * either with -dirty. A build between two releases sorts after the one it
 * starts from and before the next. Returns <0, 0 or >0 like strcmp; *known is
 * false when either is not a version at all (a bare hash: no tags fetched),
 * and then the answer is 0. */
int ota_version_compare(const char *left, const char *right, bool *known);

/* Whether to offer `latest` to a radio running `running`: newer, and not the
 * one the user said to skip. A radio whose own version is unknown is offered
 * any release - it is from no release at all. */
bool ota_offer_is_wanted(const char *running, const char *latest, const char *skipped);

const char *ota_offer_result_code(ota_offer_result_t result);

#ifdef __cplusplus
}
#endif
