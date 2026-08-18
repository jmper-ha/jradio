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

    char value[32];
    assert(settings_csv_get(test_path, "unknown", value, sizeof(value)));
    assert(strcmp(value, "keep") == 0);
    assert(settings_csv_get(test_path, "language", value, sizeof(value)));
    assert(strcmp(value, "en") == 0);
    assert(settings_csv_get(test_path, "home_screen", value, sizeof(value)));
    assert(strcmp(value, "feed") == 0);
    assert(settings_csv_get(test_path, "display_flip_vertical", value, sizeof(value)));
    assert(strcmp(value, "1") == 0);
}

static void test_invalid_values_do_not_change_model(void)
{
    reset_file();
    device_settings_t settings;
    assert(device_settings_init_at(&settings, test_path));
    assert(!device_settings_set_language(&settings, (device_language_t)99));
    assert(!device_settings_set_home_screen(&settings, (device_home_screen_t)99));
    assert(!device_settings_set_flip_vertical_value(&settings, 2));
    assert(settings.language == DEVICE_LANGUAGE_RU);
    assert(settings.home_screen == DEVICE_HOME_SCREEN_TEXT);
    assert(!settings.flip_vertical);
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

int main(void)
{
    test_defaults_and_load();
    test_values_and_unknown_lines_are_saved();
    test_invalid_values_do_not_change_model();
    (void)unlink(test_path);
    test_volume_defaults_and_persists();
    test_a_corrupt_volume_leaves_the_default();
    puts("device_settings tests passed");
    return 0;
}
