#pragma once

#include <stddef.h>
#include <stdint.h>

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

const char *web_view_playback_name(player_playback_state_t state);
const char *web_view_playback_label(player_playback_state_t state);
const char *web_view_source_name(audio_source_t source);
const char *web_view_source_label(audio_source_t source);

uint32_t web_view_snapshot_changes(const player_snapshot_t *previous,
                                   const player_snapshot_t *current);

#ifdef __cplusplus
}
#endif
