#pragma once

#include <stdbool.h>

#include "board_input.h"
#include "esp_err.h"

esp_err_t ui_init(void);
bool ui_post_input(board_input_action_t action);
