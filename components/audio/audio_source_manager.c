#include "audio_source.h"

#include <stddef.h>

void audio_source_manager_init(audio_source_manager_t *manager)
{
    if (manager != NULL) {
        manager->active_source = AUDIO_SOURCE_NONE;
    }
}

audio_source_result_t audio_source_manager_start(audio_source_manager_t *manager,
                                                 audio_source_t source)
{
    if (manager == NULL || source == AUDIO_SOURCE_NONE) {
        return AUDIO_SOURCE_INVALID_ARGUMENT;
    }
    if (manager->active_source != AUDIO_SOURCE_NONE) {
        return AUDIO_SOURCE_BUSY;
    }

    manager->active_source = source;
    return AUDIO_SOURCE_OK;
}

audio_source_result_t audio_source_manager_stop(audio_source_manager_t *manager,
                                                audio_source_t source)
{
    if (manager == NULL || source == AUDIO_SOURCE_NONE) {
        return AUDIO_SOURCE_INVALID_ARGUMENT;
    }
    if (manager->active_source != source) {
        return AUDIO_SOURCE_BUSY;
    }

    manager->active_source = AUDIO_SOURCE_NONE;
    return AUDIO_SOURCE_OK;
}

audio_source_t audio_source_manager_active(const audio_source_manager_t *manager)
{
    return manager == NULL ? AUDIO_SOURCE_NONE : manager->active_source;
}
