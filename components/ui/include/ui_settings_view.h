#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Text of the settings screen, derived from device state.
 *
 * The screen is read-only: it answers "what is this box doing and where do I
 * reach it", which on a device with one encoder and no keyboard is the only
 * settings question that can be usefully answered on the panel itself.
 * Everything editable lives in the web UI - and the IP shown here is how the
 * user finds it, which is the main reason this screen exists at all.
 */

typedef struct {
    bool connected;
    const char *ssid;
    const char *ipv4;
    bool rssi_valid;
    int8_t rssi_dbm;
    /* Address of the web UI is the device's own IP, so this only says whether
     * the server came up. */
    bool web_ready;
} ui_settings_info_t;

typedef enum {
    UI_SETTINGS_ROW_WIFI = 0,
    UI_SETTINGS_ROW_ADDRESS,
    UI_SETTINGS_ROW_SIGNAL,
    UI_SETTINGS_ROW_COUNT,
} ui_settings_row_t;

/* Writes the row's text, always NUL-terminated. An unknown row yields an empty
 * string rather than being rejected, so a caller looping over the rows needs no
 * special case. */
void ui_settings_row_text(ui_settings_row_t row, const ui_settings_info_t *info,
                          char *out, size_t out_size);
