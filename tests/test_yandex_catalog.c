#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "yandex_catalog.h"

/* Shaped like the real answer, including the part that makes a naive reader
 * fail: every station carries its settings inline, and those hold more than
 * twenty "name" fields of their own. Trimmed from a capture taken 2026-08-23. */
static const char *const DASHBOARD =
    "{\"invocationInfo\":{\"hostname\":\"music-web-mobile.yandex.net\"},"
    "\"result\":{\"dashboardId\":\"1787490817396467-343887\",\"stations\":["
    "{\"station\":{\"id\":{\"type\":\"user\",\"tag\":\"onyourwave\"},"
    "\"name\":\"Моя волна\","
    "\"icon\":{\"backgroundColor\":\"#2AA75B\",\"imageUrl\":\"avatars/%%\"},"
    "\"idForFrom\":\"user-onyourwave\","
    "\"restrictions\":{\"language\":{\"name\":\"По языку\",\"type\":\"enum\","
    "\"possibleValues\":[{\"name\":\"Русский\",\"value\":\"russian\"},"
    "{\"name\":\"Иностранный\",\"value\":\"not-russian\"}]},"
    "\"mood\":{\"name\":\"Под настроение\",\"min\":{\"name\":\"Грустнее\",\"value\":0.0},"
    "\"max\":{\"name\":\"Веселее\",\"value\":1.0}}}},"
    "\"settings\":{\"language\":\"any\"},\"adParams\":{\"partnerId\":\"1\"}},"
    "{\"station\":{\"id\":{\"type\":\"micro-genre\",\"tag\":\"jump-blues\"},"
    "\"name\":\"Джамп-блюз\",\"idForFrom\":\"micro-genre-jumpblues\","
    "\"restrictions2\":{\"diversity\":{\"name\":\"По характеру\"}}},"
    "\"settings2\":{\"diversity\":\"default\"}},"
    "{\"station\":{\"id\":{\"type\":\"genre\",\"tag\":\"jazz\"},\"name\":\"Джаз\","
    "\"idForFrom\":\"genre-jazz\"},\"rupTitle\":\"Джаз\"}"
    "],\"pumpkin\":false}}";

static void test_the_real_shape_yields_exactly_the_stations(void)
{
    yandex_catalog_t catalog;
    assert(yandex_catalog_parse_dashboard(DASHBOARD, &catalog));
    assert(catalog.count == 3U);

    assert(strcmp(catalog.stations[0].id, "user:onyourwave") == 0);
    assert(strcmp(catalog.stations[0].name, "Моя волна") == 0);
    assert(strcmp(catalog.stations[0].from, "user-onyourwave") == 0);
    assert(strcmp(catalog.stations[1].id, "micro-genre:jump-blues") == 0);
    assert(strcmp(catalog.stations[1].name, "Джамп-блюз") == 0);
    assert(strcmp(catalog.stations[2].id, "genre:jazz") == 0);
    assert(strcmp(catalog.stations[2].name, "Джаз") == 0);
}

static void test_the_settings_inside_a_station_do_not_leak_into_it(void)
{
    /* The whole reason this parser tracks structure: "По языку" and "Грустнее"
     * are `name` fields too, one level down. A reader that searched for the
     * key would have named the first station "По языку". */
    yandex_catalog_t catalog;
    assert(yandex_catalog_parse_dashboard(DASHBOARD, &catalog));
    for (uint8_t index = 0U; index < catalog.count; ++index) {
        assert(strstr(catalog.stations[index].name, "языку") == NULL);
        assert(strstr(catalog.stations[index].name, "настроение") == NULL);
        assert(strstr(catalog.stations[index].name, "характеру") == NULL);
    }
}

static void test_the_envelope_is_optional(void)
{
    /* The device gets the whole answer; a fixture or a future caller may hold
     * only the payload. */
    yandex_catalog_t catalog;
    assert(yandex_catalog_parse_dashboard(
        "{\"stations\":[{\"station\":{\"id\":{\"type\":\"genre\",\"tag\":\"jazz\"},"
        "\"name\":\"Джаз\"}}]}",
        &catalog));
    assert(catalog.count == 1U);
    assert(strcmp(catalog.stations[0].id, "genre:jazz") == 0);
    /* idForFrom is optional in this shape; playback can still address the
     * station by id. */
    assert(catalog.stations[0].from[0] == '\0');
}

static void test_one_broken_station_does_not_cost_the_others(void)
{
    yandex_catalog_t catalog;
    assert(yandex_catalog_parse_dashboard(
        "{\"stations\":["
        "{\"station\":{\"id\":{\"type\":\"genre\"},\"name\":\"Без тега\"}},"
        "{\"nothing\":1},"
        "{\"station\":{\"id\":{\"type\":\"genre\",\"tag\":\"jazz\"},\"name\":\"Джаз\"}}"
        "]}",
        &catalog));
    assert(catalog.count == 1U);
    assert(strcmp(catalog.stations[0].name, "Джаз") == 0);
}

static void test_an_overlong_field_drops_that_station_not_a_truncated_one(void)
{
    /* A clipped id addresses a station the rotor has never heard of, and the
     * failure would only appear when playback was attempted. */
    char json[YANDEX_STATION_NAME_MAX + 256];
    char long_name[YANDEX_STATION_NAME_MAX + 2];
    memset(long_name, 'a', sizeof(long_name) - 1U);
    long_name[sizeof(long_name) - 1U] = '\0';
    snprintf(json, sizeof(json),
             "{\"stations\":[{\"station\":{\"id\":{\"type\":\"genre\",\"tag\":\"jazz\"},"
             "\"name\":\"%s\"}}]}",
             long_name);
    yandex_catalog_t catalog;
    assert(!yandex_catalog_parse_dashboard(json, &catalog));
    assert(catalog.count == 0U);
}

static void test_more_stations_than_fit_are_taken_up_to_the_limit(void)
{
    char json[4096];
    size_t offset = (size_t)snprintf(json, sizeof(json), "{\"stations\":[");
    for (int index = 0; index < YANDEX_CATALOG_MAX_STATIONS + 4; ++index) {
        offset += (size_t)snprintf(json + offset, sizeof(json) - offset,
                                   "%s{\"station\":{\"id\":{\"type\":\"genre\",\"tag\":\"g%d\"},"
                                   "\"name\":\"S%d\"}}",
                                   index == 0 ? "" : ",", index, index);
    }
    snprintf(json + offset, sizeof(json) - offset, "]}");
    yandex_catalog_t catalog;
    assert(yandex_catalog_parse_dashboard(json, &catalog));
    assert(catalog.count == YANDEX_CATALOG_MAX_STATIONS);
    assert(strcmp(catalog.stations[0].name, "S0") == 0);
}

static void test_escapes_are_decoded_including_the_ones_yandex_does_not_send(void)
{
    /* The server sends Cyrillic raw, verified on a capture. This only matters
     * if that ever changes - but a name rendered as literal backslash-u would
     * be a puzzling bug to chase later. */
    yandex_catalog_t catalog;
    assert(yandex_catalog_parse_dashboard(
        "{\"stations\":[{\"station\":{\"id\":{\"type\":\"genre\",\"tag\":\"jazz\"},"
        "\"name\":\"\\u0414\\u0436\\u0430\\u0437 \\\"live\\\"\"}}]}",
        &catalog));
    assert(strcmp(catalog.stations[0].name, "Джаз \"live\"") == 0);
}

static void test_malformed_answers_are_refused_without_reading_past_the_end(void)
{
    yandex_catalog_t catalog;
    assert(!yandex_catalog_parse_dashboard("", &catalog));
    assert(!yandex_catalog_parse_dashboard("{", &catalog));
    assert(!yandex_catalog_parse_dashboard("{\"stations\":", &catalog));
    assert(!yandex_catalog_parse_dashboard("{\"stations\":[{\"station\":{", &catalog));
    assert(!yandex_catalog_parse_dashboard("{\"stations\":[]}", &catalog) ||
           catalog.count == 0U);
    assert(!yandex_catalog_parse_dashboard("{\"result\":{\"other\":1}}", &catalog));
    assert(!yandex_catalog_parse_dashboard(NULL, &catalog));
    assert(!yandex_catalog_parse_dashboard(DASHBOARD, NULL));
}

static void test_two_identical_answers_compare_equal(void)
{
    /* What lets a refresh that changed nothing leave the screen alone. */
    yandex_catalog_t first;
    yandex_catalog_t second;
    assert(yandex_catalog_parse_dashboard(DASHBOARD, &first));
    assert(yandex_catalog_parse_dashboard(DASHBOARD, &second));
    assert(yandex_catalog_equal(&first, &second));
    snprintf(second.stations[1].name, sizeof(second.stations[1].name), "Другое");
    assert(!yandex_catalog_equal(&first, &second));
    second = first;
    second.count -= 1U;
    assert(!yandex_catalog_equal(&first, &second));
    assert(!yandex_catalog_equal(&first, NULL));
}

/* Trimmed from POST /wheel/new, 2026-10-13: an artist wave, the two
 * controls, a genre, a wave of two seeds, and one the dashboard already has. */
static const char *const WHEEL =
    "{\"wheelId\":\"8cf7ffd1\",\"items\":["
    "{\"id\":\"artist:36810\",\"type\":\"WAVE\",\"style\":\"DEFAULT\","
    "\"description\":\"В стиле артиста\",\"data\":{\"wave\":{\"name\":\"Depeche Mode\","
    "\"description\":\"Моя волна по артисту\",\"seeds\":[\"artist:36810\"]},"
    "\"agent\":{\"cover\":{\"uri\":\"avatars/%%\",\"color\":\"#7C7C7C\"},"
    "\"entity\":{\"type\":\"artist\"}}}},"
    "{\"id\":\"user:onyourwave\",\"type\":\"WAVE\",\"style\":\"CONTROL_ACCENT\","
    "\"data\":{\"wave\":{\"name\":\"Назад к привычному\",\"seeds\":[\"user:onyourwave\"]}}},"
    "{\"id\":\"diversity:reshuffle_1\",\"type\":\"WAVE\",\"style\":\"CONTROL_ACCENT\","
    "\"data\":{\"wave\":{\"name\":\"Встряхнуть Мою волну\","
    "\"seeds\":[\"diversity:reshuffle_1791917855515\"]}}},"
    "{\"id\":\"genre:jazz\",\"type\":\"WAVE\",\"style\":\"DEFAULT\","
    "\"data\":{\"wave\":{\"name\":\"Весь этот джаз\",\"seeds\":[\"genre:jazz\"]}}},"
    "{\"id\":\"x\",\"type\":\"WAVE\",\"style\":\"DEFAULT\",\"data\":{\"wave\":{"
    "\"name\":\"Веселюсь на вечеринке под лучшие треки\","
    "\"seeds\":[\"mood:happy\",\"activity:party\"]}}},"
    "{\"id\":\"y\",\"type\":\"BANNER\",\"style\":\"DEFAULT\","
    "\"data\":{\"wave\":{\"name\":\"Реклама\",\"seeds\":[\"genre:ad\"]}}}"
    "]}";

static void test_the_wheel_adds_its_waves_after_the_dashboard(void)
{
    yandex_catalog_t catalog;
    assert(yandex_catalog_parse_dashboard(DASHBOARD, &catalog));
    assert(catalog.count == 3U);
    /* Depeche Mode and the party; jazz is already the dashboard's, the
     * controls steer the personal wave, and a banner is not a wave. */
    assert(yandex_catalog_parse_wheel(WHEEL, &catalog) == 2U);
    assert(catalog.count == 5U);
    assert(strcmp(catalog.stations[3].id, "artist:36810") == 0);
    assert(strcmp(catalog.stations[3].name, "Depeche Mode") == 0);
    assert(catalog.stations[3].from[0] == '\0');
    assert(strcmp(catalog.stations[4].id, "mood:happy+activity:party") == 0);
    assert(strcmp(catalog.stations[4].name, "Веселюсь на вечеринке под лучшие треки") == 0);

    /* On its own, with nothing from the dashboard. */
    catalog = (yandex_catalog_t){0};
    assert(yandex_catalog_parse_wheel(WHEEL, &catalog) == 3U);
    assert(strcmp(catalog.stations[1].id, "genre:jazz") == 0);
}

static void test_a_wave_that_does_not_fit_is_left_out(void)
{
    /* Seeds that join past the id's room would name a different wave if cut. */
    char json[512];
    snprintf(json, sizeof(json),
             "{\"items\":[{\"type\":\"WAVE\",\"style\":\"DEFAULT\",\"data\":{\"wave\":{"
             "\"name\":\"Длинная\",\"seeds\":[\"%040d\",\"%040d\"]}}},"
             "{\"type\":\"WAVE\",\"data\":{\"wave\":{\"name\":\"Без сидов\",\"seeds\":[]}}},"
             "{\"type\":\"WAVE\",\"data\":{\"wave\":{\"name\":\"Поп\",\"seeds\":[\"genre:pop\"]}}}]}",
             0, 0);
    yandex_catalog_t catalog = {0};
    assert(yandex_catalog_parse_wheel(json, &catalog) == 1U);
    assert(strcmp(catalog.stations[0].id, "genre:pop") == 0);

    assert(yandex_catalog_parse_wheel("{\"items\":[{\"type\":", &catalog) == 0U);
    assert(yandex_catalog_parse_wheel("{\"error\":{}}", &catalog) == 0U);
    assert(yandex_catalog_parse_wheel(NULL, &catalog) == 0U);
    assert(catalog.count == 1U);
}

static void test_an_artist_wave_is_told_apart(void)
{
    assert(yandex_station_is_artist_wave("artist:36810"));
    assert(!yandex_station_is_artist_wave("genre:jazz"));
    assert(!yandex_station_is_artist_wave("artist"));
    assert(!yandex_station_is_artist_wave("artist:1+mood:sad"));
    assert(!yandex_station_is_artist_wave(NULL));
}

static yandex_catalog_t catalog_of(const char *const *ids, uint8_t count)
{
    yandex_catalog_t catalog = {0};
    for (uint8_t index = 0U; index < count; ++index) {
        snprintf(catalog.stations[index].id, sizeof(catalog.stations[index].id), "%s", ids[index]);
        snprintf(catalog.stations[index].name, sizeof(catalog.stations[index].name), "n%s",
                 ids[index]);
    }
    catalog.count = count;
    return catalog;
}

static void test_a_reshuffled_wheel_keeps_the_rows_where_they_were(void)
{
    static const char *const before[] = {"user:onyourwave", "genre:jazz", "mood:happy",
                                         "artist:1"};
    // Reshuffled, one gone (genre:jazz), one new (genre:pop).
    static const char *const after[] = {"mood:happy", "genre:pop", "artist:1",
                                        "user:onyourwave"};
    const yandex_catalog_t previous = catalog_of(before, 4U);
    yandex_catalog_t fresh = catalog_of(after, 4U);
    yandex_catalog_keep_order(&previous, &fresh);
    assert(fresh.count == 4U);
    assert(strcmp(fresh.stations[0].id, "user:onyourwave") == 0);
    assert(strcmp(fresh.stations[1].id, "mood:happy") == 0);
    assert(strcmp(fresh.stations[2].id, "artist:1") == 0);
    assert(strcmp(fresh.stations[3].id, "genre:pop") == 0);
    assert(strcmp(fresh.stations[3].name, "ngenre:pop") == 0);
    // The same stations again: equal, so a refresh moves nothing at all.
    yandex_catalog_t again = catalog_of(after, 4U);
    yandex_catalog_keep_order(&fresh, &again);
    assert(yandex_catalog_equal(&fresh, &again));
    // Nothing before: the order that came.
    const yandex_catalog_t empty = {0};
    yandex_catalog_t first = catalog_of(after, 4U);
    yandex_catalog_keep_order(&empty, &first);
    assert(strcmp(first.stations[0].id, "mood:happy") == 0);
}

int main(void)
{
    test_a_reshuffled_wheel_keeps_the_rows_where_they_were();
    test_an_artist_wave_is_told_apart();
    test_the_wheel_adds_its_waves_after_the_dashboard();
    test_a_wave_that_does_not_fit_is_left_out();
    test_the_real_shape_yields_exactly_the_stations();
    test_the_settings_inside_a_station_do_not_leak_into_it();
    test_the_envelope_is_optional();
    test_one_broken_station_does_not_cost_the_others();
    test_an_overlong_field_drops_that_station_not_a_truncated_one();
    test_more_stations_than_fit_are_taken_up_to_the_limit();
    test_escapes_are_decoded_including_the_ones_yandex_does_not_send();
    test_malformed_answers_are_refused_without_reading_past_the_end();
    test_two_identical_answers_compare_equal();
    printf("yandex_catalog tests passed\n");
    return 0;
}
