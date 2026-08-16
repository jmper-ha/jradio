#include "ui_settings_view.h"

#include <stdio.h>

static const char *text_or(const char *value, const char *fallback)
{
    return value == NULL || value[0] == '\0' ? fallback : value;
}

void ui_settings_row_text(ui_settings_row_t row, const ui_settings_info_t *info,
                          char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';
    if (info == NULL) return;

    switch (row) {
    case UI_SETTINGS_ROW_WIFI:
        if (!info->connected) {
            snprintf(out, out_size, "Wi-Fi: нет подключения");
            return;
        }
        snprintf(out, out_size, "Wi-Fi: %s", text_or(info->ssid, "?"));
        return;
    case UI_SETTINGS_ROW_ADDRESS:
        /* Without an address there is no web UI to open, so say that plainly
         * rather than print a blank or a stale one. */
        if (!info->connected || !info->web_ready ||
            text_or(info->ipv4, "")[0] == '\0') {
            snprintf(out, out_size, "Веб-интерфейс: недоступен");
            return;
        }
        snprintf(out, out_size, "Веб-интерфейс: http://%s", info->ipv4);
        return;
    case UI_SETTINGS_ROW_SIGNAL:
        if (!info->connected || !info->rssi_valid) {
            snprintf(out, out_size, "Сигнал: --");
            return;
        }
        snprintf(out, out_size, "Сигнал: %d дБм", (int)info->rssi_dbm);
        return;
    case UI_SETTINGS_ROW_COUNT:
    default:
        return;
    }
}
