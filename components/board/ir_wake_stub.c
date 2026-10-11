#include "ir_wake_stub.h"

#include "soc/soc_caps.h"

#include <string.h>

#include "board_config.h"
#include "driver/rtc_io.h"
#include "soc/gpio_periph.h"

#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_wake_stub.h"
#include "hal/rtc_cntl_ll.h"
#include "soc/gpio_reg.h"
#include "soc/io_mux_reg.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_channel.h"

static const char *TAG = "ir_wake";


#define IR_WAKE_MAGIC 0x4952574BU /* "IRWK" */
#define IR_WAKE_BITS 32U
/* Bounds on every wait, so a stuck line costs the boot a few milliseconds and
 * not for ever: the leader mark is 9 ms, its space 4.5 ms, a one's space
 * 1.7 ms. */
#define IR_WAKE_LEADER_MAX_US 12000U
#define IR_WAKE_SPACE_MAX_US 6000U
#define IR_WAKE_BIT_MAX_US 2500U

typedef enum {
    IR_WAKE_NOTHING = 0,
    IR_WAKE_FRAME,     /* a leader and 32 bits were read */
    IR_WAKE_REPEAT,    /* a repeat frame: the key held from before the sleep */
    IR_WAKE_GARBAGE,   /* something was on the line but not a frame */
} ir_wake_kind_t;

/* The line has to stay high this long before the stub sleeps again: the
 * receiver's noise came in bursts of pulses a hundred-odd microseconds apart,
 * and a sleep entered inside one wakes on the next pulse. NEC's own gap
 * between frames is some 40 ms. */
#define IR_WAKE_QUIET_US 25000U
/* And a line that will not go quiet in this long is let through to the boot:
 * the stub is not the place to spend a night spinning. */
#define IR_WAKE_SETTLE_MAX_US 400000U
// A low this long, met while waiting for quiet, is a leader worth reading.
#define IR_WAKE_LEADER_MIN_US 7000U
/* The alarm's timer is given back its own target when the stub sleeps again;
 * a target closer than this (RTC slow-clock ticks, 75 ms or so on the
 * internal 136 kHz RC) is let through to the boot instead. */
#define IR_WAKE_TIMER_MARGIN_TICKS 10000U

/* What the stub leaves for the boot. RTC memory keeps it through the wake;
 * the magic says the stub ran on this wake and not some earlier one. */
static RTC_DATA_ATTR uint32_t s_magic;
static RTC_DATA_ATTR uint32_t s_kind;
static RTC_DATA_ATTR uint32_t s_bits;
static RTC_DATA_ATTR uint32_t s_leader_space_us;
static RTC_DATA_ATTR uint32_t s_stub_at_us;
/* The receiver's pin, its RTC channel - what the RTC GPIO registers index
 * by - and its IO_MUX register, worked out by ir_wake_stub_arm() out of the
 * wiring. The stub cannot do that itself: the map from GPIO to channel and
 * to register is a table in flash, and flash is not up while it runs. */
static RTC_DATA_ATTR uint32_t s_pin;
static RTC_DATA_ATTR uint32_t s_rtc_channel;
static RTC_DATA_ATTR uint32_t s_io_mux_reg;
// The Power key's NEC frame, and whether there is one to compare with.
static RTC_DATA_ATTR uint32_t s_power_bits;
static RTC_DATA_ATTR uint32_t s_power_known;
static RTC_DATA_ATTR uint32_t s_timer;
static RTC_DATA_ATTR uint32_t s_slept_through;

/* Read through the digital GPIO, not the RTC one: sampled side by side on
 * the bench, the RTC pad read high for the whole of a frame - the ROM hands
 * the pad back to the digital mux on wake - while the digital input, once
 * its input enable is set here, followed the line exactly. */
static void ir_wake_stub(void);

static RTC_IRAM_ATTR bool pin_high(void)
{
    return (REG_READ(GPIO_IN_REG) >> s_pin) & 1U;
}

/* Spins until the line reads `high`, and answers how long that took, or the
 * bound when it did not. The cycle counter is the clock: the CPU runs on the
 * crystal here, and the ROM knows how many ticks that is per microsecond. */
static RTC_IRAM_ATTR uint32_t wait_for(bool high, uint32_t max_us, uint32_t ticks_per_us)
{
    const uint32_t started = esp_cpu_get_cycle_count();
    const uint32_t max_ticks = max_us * ticks_per_us;
    while (pin_high() != high) {
        const uint32_t elapsed = esp_cpu_get_cycle_count() - started;
        if (elapsed >= max_ticks) return max_us;
    }
    return (esp_cpu_get_cycle_count() - started) / ticks_per_us;
}

/* What follows the end of a leader mark: its space, then for a frame the 32
 * bits. Leaves the kind and the bits in RTC memory. */
static RTC_IRAM_ATTR void read_after_leader(uint32_t ticks_per_us)
{
    s_bits = 0U;
    const uint32_t space = wait_for(false, IR_WAKE_SPACE_MAX_US, ticks_per_us);
    s_leader_space_us = space;
    if (space >= 1700U && space <= 2800U) {
        s_kind = IR_WAKE_REPEAT;
        return;
    }
    if (space < 3500U || space > 5500U) {
        s_kind = IR_WAKE_GARBAGE;
        return;
    }
    /* The 32 bits, least significant first: a 560 us mark each, then a space
     * of 560 us for a zero and 1690 us for a one. */
    uint32_t bits = 0U;
    for (uint32_t index = 0U; index < IR_WAKE_BITS; ++index) {
        const uint32_t mark = wait_for(true, IR_WAKE_BIT_MAX_US, ticks_per_us);
        if (mark < 250U || mark > 900U) {
            s_kind = IR_WAKE_GARBAGE;
            return;
        }
        const uint32_t bit_space = wait_for(false, IR_WAKE_BIT_MAX_US, ticks_per_us);
        if (bit_space > 1100U) bits |= 1UL << index;
    }
    s_bits = bits;
    s_kind = IR_WAKE_FRAME;
}

static RTC_IRAM_ATTR bool is_power(void)
{
    return s_kind == IR_WAKE_FRAME && s_bits == s_power_bits;
}

/* Waits for the line to stay high for IR_WAKE_QUIET_US. True when it did;
 * false when a Power frame came meanwhile - noise and a real press can share
 * a wake - or the line never went quiet, and either way the boot goes on. */
static RTC_IRAM_ATTR bool settle(uint32_t ticks_per_us)
{
    const uint32_t started = esp_cpu_get_cycle_count();
    uint32_t quiet_from = started;
    while ((esp_cpu_get_cycle_count() - started) / ticks_per_us < IR_WAKE_SETTLE_MAX_US) {
        if (pin_high()) {
            if ((esp_cpu_get_cycle_count() - quiet_from) / ticks_per_us >= IR_WAKE_QUIET_US) {
                return true;
            }
            continue;
        }
        const uint32_t mark = wait_for(true, IR_WAKE_LEADER_MAX_US, ticks_per_us);
        if (mark >= IR_WAKE_LEADER_MIN_US) {
            read_after_leader(ticks_per_us);
            if (is_power()) return false;
        }
        quiet_from = esp_cpu_get_cycle_count();
    }
    return false;
}

/* Back to sleep from the stub, the app never started: the same wake sources
 * as the sleep before, and the alarm's timer on its own target - unless that
 * target is too close, when the boot had better go ahead. */
static RTC_IRAM_ATTR bool sleep_again(void)
{
    if (s_timer != 0U) {
        const uint64_t target = (uint64_t)REG_READ(RTC_CNTL_SLP_TIMER0_REG) |
                                ((uint64_t)(REG_READ(RTC_CNTL_SLP_TIMER1_REG) & 0xFFFFU) << 32);
        const uint64_t now = rtc_cntl_ll_get_rtc_time();
        if (target <= now + IR_WAKE_TIMER_MARGIN_TICKS) return false;
        rtc_cntl_ll_set_wakeup_timer(target);
    }
    ++s_slept_through;
    s_magic = 0U;
    rtc_cntl_ll_ext1_clear_wakeup_status();
    esp_wake_stub_sleep(&ir_wake_stub);
    return true;  // not reached
}

static RTC_IRAM_ATTR void ir_wake_stub(void)
{
    const uint32_t ticks_per_us = esp_rom_get_cpu_ticks_per_us();
    s_stub_at_us = esp_cpu_get_cycle_count() / ticks_per_us;
    s_kind = IR_WAKE_NOTHING;
    s_bits = 0U;
    s_leader_space_us = 0U;
    s_magic = IR_WAKE_MAGIC;

    /* Only the receiver's wake is worth the wait; the button's and the
     * timer's boot on at once. */
    const uint32_t woke = REG_GET_FIELD(RTC_CNTL_EXT_WAKEUP1_STATUS_REG, RTC_CNTL_EXT_WAKEUP1_STATUS);
    if ((woke & (1U << s_rtc_channel)) == 0U) {
        esp_default_wake_deep_sleep();
        return;
    }

    /* The pad as a plain input. The stub arrives about 3 ms into the 9 ms
     * leader mark (measured), so what follows is the rest of that mark, then
     * its space: 4.5 ms before a frame, 2.25 ms before a repeat. */
    REG_SET_BIT(s_io_mux_reg, FUN_IE);
    (void)wait_for(true, IR_WAKE_LEADER_MAX_US, ticks_per_us);
    read_after_leader(ticks_per_us);

    /* Not Power, with Power known: noise, another key, a held key's repeats.
     * Nothing in the app would do more than listen for three seconds and
     * sleep again, so it is not started. */
    if (s_power_known != 0U && !is_power() && settle(ticks_per_us)) {
        (void)sleep_again();
    }
    esp_default_wake_deep_sleep();
}

void ir_wake_stub_arm(const uint32_t *power_bits, bool timer)
{
    s_magic = 0U;
    s_kind = IR_WAKE_NOTHING;
    s_power_known = power_bits != NULL ? 1U : 0U;
    s_power_bits = power_bits != NULL ? *power_bits : 0U;
    s_timer = timer ? 1U : 0U;
    const int8_t pin = board_config_get()->ir_receiver;
    const int channel = pin >= 0 ? rtc_io_number_get((gpio_num_t)pin) : -1;
    if (channel < 0) return;  // not on an RTC pin: nothing can wake the chip from it
    s_pin = (uint32_t)pin;
    s_rtc_channel = (uint32_t)channel;
    s_io_mux_reg = GPIO_PIN_MUX_REG[pin];
    /* The ROM prints its banner on the console before it runs the stub, and
     * ninety characters at 115200 baud are 7.7 ms - measured, and most of the
     * 9 ms leader the stub was meant to arrive inside of. Silenced for every
     * wake from deep sleep; the app logs the reset reason itself. */
    esp_deep_sleep_disable_rom_logging();
    esp_set_deep_sleep_wake_stub(&ir_wake_stub);
}

bool ir_wake_stub_take(ir_code_t *code)
{
    if (s_magic != IR_WAKE_MAGIC) return false;
    s_magic = 0U;
    const uint32_t kind = s_kind;
    ESP_LOGI(TAG, "stub ran %u us after the wake: %s, leader space %u us",
             (unsigned)s_stub_at_us,
             kind == IR_WAKE_FRAME ? "a frame" : kind == IR_WAKE_REPEAT ? "a repeat"
             : kind == IR_WAKE_GARBAGE ? "not a frame" : "nothing",
             (unsigned)s_leader_space_us);
    if (kind != IR_WAKE_FRAME || code == NULL) return false;
    /* Through the same decoder as a frame off the RMT, rebuilt as nominal
     * pulses, so an extended address or a bad complement is judged the same
     * way here as when the board is awake. */
    ir_pulse_t pulses[2U + IR_WAKE_BITS];
    pulses[0] = (ir_pulse_t){.mark_us = 9000U, .space_us = 4500U};
    for (uint32_t index = 0U; index < IR_WAKE_BITS; ++index) {
        const bool one = (s_bits >> index) & 1U;
        pulses[1U + index] = (ir_pulse_t){.mark_us = 562U, .space_us = one ? 1687U : 562U};
    }
    pulses[1U + IR_WAKE_BITS] = (ir_pulse_t){.mark_us = 562U, .space_us = 0U};
    *code = ir_decode(pulses, 2U + IR_WAKE_BITS);
    return code->kind != IR_CODE_NONE;
}

uint32_t ir_wake_stub_slept_through(void)
{
    const uint32_t count = s_slept_through;
    s_slept_through = 0U;
    return count;
}
