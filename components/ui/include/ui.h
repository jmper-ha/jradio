#pragma once

#include <stdbool.h>

#include "audio_source.h"
#include "board_input.h"
#include "esp_err.h"

esp_err_t ui_init(void);
bool ui_post_input(board_input_action_t action);
/* The web page's source tab: done as the remote's source key, the source
 * resuming where it was left, but not through the input queue - a key there
 * only wakes the screensaver, and the page's visitor is not looking at the
 * panel. Any task; the screen picks it up on its next pass. */
bool ui_request_source(audio_source_t source);
