#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "device_settings.h"
#include "settings_csv.h"
#include "web_settings.h"

static const char *test_path = "/tmp/jradio-web-settings.csv";

static void reset_file(void)
{
    (void)unlink(test_path);
    FILE *file = fopen(test_path, "w");
    assert(file != NULL);
    // A line this layer knows nothing about, to prove a web write leaves the
    // rest of the file alone - the last station lives in here too.
    fputs("station_url,http://example.invalid/stream\n", file);
    fclose(file);
}

static bool parse_one(const char *body, web_settings_change_t *change)
{
    return web_settings_parse(body, strlen(body), change);
}

static void test_choices_are_named_not_numbered(void)
{
    web_settings_change_t change;
    assert(parse_one("{\"field\":\"language\",\"value\":\"en\"}", &change));
    assert(change.field == WEB_SETTINGS_FIELD_LANGUAGE);
    assert(change.value == DEVICE_LANGUAGE_EN);

    assert(parse_one("{\"field\":\"language\",\"value\":\"ru\"}", &change));
    assert(change.value == DEVICE_LANGUAGE_RU);

    assert(parse_one("{\"field\":\"home_screen\",\"value\":\"feed\"}", &change));
    assert(change.field == WEB_SETTINGS_FIELD_HOME_SCREEN);
    assert(change.value == DEVICE_HOME_SCREEN_FEED);

    assert(parse_one("{\"field\":\"scroll\",\"value\":\"left\"}", &change));
    assert(change.field == WEB_SETTINGS_FIELD_SCROLL);
    assert(change.value == DEVICE_SCROLL_LEFT);

    /* A name this build does not know is refused rather than falling back to
     * the first choice: the page would then show a value nobody asked for. */
    assert(!parse_one("{\"field\":\"language\",\"value\":\"de\"}", &change));
    // And the ordinal is not an accepted spelling of the name.
    assert(!parse_one("{\"field\":\"language\",\"value\":1}", &change));
    assert(!parse_one("{\"field\":\"language\",\"value\":true}", &change));
}

static void test_switches_take_booleans_only(void)
{
    web_settings_change_t change;
    assert(parse_one("{\"field\":\"autoplay\",\"value\":true}", &change));
    assert(change.field == WEB_SETTINGS_FIELD_AUTOPLAY);
    assert(change.value == 1);

    assert(parse_one("{\"field\":\"flip_horizontal\",\"value\":false}", &change));
    assert(change.field == WEB_SETTINGS_FIELD_FLIP_HORIZONTAL);
    assert(change.value == 0);

    assert(!parse_one("{\"field\":\"autoplay\",\"value\":1}", &change));
    assert(!parse_one("{\"field\":\"autoplay\",\"value\":\"on\"}", &change));
}

/* Brightness stops where the encoder does. Below about ten the panel cannot be
 * read and zero looks like a dead device, so a slider that could reach them
 * would be a way to lose the screen from across the room. */
static void test_numbers_are_range_checked(void)
{
    web_settings_change_t change;
    assert(parse_one("{\"field\":\"brightness\",\"value\":10}", &change));
    assert(change.value == WEB_SETTINGS_BRIGHTNESS_MIN);
    assert(parse_one("{\"field\":\"brightness\",\"value\":90}", &change));
    assert(change.value == WEB_SETTINGS_BRIGHTNESS_MAX);
    assert(!parse_one("{\"field\":\"brightness\",\"value\":9}", &change));
    assert(!parse_one("{\"field\":\"brightness\",\"value\":91}", &change));
    assert(!parse_one("{\"field\":\"brightness\",\"value\":-5}", &change));

    // The volume has the whole range: silence is a thing to ask for.
    assert(parse_one("{\"field\":\"volume\",\"value\":0}", &change));
    assert(change.field == WEB_SETTINGS_FIELD_VOLUME);
    assert(change.value == 0);
    assert(parse_one("{\"field\":\"volume\",\"value\":100}", &change));
    assert(change.value == 100);
    assert(!parse_one("{\"field\":\"volume\",\"value\":101}", &change));
    // Rounding a fraction behind the caller's back would show a value the
    // device never took.
    assert(!parse_one("{\"field\":\"volume\",\"value\":55.5}", &change));
}

static void test_malformed_requests_are_refused(void)
{
    web_settings_change_t change;
    assert(!parse_one("", &change));
    assert(!parse_one("not json", &change));
    assert(!parse_one("[{\"field\":\"volume\",\"value\":10}]", &change));
    assert(!parse_one("{\"field\":\"volume\"}", &change));
    assert(!parse_one("{\"value\":10}", &change));
    assert(!parse_one("{\"field\":\"nothing\",\"value\":10}", &change));
    /* A third member is refused rather than ignored: it is how a page from an
     * older firmware announces that it means something this build does not,
     * and applying the half that parses would be worse than refusing. */
    assert(!parse_one("{\"field\":\"volume\",\"value\":10,\"extra\":1}", &change));
    // An embedded NUL never reaches cJSON, which would stop at it.
    const char embedded[] = "{\"field\":\"volume\",\"value\"\0:10}";
    assert(!web_settings_parse(embedded, sizeof(embedded) - 1U, &change));
    assert(!web_settings_parse(NULL, 4U, &change));
}

static void test_apply_writes_through_to_the_file(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));

    const web_settings_change_t brightness = {WEB_SETTINGS_FIELD_BRIGHTNESS, 35};
    assert(web_settings_apply(&settings, &brightness));
    assert(settings.brightness == 35);

    const web_settings_change_t scroll = {WEB_SETTINGS_FIELD_SCROLL, DEVICE_SCROLL_LEFT};
    assert(web_settings_apply(&settings, &scroll));

    /* Read back through a second copy, the way the device's UI task does after
     * it is told the file changed: that round trip is the whole mechanism by
     * which a browser change reaches the panel. */
    device_settings_t reloaded;
    assert(device_settings_init_at(&reloaded, test_path));
    assert(reloaded.brightness == 35);
    assert(reloaded.scroll == DEVICE_SCROLL_LEFT);

    char value[64];
    assert(settings_csv_get(test_path, "station_url", value, sizeof(value)));
    assert(strcmp(value, "http://example.invalid/stream") == 0);
}

static void test_document_names_what_the_build_has(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(device_settings_set_language(&settings, DEVICE_LANGUAGE_EN));
    assert(device_settings_set_autoplay(&settings, true));
    assert(device_settings_set_volume(&settings, 42));

    web_settings_view_t view;
    web_settings_make_view(&view, &settings, true, false);
    char document[512];
    size_t length = web_settings_serialize(document, sizeof(document), &view);
    assert(length > 0U && length == strlen(document));
    assert(strstr(document, "\"language\":\"en\"") != NULL);
    assert(strstr(document, "\"home_screen\":\"text\"") != NULL);
    assert(strstr(document, "\"scroll\":\"bounce\"") != NULL);
    assert(strstr(document, "\"autoplay\":true") != NULL);
    assert(strstr(document, "\"volume\":42") != NULL);
    assert(strstr(document, "\"brightness\":50") != NULL);
    // A build without Yandex Music says so, so the page drops the row rather
    // than offering a switch behind which there is nothing.
    assert(strstr(document, "\"yandex_music\":false}") != NULL);
    assert(strstr(document, "\"home_screen\":true") != NULL);
    assert(strstr(document, "\"brightness_min\":10") != NULL);
    assert(strstr(document, "\"brightness_max\":90") != NULL);

    // Truncation is never handed back as a short document.
    char tight[32];
    assert(web_settings_serialize(tight, sizeof(tight), &view) == 0U);
    assert(tight[0] == '\0');
}

/* The comparison the WebSocket broadcaster runs on every pass to decide whether
 * anything has to be sent. One field at a time, because the whole point is
 * that turning the knob one step must reach an open settings page. */
static void test_view_comparison_notices_every_field(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));

    web_settings_view_t base;
    web_settings_make_view(&base, &settings, true, true);
    web_settings_view_t other = base;
    assert(web_settings_view_equal(&base, &other));

    other.volume = (uint8_t)(base.volume + 1U);
    assert(!web_settings_view_equal(&base, &other));
    other = base;
    other.brightness = (uint8_t)(base.brightness + 5U);
    assert(!web_settings_view_equal(&base, &other));
    other = base;
    other.language = DEVICE_LANGUAGE_EN;
    assert(!web_settings_view_equal(&base, &other));
    other = base;
    other.home_screen = DEVICE_HOME_SCREEN_FEED;
    assert(!web_settings_view_equal(&base, &other));
    other = base;
    other.scroll = DEVICE_SCROLL_LEFT;
    assert(!web_settings_view_equal(&base, &other));
    other = base;
    other.autoplay = !base.autoplay;
    assert(!web_settings_view_equal(&base, &other));
    other = base;
    other.yandex_music = !base.yandex_music;
    assert(!web_settings_view_equal(&base, &other));
    other = base;
    other.flip_vertical = !base.flip_vertical;
    assert(!web_settings_view_equal(&base, &other));
    other = base;
    other.flip_horizontal = !base.flip_horizontal;
    assert(!web_settings_view_equal(&base, &other));
    /* The availability flags too: turning the Yandex switch off can take the
     * last choice off the home screen, and the page has to be told the row is
     * gone rather than left offering it. */
    other = base;
    other.home_screen_available = !base.home_screen_available;
    assert(!web_settings_view_equal(&base, &other));
    other = base;
    other.yandex_available = !base.yandex_available;
    assert(!web_settings_view_equal(&base, &other));
}

/* The copy the UI task hands to everyone who must not read the card. */
static void test_published_copy_round_trips(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(device_settings_set_volume(&settings, 17));

    device_settings_publish(&settings);
    device_settings_t read_back;
    assert(device_settings_read_published(&read_back));
    assert(read_back.volume == 17);

    // A later publish replaces it rather than accumulating.
    assert(device_settings_set_volume(&settings, 88));
    device_settings_publish(&settings);
    assert(device_settings_read_published(&read_back));
    assert(read_back.volume == 88);
    assert(!device_settings_read_published(NULL));
}

int main(void)
{
    settings_csv_init();
    test_choices_are_named_not_numbered();
    test_switches_take_booleans_only();
    test_numbers_are_range_checked();
    test_malformed_requests_are_refused();
    test_apply_writes_through_to_the_file();
    test_document_names_what_the_build_has();
    test_view_comparison_notices_every_field();
    test_published_copy_round_trips();
    (void)unlink(test_path);
    printf("web settings tests passed\n");
    return 0;
}
