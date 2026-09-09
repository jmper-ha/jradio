#pragma once

#include <stddef.h>

/* The time zones the device offers, as a table rather than a tzdata database.
 *
 * A POSIX TZ string is what the C library actually wants - "MSK-3", or
 * "CET-1CEST,M3.5.0,M10.5.0/3" for a zone that shifts twice a year - and it is
 * not something to ask anybody to type: the sign is inverted from what people
 * mean by UTC+3, and the DST rules are a small language of their own. So the
 * setting is a choice out of this list, and the string stays in the firmware.
 *
 * It also has to be, because settings.csv splits a line on its first comma and
 * every DST rule has two of them. What is stored is the id below.
 *
 * The list is Russia's zones, which is where this device lives, plus a few
 * that show the two shapes a rule can take. Adding one is a line here and
 * nothing else: the browser is handed the list rather than carrying its own
 * copy, so the two cannot drift. */

#include "device_language.h"

#define DEVICE_TIMEZONE_ID_MAX 24
#define DEVICE_TIMEZONE_LABEL_MAX 48
/* What the device came up with before the setting existed, and what it stays
 * on for anyone who never opens the page. */
#define DEVICE_TIMEZONE_DEFAULT_ID "europe/moscow"

typedef struct {
    /* Stored in settings.csv and sent to the browser. Lower case, ASCII, no
     * comma - the file's separator - and no tab. */
    const char *id;
    /* What the page shows, in each language. A city name is not a translation
     * exercise - "Москва" and "Moscow" are the same place spelled for two
     * readers - but a page that says one while everything around it says the
     * other reads as half-finished, which is what this whole pass was
     * about. */
    const char *label_ru;
    const char *label_en;
    /* What setenv("TZ", …) is given. */
    const char *posix;
} device_timezone_t;

size_t device_timezone_count(void);
/* NULL past the end. */
const device_timezone_t *device_timezone_at(size_t index);
/* NULL for an id this firmware does not have - an older settings.csv, or a
 * page from a newer build. The caller falls back to the default rather than
 * setting a zone nobody chose. */
const device_timezone_t *device_timezone_find(const char *id);
/* Where an id sits in the list, or the count when it is not in it. Wanted by
 * the web view, which carries the choice as one byte rather than a string. */
size_t device_timezone_index_of(const char *id);

/* The label in the language in force. Never NULL, so a caller has nothing to
 * check before writing it into a document. */
const char *device_timezone_label(const device_timezone_t *zone,
                                  device_language_t language);
