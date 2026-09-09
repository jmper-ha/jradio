#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "device_text.h"

/* The cap the player's error line lives under. Duplicated rather than pulled
 * in: player_control_types.h drags in half the audio stack, and this is a
 * number, not a dependency. */
#define PLAYER_ERROR_MAX_LEN 64

static void test_every_id_speaks_both_languages(void)
{
    for (int id = 0; id < DEVICE_TEXT_COUNT; ++id) {
        const char *ru = device_text((device_text_id_t)id, DEVICE_LANGUAGE_RU);
        const char *en = device_text((device_text_id_t)id, DEVICE_LANGUAGE_EN);
        assert(ru != NULL && en != NULL);
        /* An id added to the enum and not to the table reads back as the
         * fallback question mark; a half-filled entry reads as one language
         * borrowing the other's words. Both are the failure this exists to
         * catch, and both are silent on the device. */
        assert(ru[0] != '\0' && en[0] != '\0');
        assert(strcmp(ru, "?") != 0 && strcmp(en, "?") != 0);
    }
}

static void test_the_table_is_keyed_by_id(void)
{
    /* The entries are written with designated initializers, so their order in
     * the file means nothing and a line in the wrong place cannot hand back a
     * neighbour's words. Spot-checked anyway, because that guarantee is only
     * as good as the id in the brackets. */
    assert(strcmp(device_text(DEVICE_TEXT_SOURCE_NONE, DEVICE_LANGUAGE_EN), "No source") == 0);
    assert(strcmp(device_text(DEVICE_TEXT_SOURCE_NONE, DEVICE_LANGUAGE_RU),
                  "Нет источника") == 0);
    assert(strcmp(device_text(DEVICE_TEXT_STATE_PAUSED, DEVICE_LANGUAGE_EN), "Paused") == 0);
    assert(strcmp(device_text(DEVICE_TEXT_ROW_BRIGHTNESS, DEVICE_LANGUAGE_EN),
                  "Brightness") == 0);
    assert(strcmp(device_text(DEVICE_TEXT_HINT_LINK, DEVICE_LANGUAGE_EN),
                  "OK - link, hold - back") == 0);
}

static void test_a_language_picker_can_always_be_read(void)
{
    /* Both names stay in their own language whichever one is in force -
     * otherwise a reader who cannot read the current setting has no way back
     * out of it. */
    assert(strcmp(device_text(DEVICE_TEXT_LANGUAGE_RUSSIAN, DEVICE_LANGUAGE_EN),
                  device_text(DEVICE_TEXT_LANGUAGE_RUSSIAN, DEVICE_LANGUAGE_RU)) == 0);
    assert(strcmp(device_text(DEVICE_TEXT_LANGUAGE_ENGLISH, DEVICE_LANGUAGE_EN),
                  device_text(DEVICE_TEXT_LANGUAGE_ENGLISH, DEVICE_LANGUAGE_RU)) == 0);
}

static void test_the_error_lines_fit_the_snapshot(void)
{
    /* The snapshot carries these in a fixed 64-byte field, and Cyrillic costs
     * two bytes a character - which is how "Нечего включить - выберите в
     * списке" turned out to be 65 bytes and failed the build. Checked here so
     * that a translation cannot quietly truncate one instead. */
    static const device_text_id_t errors[] = {
        DEVICE_TEXT_ERROR_STATION_FAILED,   DEVICE_TEXT_ERROR_FILE_FAILED,
        DEVICE_TEXT_ERROR_NOTHING_TO_RESUME, DEVICE_TEXT_ERROR_FOLDER_FAILED,
        DEVICE_TEXT_ERROR_YANDEX_SUBSCRIPTION, DEVICE_TEXT_ERROR_YANDEX_REFRESH,
        DEVICE_TEXT_ERROR_BAD_COMMAND,      DEVICE_TEXT_ERROR_DEVICE_BUSY,
    };
    for (size_t index = 0U; index < sizeof(errors) / sizeof(errors[0]); ++index) {
        assert(strlen(device_text(errors[index], DEVICE_LANGUAGE_RU)) < PLAYER_ERROR_MAX_LEN);
        assert(strlen(device_text(errors[index], DEVICE_LANGUAGE_EN)) < PLAYER_ERROR_MAX_LEN);
    }
}

static void test_an_id_out_of_range_still_shows_something(void)
{
    const char *out = device_text((device_text_id_t)DEVICE_TEXT_COUNT, DEVICE_LANGUAGE_RU);
    assert(out != NULL && out[0] != '\0');
}

int main(void)
{
    test_every_id_speaks_both_languages();
    test_the_table_is_keyed_by_id();
    test_a_language_picker_can_always_be_read();
    test_the_error_lines_fit_the_snapshot();
    test_an_id_out_of_range_still_shows_something();
    puts("device_text tests passed");
    return 0;
}
