#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ir_decode.h"

/* The infrared receiver on IR_RECEIVER_GPIO: the RMT peripheral catches the
 * pulse train in the background, a small task decodes it and hands the key
 * to whoever registered for it. On a board without the pin every call here
 * is a no-op that says so. */

typedef void (*ir_receiver_listener_t)(const ir_code_t *code, void *context);

esp_err_t ir_receiver_init(void);
/* One listener; a second call replaces the first. Called from the receiver's
 * own task, never from an interrupt. */
void ir_receiver_set_listener(ir_receiver_listener_t listener, void *context);
