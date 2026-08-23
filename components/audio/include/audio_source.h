#pragma once

#include <stdbool.h>

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
    AUDIO_SOURCE_SD,
    /* A Yandex Music station. Appended rather than slotted in next to the
     * radio: these values are what a saved setting holds, and renumbering them
     * would silently change what a device restores after an update. */
    AUDIO_SOURCE_YANDEX,
} audio_source_t;

/* True for the sources that are a filesystem with a browser over it - the USB
 * drive and the SD card.
 *
 * They differ only in how the volume is mounted; everything above that - the
 * listing, the browser screen, the player, seeking, tags, the resume point -
 * is the same code, and this is what it asks instead of naming one of them.
 * Adding a third volume means adding it here and nowhere else. */
static inline bool audio_source_is_files(audio_source_t source)
{
    return source == AUDIO_SOURCE_USB || source == AUDIO_SOURCE_SD;
}

/* True for the sources that are a flat list of stations rather than a
 * filesystem: the internet radio catalog and the Yandex rotor.
 *
 * Both play an HTTP stream through the same decoder and answer to the same
 * pause, resume and stop; what differs is only where the list comes from and,
 * for Yandex, that the stream arrives one track at a time. The counterpart of
 * audio_source_is_files(), and used the same way - by asking, not by naming. */
static inline bool audio_source_is_stations(audio_source_t source)
{
    return source == AUDIO_SOURCE_INTERNET_RADIO || source == AUDIO_SOURCE_YANDEX;
}

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
