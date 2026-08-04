#pragma once

#include <stdbool.h>

#include "audio_source.h"
#include "board_input.h"
#include "esp_err.h"

esp_err_t ui_init(audio_source_manager_t *source_manager);
bool ui_post_input(board_input_action_t action);
