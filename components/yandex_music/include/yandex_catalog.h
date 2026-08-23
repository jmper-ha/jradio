#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The stations the device offers under Yandex Music. Deliberately the personal
 * dashboard and not the full catalogue: /rotor/stations/list is 696 entries
 * and 2.4 MB, which is minutes over TLS and unreadable on an encoder, while
 * the dashboard is a handful of personalised stations in 13 KB - and it is the
 * only place the personal wave appears at all. */

#define YANDEX_STATION_ID_MAX 47
/* Long enough for the longest name in the catalogue, which is 37 Cyrillic
 * characters - and those are two bytes each in UTF-8. */
#define YANDEX_STATION_NAME_MAX 95
#define YANDEX_STATION_FROM_MAX 47
#define YANDEX_CATALOG_MAX_STATIONS 12

typedef struct {
    /* "user:onyourwave", "genre:jazz" - the form the rotor endpoints take. */
    char id[YANDEX_STATION_ID_MAX + 1];
    char name[YANDEX_STATION_NAME_MAX + 1];
    /* idForFrom, "user-onyourwave". The same identity with dashes, which is
     * what the feedback endpoint wants as `from`; kept now so that playback
     * does not have to fetch the dashboard again to find it. */
    char from[YANDEX_STATION_FROM_MAX + 1];
} yandex_station_t;

typedef struct {
    yandex_station_t stations[YANDEX_CATALOG_MAX_STATIONS];
    uint8_t count;
} yandex_catalog_t;

/* Reads the dashboard answer, whole or unwrapped. Allocates nothing: the
 * answer is 13 KB of deeply nested JSON, and in this build every allocation
 * under a kilobyte lands in internal RAM - a cJSON tree of it would take tens
 * of kilobytes of the scarcest memory on the board, transiently, possibly
 * while a stream is playing.
 *
 * Structure-aware rather than a search for the keys: each station carries its
 * settings inline, and those contain more than twenty "name" fields of their
 * own ("По языку", "Бодрее"), so anything that merely scans for a key finds
 * the wrong one. */
bool yandex_catalog_parse_dashboard(const char *json, yandex_catalog_t *catalog);

/* True when the two hold the same stations in the same order, so a refresh
 * that changed nothing does not have to disturb a screen. */
bool yandex_catalog_equal(const yandex_catalog_t *left, const yandex_catalog_t *right);

typedef enum {
    YANDEX_CATALOG_EMPTY = 0, /* never fetched, or the account was unlinked */
    YANDEX_CATALOG_LOADING,
    YANDEX_CATALOG_READY,
    YANDEX_CATALOG_FAILED,
} yandex_catalog_state_t;

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t yandex_catalog_init(void);

/* Starts a fetch and returns at once; the screen follows it through
 * yandex_catalog_get_state(). Asking again while one is running is harmless
 * and does nothing, because the screen and the web UI can both ask.
 *
 * Asynchronous because the fetch performs a TLS handshake and reads 14 KB,
 * which is far too much to do from the UI poll loop or from player_control -
 * the task with the least stack headroom in the system. */
esp_err_t yandex_catalog_request_refresh(void);
yandex_catalog_state_t yandex_catalog_get_state(void);

/* Empties the table, for when the account is unlinked: leaving someone else's
 * stations on the screen after a logout would be a small betrayal. */
void yandex_catalog_clear(void);

/* Copies the station at `index`; false when there is no such row. Copies
 * rather than lends a pointer: a refresh can replace the table between the
 * screen reading a row and drawing it. */
bool yandex_catalog_station_at(size_t index, yandex_station_t *station);
size_t yandex_catalog_count(void);

/* Changes whenever the table is replaced with different contents, so a screen
 * can tell "same list" from "a new list that happens to be as long". */
unsigned int yandex_catalog_revision(void);
#endif
