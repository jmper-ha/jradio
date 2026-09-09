#pragma once

#include <stddef.h>
#include <stdint.h>

#include "device_text.h"
#include "player_control_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEB_VIEW_SECTION_NONE = 0U,
    WEB_VIEW_SECTION_CAPABILITIES = 1U << 0,
    WEB_VIEW_SECTION_PLAYER = 1U << 1,
    WEB_VIEW_SECTION_LIST = 1U << 2,
    WEB_VIEW_SECTION_ALL = WEB_VIEW_SECTION_CAPABILITIES |
                           WEB_VIEW_SECTION_PLAYER |
                           WEB_VIEW_SECTION_LIST,
} web_view_section_t;

/* The machine-readable name, which never changes with the language: it is what
 * the browser's own code matches on. */
const char *web_view_playback_name(player_playback_state_t state);
const char *web_view_source_name(audio_source_t source);
/* What a person reads, which does. */
const char *web_view_playback_label(player_playback_state_t state,
                                    device_language_t language);
const char *web_view_source_label(audio_source_t source, device_language_t language);

uint32_t web_view_snapshot_changes(const player_snapshot_t *previous,
                                   const player_snapshot_t *current);

#ifdef __cplusplus
}
#endif
