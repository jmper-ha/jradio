#include "device_timezone.h"

#include <string.h>

/* Russia first and in order of offset, then the two rules that shift, then the
 * one that does neither. A list on a page is read top to bottom, and somebody
 * looking for their own city should not have to hunt for it among the world's.
 *
 * The abbreviations are the ones the zones are actually called - the C library
 * only ever prints them back, so what matters is that they are three letters
 * and not a lie. The digit after them is inverted on purpose: POSIX counts how
 * far a zone is *behind* UTC, so Moscow at UTC+3 is written "-3". */
static const device_timezone_t k_zones[] = {
    {"europe/kaliningrad", "Калининград (UTC+2)", "Kaliningrad (UTC+2)", "EET-2"},
    {"europe/moscow", "Москва (UTC+3)", "Moscow (UTC+3)", "MSK-3"},
    {"europe/samara", "Самара (UTC+4)", "Samara (UTC+4)", "SAMT-4"},
    {"asia/yekaterinburg", "Екатеринбург (UTC+5)", "Yekaterinburg (UTC+5)", "YEKT-5"},
    {"asia/omsk", "Омск (UTC+6)", "Omsk (UTC+6)", "OMST-6"},
    {"asia/krasnoyarsk", "Красноярск (UTC+7)", "Krasnoyarsk (UTC+7)", "KRAT-7"},
    {"asia/irkutsk", "Иркутск (UTC+8)", "Irkutsk (UTC+8)", "IRKT-8"},
    {"asia/yakutsk", "Якутск (UTC+9)", "Yakutsk (UTC+9)", "YAKT-9"},
    {"asia/vladivostok", "Владивосток (UTC+10)", "Vladivostok (UTC+10)", "VLAT-10"},
    {"asia/magadan", "Магадан (UTC+11)", "Magadan (UTC+11)", "MAGT-11"},
    {"asia/kamchatka", "Камчатка (UTC+12)", "Kamchatka (UTC+12)", "PETT-12"},
    /* The two that move. Written out in full because a summer-time rule is
     * exactly what a table like this exists to keep out of the settings
     * file. */
    {"europe/london", "Лондон (UTC+0/+1)", "London (UTC+0/+1)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"europe/berlin", "Берлин (UTC+1/+2)", "Berlin (UTC+1/+2)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"america/new_york", "Нью-Йорк (UTC-5/-4)", "New York (UTC-5/-4)", "EST5EDT,M3.2.0,M11.1.0"},
    {"utc", "UTC (UTC+0)", "UTC (UTC+0)", "UTC0"},
};

size_t device_timezone_count(void)
{
    return sizeof(k_zones) / sizeof(k_zones[0]);
}

const device_timezone_t *device_timezone_at(size_t index)
{
    return index < device_timezone_count() ? &k_zones[index] : NULL;
}

const device_timezone_t *device_timezone_find(const char *id)
{
    if (id == NULL) return NULL;
    for (size_t index = 0U; index < device_timezone_count(); ++index) {
        if (strcmp(k_zones[index].id, id) == 0) return &k_zones[index];
    }
    return NULL;
}

const char *device_timezone_label(const device_timezone_t *zone,
                                  device_language_t language)
{
    if (zone == NULL) return "";
    return language == DEVICE_LANGUAGE_EN ? zone->label_en : zone->label_ru;
}

size_t device_timezone_index_of(const char *id)
{
    if (id == NULL) return device_timezone_count();
    for (size_t index = 0U; index < device_timezone_count(); ++index) {
        if (strcmp(k_zones[index].id, id) == 0) return index;
    }
    return device_timezone_count();
}
