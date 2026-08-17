#include "radio_prebuffer.h"

void radio_prebuffer_config_init(radio_prebuffer_config_t *config, size_t capacity,
                                 size_t chunk)
{
    if (config == NULL) return;
    config->capacity = capacity;
    /* Half the buffer: enough cushion to ride out a stall without the loop
     * spending its life reading, and it leaves room for the decoder to keep
     * consuming while a refill is in flight. */
    config->target = capacity / 2U;
    /* Roughly one chunk of slack. Below this the next decode call may find
     * nothing, so the pass is allowed to block. */
    config->critical = chunk;
    /* Well under the ~93 ms of I2S DMA: a pass that blocks longer than this
     * risks draining the buffer it is trying to protect. */
    config->normal_budget_ms = 0U;
    config->urgent_budget_ms = 30U;
}

radio_prebuffer_plan_t radio_prebuffer_plan(const radio_prebuffer_config_t *config,
                                            size_t available, bool need_input)
{
    radio_prebuffer_plan_t plan = {false, 0U, 0U};
    if (config == NULL || config->capacity == 0U) return plan;
    if (available >= config->capacity) return plan;

    const size_t room = config->capacity - available;
    const bool urgent = need_input || available <= config->critical;
    if (!urgent && available >= config->target) {
        /* Comfortable: nothing to do this pass. Reading anyway would only add
         * latency between decode calls. */
        return plan;
    }

    plan.should_read = true;
    plan.max_bytes = room;
    /* Only an empty-ish buffer justifies waiting on the socket. Otherwise take
     * whatever has already arrived and get back to decoding - that is what
     * lets the backlog grow on a fast link without ever stalling the DAC. */
    plan.budget_ms = urgent ? config->urgent_budget_ms : config->normal_budget_ms;
    return plan;
}

uint32_t radio_prebuffer_millis(size_t available, uint32_t bitrate_kbps)
{
    if (bitrate_kbps == 0U) return 0U;
    /* bytes * 8 / (kbps * 1000) seconds -> * 1000 for ms, i.e. bytes*8/kbps. */
    return (uint32_t)((uint64_t)available * 8U / bitrate_kbps);
}

uint8_t radio_prebuffer_percent(size_t available, size_t capacity)
{
    if (capacity == 0U) return 0U;
    if (available >= capacity) return 100U;
    if (available == 0U) return 0U;
    const uint8_t percent = (uint8_t)((available * 100U + capacity / 2U) / capacity);
    return percent == 0U ? 1U : percent;
}
