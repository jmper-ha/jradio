#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_SOURCE_NONE = 0,
    AUDIO_SOURCE_USB,
    AUDIO_SOURCE_INTERNET_RADIO,
    AUDIO_SOURCE_DLNA,
    AUDIO_SOURCE_FM,
    AUDIO_SOURCE_BLUETOOTH,
} audio_source_t;

typedef enum {
    AUDIO_SOURCE_OK = 0,
    AUDIO_SOURCE_BUSY,
    AUDIO_SOURCE_INVALID_ARGUMENT,
} audio_source_result_t;

typedef struct {
    audio_source_t active_source;
} audio_source_manager_t;

void audio_source_manager_init(audio_source_manager_t *manager);
audio_source_result_t audio_source_manager_start(audio_source_manager_t *manager,
                                                 audio_source_t source);
audio_source_result_t audio_source_manager_stop(audio_source_manager_t *manager,
                                                audio_source_t source);
audio_source_t audio_source_manager_active(const audio_source_manager_t *manager);

#ifdef __cplusplus
}
#endif
