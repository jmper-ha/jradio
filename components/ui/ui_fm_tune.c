#include "ui_fm_tune.h"

#include <stddef.h>
#include <string.h>

void ui_fm_tune_reset(ui_fm_tune_t *tune)
{
    if (tune != NULL) memset(tune, 0, sizeof(*tune));
}

static uint32_t ui_fm_tune_clamp(uint32_t khz)
{
    if (khz < UI_FM_TUNE_MIN_KHZ) return UI_FM_TUNE_MIN_KHZ;
    if (khz > UI_FM_TUNE_MAX_KHZ) return UI_FM_TUNE_MAX_KHZ;
    return khz;
}

bool ui_fm_tune_begin(ui_fm_tune_t *tune, uint32_t khz, uint32_t now_ms)
{
    if (tune == NULL || khz == 0U) return false;
    ui_fm_tune_reset(tune);
    tune->active = true;
    tune->start_khz = ui_fm_tune_clamp(khz);
    tune->khz = tune->start_khz;
    // Already where the tuner is: nothing to send until the knob moves.
    tune->sent_khz = tune->khz;
    tune->sent_ms = now_ms;
    tune->touched_ms = now_ms;
    return true;
}

bool ui_fm_tune_move(ui_fm_tune_t *tune, int direction, uint32_t now_ms)
{
    if (tune == NULL || !tune->active || direction == 0) return false;
    tune->touched_ms = now_ms;
    uint32_t next = tune->khz;
    if (direction > 0) {
        next = tune->khz + UI_FM_TUNE_STEP_KHZ;
    } else if (tune->khz >= UI_FM_TUNE_STEP_KHZ) {
        next = tune->khz - UI_FM_TUNE_STEP_KHZ;
    }
    next = ui_fm_tune_clamp(next);
    if (next == tune->khz) return false;
    tune->khz = next;
    return true;
}

void ui_fm_tune_follow(ui_fm_tune_t *tune, uint32_t khz, uint32_t now_ms)
{
    if (tune == NULL || !tune->active || khz == 0U) return;
    tune->khz = ui_fm_tune_clamp(khz);
    // The tuner is already there; nothing is owed to it.
    tune->sent_khz = tune->khz;
    tune->touched_ms = now_ms;
}

bool ui_fm_tune_due(ui_fm_tune_t *tune, uint32_t now_ms, uint32_t *khz)
{
    if (tune == NULL || !tune->active || tune->khz == tune->sent_khz) return false;
    if ((uint32_t)(now_ms - tune->sent_ms) < UI_FM_TUNE_SEND_MS) return false;
    tune->sent_khz = tune->khz;
    tune->sent_ms = now_ms;
    if (khz != NULL) *khz = tune->khz;
    return true;
}

uint32_t ui_fm_tune_keep(ui_fm_tune_t *tune)
{
    if (tune == NULL || !tune->active) return 0U;
    const uint32_t khz = tune->khz;
    ui_fm_tune_reset(tune);
    return khz;
}

uint32_t ui_fm_tune_cancel(ui_fm_tune_t *tune)
{
    if (tune == NULL || !tune->active) return 0U;
    const uint32_t khz = tune->start_khz;
    ui_fm_tune_reset(tune);
    return khz;
}

bool ui_fm_tune_idle(const ui_fm_tune_t *tune, uint32_t now_ms)
{
    return tune != NULL && tune->active &&
           (uint32_t)(now_ms - tune->touched_ms) >= UI_FM_TUNE_IDLE_MS;
}

bool ui_fm_tune_is_active(const ui_fm_tune_t *tune)
{
    return tune != NULL && tune->active;
}

uint32_t ui_fm_tune_khz(const ui_fm_tune_t *tune)
{
    return tune != NULL && tune->active ? tune->khz : 0U;
}
