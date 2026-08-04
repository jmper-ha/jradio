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

int main(void)
{
    test_only_one_source_can_play();
    test_stopping_active_source_returns_to_idle();
    puts("audio_source tests passed");
    return 0;
}
