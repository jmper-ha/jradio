#pragma once

#include <stddef.h>

#include "player_control_types.h"
#include "wifi_settings.h"

#define WEB_PROTOCOL_FRAME_MAX 512U
#define WEB_PROTOCOL_REQUEST_ID_MAX 32U
#define WEB_PROTOCOL_EVENT_MAX 4096U

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEB_PROTOCOL_OK = 0,
    WEB_PROTOCOL_INVALID,
    WEB_PROTOCOL_TOO_LARGE,
} web_protocol_result_t;

typedef enum {
    WEB_COMMAND_PLAYER = 0,
    WEB_COMMAND_WIFI_SAVE,
} web_command_kind_t;

typedef struct {
    web_command_kind_t kind;
    char request_id[WEB_PROTOCOL_REQUEST_ID_MAX + 1U];
    player_command_t player;
    wifi_network_t wifi;
} web_command_t;

web_protocol_result_t web_protocol_parse_command(const char *frame,
                                                  size_t frame_length,
                                                  web_command_t *command);
void web_protocol_clear_command(web_command_t *command);

#ifdef __cplusplus
}
#endif
