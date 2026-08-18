#include "ui_status_bar.h"

#include <stdio.h>

uint8_t ui_status_wifi_bars(bool valid, int8_t rssi_dbm)
{
    if (!valid) return 0U;
    if (rssi_dbm >= -55) return 4U;
    if (rssi_dbm >= -67) return 3U;
    if (rssi_dbm >= -78) return 2U;
    return 1U;
}

bool ui_volume_commit_due(bool pending, uint32_t changed_ms, uint32_t now_ms,
                          uint32_t settle_ms)
{
    if (!pending) return false;
    return (uint32_t)(now_ms - changed_ms) >= settle_ms;
}

void ui_status_clock_text(char *text, size_t text_size, bool valid, int hour, int minute)
{
    if (text == NULL || text_size == 0U) return;
    if (!valid || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        snprintf(text, text_size, "--:--");
        return;
    }
    snprintf(text, text_size, "%02d:%02d", hour, minute);
}
