#include "device_settings.h"
#include "settings_csv.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *test_path = "/tmp/jradio-device-settings.csv";

static void reset_file(void)
{
    (void)unlink(test_path);
    FILE *file = fopen(test_path, "w");
    assert(file != NULL);
    fputs("unknown,keep\n", file);
    fclose(file);
}

static void test_defaults_and_load(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(settings.language == DEVICE_LANGUAGE_RU);
    assert(settings.home_screen == DEVICE_HOME_SCREEN_TEXT);
    assert(!settings.flip_vertical && !settings.flip_horizontal);
    /* On by default: a device whose firmware has Yandex Music should show it
     * without the user first going to find a switch. */
    assert(settings.yandex_music);
    assert(settings.brightness == DEVICE_BRIGHTNESS_DEFAULT);
    /* Bounce is what the device did before the setting existed. */
    assert(settings.scroll == DEVICE_SCROLL_BOUNCE);
    /* And a number is what the buffer reading was before it could be a strip:
     * a card written by an older build gets the screen it used to have. */
    assert(settings.buffer_view == DEVICE_BUFFER_VIEW_TEXT);
    assert(strcmp(settings.storage_path, test_path) == 0);
}

static void test_values_and_unknown_lines_are_saved(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(device_settings_set_language(&settings, DEVICE_LANGUAGE_EN));
    assert(device_settings_set_home_screen(&settings, DEVICE_HOME_SCREEN_FEED));
    assert(device_settings_set_flip_vertical(&settings, true));
    assert(device_settings_set_flip_horizontal(&settings, true));
    assert(device_settings_set_buffer_view(&settings, DEVICE_BUFFER_VIEW_GRAPH));

    char value[32];
    assert(settings_csv_get(test_path, "unknown", value, sizeof(value)));
    assert(strcmp(value, "keep") == 0);
    assert(settings_csv_get(test_path, "language", value, sizeof(value)));
    assert(strcmp(value, "en") == 0);
    assert(settings_csv_get(test_path, "home_screen", value, sizeof(value)));
    assert(strcmp(value, "feed") == 0);
    assert(settings_csv_get(test_path, "display_flip_vertical", value, sizeof(value)));
    assert(strcmp(value, "1") == 0);
    assert(settings_csv_get(test_path, "buffer_view", value, sizeof(value)));
    assert(strcmp(value, "graph") == 0);

    device_settings_t reloaded;
    assert(device_settings_init_at(&reloaded, test_path));
    assert(reloaded.buffer_view == DEVICE_BUFFER_VIEW_GRAPH);
}

static void test_invalid_values_do_not_change_model(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(!device_settings_set_language(&settings, (device_language_t)99));
    assert(!device_settings_set_home_screen(&settings, (device_home_screen_t)99));
    assert(!device_settings_set_flip_vertical_value(&settings, 2));
    assert(!device_settings_set_buffer_view(&settings, (device_buffer_view_t)99));
    assert(settings.language == DEVICE_LANGUAGE_RU);
    assert(settings.home_screen == DEVICE_HOME_SCREEN_TEXT);
    assert(!settings.flip_vertical);
    assert(settings.buffer_view == DEVICE_BUFFER_VIEW_TEXT);
}


static void test_volume_defaults_and_persists(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    /* A fresh device must not come up at full blast. */
    assert(settings.volume == DEVICE_VOLUME_DEFAULT);

    assert(device_settings_set_volume(&settings, 35U));
    assert(settings.volume == 35U);
    device_settings_t reloaded;
    assert(device_settings_init_at(&reloaded, test_path));
    assert(reloaded.volume == 35U);

    /* Both ends of the range are legitimate settings. */
    assert(device_settings_set_volume(&settings, 0U));
    assert(device_settings_set_volume(&settings, 100U));
    assert(device_settings_init_at(&reloaded, test_path));
    assert(reloaded.volume == 100U);

    /* Out of range is a caller bug; refusing it surfaces the bug instead of
     * hiding it behind a clamp. */
    assert(!device_settings_set_volume(&settings, 101U));
    assert(settings.volume == 100U);
}

static void test_a_corrupt_volume_leaves_the_default(void)
{
    /* Neither silence nor full blast is a safe reading of a damaged file. */
    (void)unlink(test_path);
    FILE *file = fopen(test_path, "w");
    assert(file != NULL);
    fputs("volume,loud\n", file);
    fclose(file);

    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(settings.volume == DEVICE_VOLUME_DEFAULT);

    (void)unlink(test_path);
    file = fopen(test_path, "w");
    assert(file != NULL);
    fputs("volume,150\n", file);
    fclose(file);
    assert(device_settings_init_at(&settings, test_path));
    assert(settings.volume == DEVICE_VOLUME_DEFAULT);
}

static void test_yandex_music_persists(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(device_settings_set_yandex_music(&settings, false));
    assert(!settings.yandex_music);

    char value[32];
    assert(settings_csv_get(test_path, "yandex_music", value, sizeof(value)));
    assert(strcmp(value, "0") == 0);

    device_settings_t reloaded;
    assert(device_settings_init_at(&reloaded, test_path));
    assert(!reloaded.yandex_music);
}

static void test_brightness_persists_and_refuses_a_dark_panel(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(device_settings_set_brightness(&settings, 30U));
    assert(settings.brightness == 30U);

    char value[32];
    assert(settings_csv_get(test_path, "brightness", value, sizeof(value)));
    assert(strcmp(value, "30") == 0);

    device_settings_t reloaded;
    assert(device_settings_init_at(&reloaded, test_path));
    assert(reloaded.brightness == 30U);

    /* Zero would leave a dark panel, and nothing on a dark panel can turn it
     * back up. Refused, and the stored value left alone. */
    assert(!device_settings_set_brightness(&settings, 0U));
    assert(!device_settings_set_brightness(&settings, 101U));
    assert(settings.brightness == 30U);
}

static void test_a_corrupt_brightness_leaves_the_default(void)
{
    reset_file();
    assert(settings_csv_set(test_path, "brightness", "bright"));
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(settings.brightness == DEVICE_BRIGHTNESS_DEFAULT);

    assert(settings_csv_set(test_path, "brightness", "0"));
    assert(device_settings_init_at(&settings, test_path));
    assert(settings.brightness == DEVICE_BRIGHTNESS_DEFAULT);
}

static void test_scroll_mode_persists(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(device_settings_set_scroll(&settings, DEVICE_SCROLL_LEFT));

    char value[32];
    assert(settings_csv_get(test_path, "scroll", value, sizeof(value)));
    assert(strcmp(value, "left") == 0);

    device_settings_t reloaded;
    assert(device_settings_init_at(&reloaded, test_path));
    assert(reloaded.scroll == DEVICE_SCROLL_LEFT);

    /* An unknown word is the old default rather than an unnamed third mode. */
    assert(settings_csv_set(test_path, "scroll", "sideways"));
    assert(device_settings_init_at(&reloaded, test_path));
    assert(reloaded.scroll == DEVICE_SCROLL_BOUNCE);

    assert(!device_settings_set_scroll(&settings, (device_scroll_t)99));
}

static void test_the_yandex_resume_point_persists(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(settings.last_source == DEVICE_LAST_SOURCE_NONE);
    assert(settings.last_yandex_id[0] == '\0');

    assert(device_settings_set_last_source(&settings, DEVICE_LAST_SOURCE_YANDEX));
    assert(device_settings_set_last_yandex(&settings, "user:onyourwave", "Моя волна",
                                           "user-onyourwave"));

    device_settings_t reloaded;
    assert(device_settings_init_at(&reloaded, test_path));
    assert(reloaded.last_source == DEVICE_LAST_SOURCE_YANDEX);
    assert(strcmp(reloaded.last_yandex_id, "user:onyourwave") == 0);
    // The name is the user's own text and travels whole, Cyrillic and all.
    assert(strcmp(reloaded.last_yandex_name, "Моя волна") == 0);
    assert(strcmp(reloaded.last_yandex_from, "user-onyourwave") == 0);

    /* Written as playback starts, over and over: the same identity again must
     * not cost the flash a write. */
    assert(device_settings_set_last_yandex(&settings, "user:onyourwave", "Моя волна",
                                           "user-onyourwave"));

    // A name and an origin are optional - a station still starts without them.
    assert(device_settings_set_last_yandex(&settings, "genre:jazz", "", ""));
    assert(device_settings_init_at(&reloaded, test_path));
    assert(strcmp(reloaded.last_yandex_id, "genre:jazz") == 0);
    assert(reloaded.last_yandex_name[0] == '\0');

    /* A station name is the account's own text, and settings.csv splits a line
     * on its first comma: the name survives with the comma turned into a
     * space rather than taking the resume point down with it. */
    assert(device_settings_set_last_yandex(&settings, "genre:pop", "Хиты 90-х, лучшее", ""));
    assert(device_settings_init_at(&reloaded, test_path));
    assert(strcmp(reloaded.last_yandex_id, "genre:pop") == 0);
    assert(strcmp(reloaded.last_yandex_name, "Хиты 90-х  лучшее") == 0);

    // Refused rather than truncated: half an id names no station.
    char oversized[DEVICE_LAST_YANDEX_ID_MAX + 8];
    memset(oversized, 'x', sizeof(oversized) - 1U);
    oversized[sizeof(oversized) - 1U] = '\0';
    assert(!device_settings_set_last_yandex(&settings, oversized, "", ""));
    assert(strcmp(settings.last_yandex_id, "genre:pop") == 0);
}

int main(void)
{
    test_defaults_and_load();
    test_values_and_unknown_lines_are_saved();
    test_invalid_values_do_not_change_model();
    (void)unlink(test_path);
    test_volume_defaults_and_persists();
    test_a_corrupt_volume_leaves_the_default();
    test_yandex_music_persists();
    test_scroll_mode_persists();
    test_the_yandex_resume_point_persists();
    test_brightness_persists_and_refuses_a_dark_panel();
    test_a_corrupt_brightness_leaves_the_default();
    puts("device_settings tests passed");
    return 0;
}
