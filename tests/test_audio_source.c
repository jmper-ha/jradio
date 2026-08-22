#include <assert.h>
#include <stdio.h>

#include "audio_source.h"

static void test_only_one_source_can_play(void)
{
    audio_source_manager_t manager;

    audio_source_manager_init(&manager);
    assert(audio_source_manager_start(&manager, AUDIO_SOURCE_USB) == AUDIO_SOURCE_OK);
    assert(audio_source_manager_active(&manager) == AUDIO_SOURCE_USB);
    assert(audio_source_manager_start(&manager, AUDIO_SOURCE_INTERNET_RADIO) == AUDIO_SOURCE_BUSY);
    assert(audio_source_manager_active(&manager) == AUDIO_SOURCE_USB);
}

static void test_stopping_active_source_returns_to_idle(void)
{
    audio_source_manager_t manager;

    audio_source_manager_init(&manager);
    assert(audio_source_manager_start(&manager, AUDIO_SOURCE_USB) == AUDIO_SOURCE_OK);
    assert(audio_source_manager_stop(&manager, AUDIO_SOURCE_USB) == AUDIO_SOURCE_OK);
    assert(audio_source_manager_active(&manager) == AUDIO_SOURCE_NONE);
}

static void test_the_two_file_sources_are_told_apart_from_the_rest(void)
{
    /* Everything above the mount - the browser, the player, seeking, the
     * resume point - asks this instead of naming a source, so it is the one
     * place a third volume would be added. */
    assert(audio_source_is_files(AUDIO_SOURCE_USB));
    assert(audio_source_is_files(AUDIO_SOURCE_SD));
    assert(!audio_source_is_files(AUDIO_SOURCE_NONE));
    assert(!audio_source_is_files(AUDIO_SOURCE_INTERNET_RADIO));
    assert(!audio_source_is_files(AUDIO_SOURCE_BLUETOOTH));
    assert(!audio_source_is_files(AUDIO_SOURCE_FM));
    assert(!audio_source_is_files(AUDIO_SOURCE_DLNA));
}

static void test_the_card_is_a_source_like_any_other(void)
{
    audio_source_manager_t manager;

    audio_source_manager_init(&manager);
    assert(audio_source_manager_start(&manager, AUDIO_SOURCE_SD) == AUDIO_SOURCE_OK);
    // One at a time still: the card and the drive share the I2S output, and
    // holding both mounted is exactly what the card is unmounted to avoid.
    assert(audio_source_manager_start(&manager, AUDIO_SOURCE_USB) == AUDIO_SOURCE_BUSY);
    assert(audio_source_manager_stop(&manager, AUDIO_SOURCE_SD) == AUDIO_SOURCE_OK);
    assert(audio_source_manager_active(&manager) == AUDIO_SOURCE_NONE);
}

int main(void)
{
    test_the_two_file_sources_are_told_apart_from_the_rest();
    test_the_card_is_a_source_like_any_other();
    test_only_one_source_can_play();
    test_stopping_active_source_returns_to_idle();
    puts("audio_source tests passed");
    return 0;
}
