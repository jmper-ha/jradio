#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_autoplay.h"

static device_settings_t settings(bool autoplay, device_last_source_t source,
                                  const char *file)
{
    device_settings_t value = {0};
    value.autoplay = autoplay;
    value.last_source = source;
    if (file != NULL) snprintf(value.last_file, sizeof(value.last_file), "%s", file);
    return value;
}

static void test_autoplay_off_always_opens_the_home_screen(void)
{
    /* The setting is the master switch: nothing else is even consulted. */
    const device_settings_t radio = settings(false, DEVICE_LAST_SOURCE_INTERNET_RADIO, NULL);
    assert(ui_autoplay_decide(&radio, FILE_BROWSER_MEDIA_READY, FILE_BROWSER_MEDIA_ABSENT, true) == UI_AUTOPLAY_HOME);

    const device_settings_t usb = settings(false, DEVICE_LAST_SOURCE_USB, "/usb0/a.mp3");
    assert(ui_autoplay_decide(&usb, FILE_BROWSER_MEDIA_READY, FILE_BROWSER_MEDIA_ABSENT, true) == UI_AUTOPLAY_HOME);
}

static void test_nothing_played_before_opens_the_home_screen(void)
{
    /* First boot after enabling the setting: there is no resume point yet. */
    const device_settings_t value = settings(true, DEVICE_LAST_SOURCE_NONE, NULL);
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_READY, FILE_BROWSER_MEDIA_ABSENT, true) == UI_AUTOPLAY_HOME);
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_ABSENT, FILE_BROWSER_MEDIA_ABSENT, false) == UI_AUTOPLAY_HOME);
}

static void test_radio_resumes_whatever_the_drive_is_doing(void)
{
    /* The radio's resume point has nothing to do with USB, so a missing drive
     * must not derail it. */
    const device_settings_t value = settings(true, DEVICE_LAST_SOURCE_INTERNET_RADIO, NULL);
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_READY, FILE_BROWSER_MEDIA_ABSENT, true) == UI_AUTOPLAY_RADIO);
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_ABSENT, FILE_BROWSER_MEDIA_ABSENT, false) == UI_AUTOPLAY_RADIO);
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_UNREADABLE, FILE_BROWSER_MEDIA_ABSENT, false) == UI_AUTOPLAY_RADIO);
}

static void test_usb_resumes_the_same_file_when_it_is_still_there(void)
{
    const device_settings_t value = settings(true, DEVICE_LAST_SOURCE_USB, "/usb0/dir/a.mp3");
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_READY, FILE_BROWSER_MEDIA_ABSENT, true) == UI_AUTOPLAY_FILE);
}

static void test_a_missing_file_falls_back_to_the_browser(void)
{
    /* A different stick, or the track was deleted. There is still music to
     * pick, so an error screen would be wrong. */
    const device_settings_t value = settings(true, DEVICE_LAST_SOURCE_USB, "/usb0/gone.mp3");
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_READY, FILE_BROWSER_MEDIA_ABSENT, false) ==
           UI_AUTOPLAY_FILE_BROWSER);
}

static void test_usb_last_but_nothing_played_opens_the_browser(void)
{
    /* The source was entered and left without choosing a track. */
    const device_settings_t value = settings(true, DEVICE_LAST_SOURCE_USB, "");
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_READY, FILE_BROWSER_MEDIA_ABSENT, false) ==
           UI_AUTOPLAY_FILE_BROWSER);
    /* file_present cannot rescue an empty path. */
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_READY, FILE_BROWSER_MEDIA_ABSENT, true) ==
           UI_AUTOPLAY_FILE_BROWSER);
}

static void test_no_drive_says_so_rather_than_opening_an_empty_browser(void)
{
    const device_settings_t value = settings(true, DEVICE_LAST_SOURCE_USB, "/usb0/a.mp3");
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_ABSENT, FILE_BROWSER_MEDIA_ABSENT, false) ==
           UI_AUTOPLAY_FILE_UNAVAILABLE);
    /* Mounted but unreadable is still "no drive" as far as the user is
     * concerned, and it must not be mistaken for a missing file. */
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_UNREADABLE, FILE_BROWSER_MEDIA_ABSENT, true) ==
           UI_AUTOPLAY_FILE_UNAVAILABLE);
}

static void test_the_drive_check_comes_before_the_file_check(void)
{
    /* file_present is meaningless without a readable drive; a stale true must
     * not turn a missing stick into a playback attempt. */
    const device_settings_t value = settings(true, DEVICE_LAST_SOURCE_USB, "/usb0/a.mp3");
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_ABSENT, FILE_BROWSER_MEDIA_ABSENT, true) ==
           UI_AUTOPLAY_FILE_UNAVAILABLE);
}

static void test_the_card_is_judged_by_its_own_state(void)
{
    /* The two volumes are independent: a card resume must not be decided by
     * the drive's state, which is what a single media argument would have
     * done. */
    const device_settings_t value = settings(true, DEVICE_LAST_SOURCE_SD, "/sd0/dir/a.mp3");
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_ABSENT, FILE_BROWSER_MEDIA_READY,
                              true) == UI_AUTOPLAY_FILE);
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_READY, FILE_BROWSER_MEDIA_ABSENT,
                              true) == UI_AUTOPLAY_FILE_UNAVAILABLE);
    assert(ui_autoplay_decide(&value, FILE_BROWSER_MEDIA_ABSENT, FILE_BROWSER_MEDIA_READY,
                              false) == UI_AUTOPLAY_FILE_BROWSER);
}

static void test_the_remembered_source_comes_back_as_an_audio_source(void)
{
    const device_settings_t sd = settings(true, DEVICE_LAST_SOURCE_SD, "/sd0/a.mp3");
    const device_settings_t usb = settings(true, DEVICE_LAST_SOURCE_USB, "/usb0/a.mp3");
    const device_settings_t radio = settings(true, DEVICE_LAST_SOURCE_INTERNET_RADIO, NULL);
    const device_settings_t none = settings(true, DEVICE_LAST_SOURCE_NONE, NULL);
    assert(ui_autoplay_source(&sd) == AUDIO_SOURCE_SD);
    assert(ui_autoplay_source(&usb) == AUDIO_SOURCE_USB);
    assert(ui_autoplay_source(&radio) == AUDIO_SOURCE_INTERNET_RADIO);
    assert(ui_autoplay_source(&none) == AUDIO_SOURCE_NONE);
    assert(ui_autoplay_source(NULL) == AUDIO_SOURCE_NONE);
}

static void test_a_missing_settings_struct_opens_the_home_screen(void)
{
    assert(ui_autoplay_decide(NULL, FILE_BROWSER_MEDIA_READY, FILE_BROWSER_MEDIA_ABSENT, true) == UI_AUTOPLAY_HOME);
}

int main(void)
{
    test_autoplay_off_always_opens_the_home_screen();
    test_nothing_played_before_opens_the_home_screen();
    test_radio_resumes_whatever_the_drive_is_doing();
    test_usb_resumes_the_same_file_when_it_is_still_there();
    test_a_missing_file_falls_back_to_the_browser();
    test_usb_last_but_nothing_played_opens_the_browser();
    test_no_drive_says_so_rather_than_opening_an_empty_browser();
    test_the_drive_check_comes_before_the_file_check();
    test_the_card_is_judged_by_its_own_state();
    test_the_remembered_source_comes_back_as_an_audio_source();
    test_a_missing_settings_struct_opens_the_home_screen();
    puts("ui_autoplay tests passed");
    return 0;
}
