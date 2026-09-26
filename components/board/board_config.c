#include "board_config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_parts.h"

/* The file's keys, in flasher/hardware_core.js's FIELDS order, which is also
 * the order board_config_to_csv() writes them in. Kept as a table so a key
 * added there is one line here. */
typedef enum {
    KIND_IGNORED = 0,  // decided by the chip or the firmware: read past
    KIND_TEXT,
    KIND_PIN,          // a GPIO, required
    KIND_OPT_PIN,      // a GPIO or none
    KIND_FIXED_PIN,    // a pin the chip decides: its number or none
    KIND_CHOICE,
    KIND_BOOL,
} field_kind_t;

typedef enum {
    DEV_BOARD = 0,
    DEV_SPI2,
    DEV_SPI3,
    DEV_I2S0,
    DEV_UART1,
    DEV_TFT,
    DEV_ENCODER,
    DEV_BUTTONS,
    DEV_IR,
    DEV_DAC,
    DEV_AMP,
    DEV_POWER,
    DEV_USB,
    DEV_SD,
    DEV_BLUETOOTH,
    DEV_FEATURES,
} device_t;

typedef struct {
    const char *name;
    uint8_t id;
} choice_t;

#define CHOICES_MAX 10

typedef struct {
    const char *key;
    field_kind_t kind;
    device_t device;
    size_t offset;
    bool reset_ok;   // takes "rst", the module's reset pad
    // What IGNORED keys are written as.
    const char *fixed_text;
    choice_t choices[CHOICES_MAX];
} field_t;

#define AT(member) offsetof(board_config_t, member)

static const field_t k_fields[] = {
    {"board_format", KIND_IGNORED, DEV_BOARD, 0, false, "1", {{NULL, 0}}},
    {"board_name", KIND_TEXT, DEV_BOARD, AT(board_name), false, NULL, {{NULL, 0}}},
    {"module", KIND_IGNORED, DEV_BOARD, 0, false, "esp32s3_n16r8", {{NULL, 0}}},
    {"display", KIND_CHOICE, DEV_BOARD, AT(display), false, NULL,
     {{"st7796s_480_320", DISPLAY_ST7796S_480_320},
      {"st7796s_320_480", DISPLAY_ST7796S_320_480},
      {"ili9488_480_320", DISPLAY_ILI9488_480_320},
      {"ili9488_320_480", DISPLAY_ILI9488_320_480},
      {"ili9341_320_240", DISPLAY_ILI9341_320_240},
      {"ili9341_240_320", DISPLAY_ILI9341_240_320},
      {"st7789_320_240", DISPLAY_ST7789_320_240},
      {"st7789_240_320", DISPLAY_ST7789_240_320},
      {"st7789_320_170", DISPLAY_ST7789_320_170},
      {NULL, 0}}},
    // SPI2's pins are the chip's own; a file cannot move them.
    {"spi2_sclk", KIND_IGNORED, DEV_SPI2, AT(spi2_sclk), false, "12", {{NULL, 0}}},
    {"spi2_mosi", KIND_IGNORED, DEV_SPI2, AT(spi2_mosi), false, "11", {{NULL, 0}}},
    {"spi3_sclk", KIND_OPT_PIN, DEV_SPI3, AT(spi3_sclk), false, NULL, {{NULL, 0}}},
    {"spi3_mosi", KIND_OPT_PIN, DEV_SPI3, AT(spi3_mosi), false, NULL, {{NULL, 0}}},
    {"spi3_miso", KIND_OPT_PIN, DEV_SPI3, AT(spi3_miso), false, NULL, {{NULL, 0}}},
    {"i2s0_bclk", KIND_OPT_PIN, DEV_I2S0, AT(i2s0_bclk), false, NULL, {{NULL, 0}}},
    {"i2s0_lrck", KIND_OPT_PIN, DEV_I2S0, AT(i2s0_lrck), false, NULL, {{NULL, 0}}},
    {"i2s0_dout", KIND_OPT_PIN, DEV_I2S0, AT(i2s0_dout), false, NULL, {{NULL, 0}}},
    {"uart1_tx", KIND_OPT_PIN, DEV_UART1, AT(uart1_tx), false, NULL, {{NULL, 0}}},
    {"uart1_rx", KIND_OPT_PIN, DEV_UART1, AT(uart1_rx), false, NULL, {{NULL, 0}}},
    {"tft_spi", KIND_IGNORED, DEV_TFT, 0, false, "2", {{NULL, 0}}},
    {"tft_cs", KIND_PIN, DEV_TFT, AT(tft_cs), false, NULL, {{NULL, 0}}},
    {"tft_dc", KIND_PIN, DEV_TFT, AT(tft_dc), false, NULL, {{NULL, 0}}},
    {"tft_reset", KIND_OPT_PIN, DEV_TFT, AT(tft_reset), true, NULL, {{NULL, 0}}},
    {"tft_backlight", KIND_PIN, DEV_TFT, AT(tft_backlight), false, NULL, {{NULL, 0}}},
    {"encoder_right", KIND_PIN, DEV_ENCODER, AT(encoder_right), false, NULL, {{NULL, 0}}},
    {"encoder_left", KIND_PIN, DEV_ENCODER, AT(encoder_left), false, NULL, {{NULL, 0}}},
    {"encoder_button", KIND_PIN, DEV_ENCODER, AT(encoder_button), false, NULL, {{NULL, 0}}},
    {"encoder_pullups", KIND_BOOL, DEV_ENCODER, AT(encoder_pullups), false, NULL, {{NULL, 0}}},
    {"button_sleep", KIND_OPT_PIN, DEV_BUTTONS, AT(button_sleep), false, NULL, {{NULL, 0}}},
    {"button_quick_menu", KIND_OPT_PIN, DEV_BUTTONS, AT(button_quick_menu), false, NULL,
     {{NULL, 0}}},
    {"button_prev", KIND_OPT_PIN, DEV_BUTTONS, AT(button_prev), false, NULL, {{NULL, 0}}},
    {"button_next", KIND_OPT_PIN, DEV_BUTTONS, AT(button_next), false, NULL, {{NULL, 0}}},
    {"buttons_pullups", KIND_BOOL, DEV_BUTTONS, AT(buttons_pullups), false, NULL, {{NULL, 0}}},
    {"ir_receiver", KIND_OPT_PIN, DEV_IR, AT(ir_receiver), false, NULL, {{NULL, 0}}},
    {"dac", KIND_CHOICE, DEV_DAC, AT(dac), false, NULL,
     // The UDA1334A has a driver but has not been tried on a board yet.
     {{"pcm5102", DAC_PCM5102}, {NULL, 0}}},
    {"dac_i2s", KIND_CHOICE, DEV_DAC, AT(dac_i2s), false, NULL, {{"0", 0}, {NULL, 0}}},
    {"dac_mute", KIND_OPT_PIN, DEV_DAC, AT(dac_mute), false, NULL, {{NULL, 0}}},
    {"amp_enable", KIND_OPT_PIN, DEV_AMP, AT(amp_enable), false, NULL, {{NULL, 0}}},
    {"amp_on_level", KIND_BOOL, DEV_AMP, AT(amp_on_level), false, NULL, {{NULL, 0}}},
    {"peripheral_power", KIND_OPT_PIN, DEV_POWER, AT(peripheral_power), false, NULL,
     {{NULL, 0}}},
    {"peripheral_power_on_level", KIND_BOOL, DEV_POWER, AT(peripheral_power_on_level), false,
     NULL, {{NULL, 0}}},
    {"usb_dp", KIND_FIXED_PIN, DEV_USB, AT(usb_dp), false, NULL, {{NULL, 0}}},
    {"usb_dm", KIND_FIXED_PIN, DEV_USB, AT(usb_dm), false, NULL, {{NULL, 0}}},
    {"sd_spi", KIND_CHOICE, DEV_SD, AT(sd_spi), false, NULL, {{"3", 3}, {NULL, 0}}},
    {"sd_cs", KIND_OPT_PIN, DEV_SD, AT(sd_cs), false, NULL, {{NULL, 0}}},
    {"bluetooth", KIND_CHOICE, DEV_BLUETOOTH, AT(bluetooth), false, NULL,
     {{"none", BLUETOOTH_NONE}, {"jradio_bt", BLUETOOTH_JRADIO_BT}, {NULL, 0}}},
    {"bt_uart", KIND_CHOICE, DEV_BLUETOOTH, AT(bt_uart), false, NULL, {{"1", 1}, {NULL, 0}}},
    {"bt_i2s", KIND_CHOICE, DEV_BLUETOOTH, AT(bt_i2s), false, NULL, {{"0", 0}, {NULL, 0}}},
    {"yandex_music", KIND_BOOL, DEV_FEATURES, AT(yandex_music), false, NULL, {{NULL, 0}}},
    {"dlna", KIND_BOOL, DEV_FEATURES, AT(dlna), false, NULL, {{NULL, 0}}},
};

#define FIELD_COUNT (sizeof(k_fields) / sizeof(k_fields[0]))

// The pins of the chip that reach the DevKitC-1's headers: 22-34 are the
// flash's and the PSRAM's and are not brought out.
static bool on_header(int gpio)
{
    return (gpio >= 0 && gpio <= 21) || (gpio >= 35 && gpio <= 48);
}

#define RTC_GPIO_MAX 21
#define USB_DP_PIN 20
#define USB_DM_PIN 19
#define SPI2_IOMUX_CS 10

static int8_t *pin_at(const board_config_t *config, const field_t *field)
{
    return (int8_t *)((uintptr_t)config + field->offset);
}

static uint8_t *byte_at(const board_config_t *config, const field_t *field)
{
    return (uint8_t *)((uintptr_t)config + field->offset);
}

static const field_t *field_named(const char *key, size_t length)
{
    for (size_t i = 0U; i < FIELD_COUNT; ++i) {
        if (strlen(k_fields[i].key) == length && memcmp(k_fields[i].key, key, length) == 0) {
            return &k_fields[i];
        }
    }
    return NULL;
}

static bool is_pin_kind(field_kind_t kind)
{
    return kind == KIND_PIN || kind == KIND_OPT_PIN || kind == KIND_FIXED_PIN;
}

void board_config_clear(board_config_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    for (size_t i = 0U; i < FIELD_COUNT; ++i) {
        if (is_pin_kind(k_fields[i].kind)) *pin_at(config, &k_fields[i]) = BOARD_PIN_NONE;
    }
    config->spi2_sclk = 12;
    config->spi2_mosi = 11;
    config->display = DISPLAY_NONE;
    config->dac = DAC_PCM5102;
    config->bluetooth = BLUETOOTH_NONE;
    config->sd_spi = 3U;
    config->dac_i2s = 0U;
    config->bt_uart = 1U;
    config->bt_i2s = 0U;
}

/* A whole decimal integer and nothing else. Out of int8_t's range reads as
 * a pin that is on no header - which the rules then say, rather than the
 * number wrapping round into one that is. */
static bool parse_integer(const char *text, size_t length, long *out)
{
    if (length == 0U || length > 12U) return false;
    char buffer[16];
    memcpy(buffer, text, length);
    buffer[length] = '\0';
    char *end = NULL;
    const long value = strtol(buffer, &end, 10);
    if (end == buffer || *end != '\0') return false;
    *out = value;
    return true;
}

static bool equals(const char *text, size_t length, const char *word)
{
    return strlen(word) == length && memcmp(text, word, length) == 0;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static void trim(const char **text, size_t *length)
{
    while (*length > 0U && is_space(**text)) {
        ++*text;
        --*length;
    }
    while (*length > 0U && is_space((*text)[*length - 1U])) --*length;
}

// False for a value that is not of the field's kind: the line is then bad.
static bool read_value(board_config_t *config, const field_t *field, const char *value,
                       size_t length)
{
    long number = 0;
    switch (field->kind) {
    case KIND_IGNORED:
        return true;
    case KIND_TEXT: {
        char *text = (char *)((uintptr_t)config + field->offset);
        const size_t kept = length < BOARD_CONFIG_NAME_MAX - 1U ? length : BOARD_CONFIG_NAME_MAX - 1U;
        memcpy(text, value, kept);
        text[kept] = '\0';
        return true;
    }
    case KIND_PIN:
    case KIND_OPT_PIN:
    case KIND_FIXED_PIN:
        if (length == 0U || equals(value, length, "none")) {
            *pin_at(config, field) = BOARD_PIN_NONE;
        } else if (field->reset_ok && equals(value, length, "rst")) {
            *pin_at(config, field) = BOARD_PIN_RESET;
        } else if (parse_integer(value, length, &number)) {
            *pin_at(config, field) = (number < 0 || number > INT8_MAX) ? INT8_MAX : (int8_t)number;
        } else {
            return false;
        }
        return true;
    case KIND_BOOL:
        if (!parse_integer(value, length, &number)) return false;
        *byte_at(config, field) = (number == 0 || number == 1) ? (uint8_t)number : BOARD_CONFIG_INVALID;
        return true;
    case KIND_CHOICE:
        *byte_at(config, field) = BOARD_CONFIG_INVALID;
        for (size_t i = 0U; i < CHOICES_MAX && field->choices[i].name != NULL; ++i) {
            if (equals(value, length, field->choices[i].name)) {
                *byte_at(config, field) = field->choices[i].id;
                break;
            }
        }
        return true;
    }
    return false;
}

void board_config_parse_csv(const char *text, size_t length, board_config_t *config,
                            board_config_parse_result_t *result)
{
    board_config_parse_result_t local;
    if (result == NULL) result = &local;
    memset(result, 0, sizeof(*result));
    if (text == NULL || config == NULL) return;

    size_t line_number = 0U;
    size_t at = 0U;
    while (at < length) {
        size_t end = at;
        while (end < length && text[end] != '\n') ++end;
        ++line_number;
        const char *line = text + at;
        size_t line_length = end - at;
        at = end + 1U;
        trim(&line, &line_length);
        if (line_length == 0U || line[0] == '#') continue;

        const char *comma = memchr(line, ',', line_length);
        bool good = comma != NULL;
        if (good) {
            const char *key = line;
            size_t key_length = (size_t)(comma - line);
            const char *value = comma + 1;
            size_t value_length = line_length - key_length - 1U;
            trim(&key, &key_length);
            trim(&value, &value_length);
            const field_t *field = field_named(key, key_length);
            if (field == NULL) {
                ++result->unknown_keys;
                continue;
            }
            good = read_value(config, field, value, value_length);
        }
        if (!good) {
            if (result->bad_lines == 0U) result->first_bad_line = line_number;
            ++result->bad_lines;
        }
    }
}

/* ---- The rules ------------------------------------------------------- */

static bool device_enabled(const board_config_t *config, device_t device)
{
    switch (device) {
    case DEV_IR: return config->ir_receiver != BOARD_PIN_NONE;
    case DEV_AMP: return config->amp_enable != BOARD_PIN_NONE;
    case DEV_POWER: return config->peripheral_power != BOARD_PIN_NONE;
    case DEV_USB: return config->usb_dp != BOARD_PIN_NONE;
    case DEV_SD: return config->sd_cs != BOARD_PIN_NONE;
    case DEV_BLUETOOTH: return config->bluetooth != BLUETOOTH_NONE;
    default: return true;
    }
}

/* A bus is in use while a device that is on the board names it; one nobody
 * is on holds no pins, so its numbers kept for later conflict with nothing. */
static bool bus_used(const board_config_t *config, device_t bus)
{
    const bool bt = device_enabled(config, DEV_BLUETOOTH);
    switch (bus) {
    case DEV_SPI2: return true;  // the display's, and the display is always there
    case DEV_SPI3: return device_enabled(config, DEV_SD) && config->sd_spi == 3U;
    case DEV_I2S0: return config->dac_i2s == 0U || (bt && config->bt_i2s == 0U);
    case DEV_UART1: return bt && config->bt_uart == 1U;
    default: return false;
    }
}

static bool is_bus(device_t device)
{
    return device == DEV_SPI2 || device == DEV_SPI3 || device == DEV_I2S0 || device == DEV_UART1;
}

/* Whether a pin field owns its pin right now. The SPI2 pins are not in the
 * table as pins - a file cannot move them - but they are signals all the
 * same, and are added by hand below. */
static bool signal_live(const board_config_t *config, const field_t *field)
{
    if (!is_pin_kind(field->kind)) return false;
    if (!device_enabled(config, field->device)) return false;
    if (is_bus(field->device) && !bus_used(config, field->device)) return false;
    return true;
}

static void add_issue(board_config_report_t *report, bool warning, board_issue_code_t code,
                      const char *key, const char *detail, int gpio)
{
    board_issue_t *list = warning ? report->warnings : report->errors;
    size_t *count = warning ? &report->warning_count : &report->error_count;
    if (*count >= BOARD_CONFIG_ISSUES_MAX) {
        ++report->dropped;
        return;
    }
    list[(*count)++] = (board_issue_t){.code = code, .key = key, .detail = detail, .gpio = gpio};
}

typedef struct {
    const char *key;
    device_t device;
    int gpio;  // -1 for none or the reset pad
    bool required;
} signal_t;

#define SIGNALS_MAX (FIELD_COUNT + 2U)

static size_t collect_signals(const board_config_t *config, signal_t *signals)
{
    size_t count = 0U;
    signals[count++] = (signal_t){"spi2_sclk", DEV_SPI2, config->spi2_sclk, false};
    signals[count++] = (signal_t){"spi2_mosi", DEV_SPI2, config->spi2_mosi, false};
    for (size_t i = 0U; i < FIELD_COUNT; ++i) {
        const field_t *field = &k_fields[i];
        if (!signal_live(config, field)) continue;
        const int8_t value = *pin_at(config, field);
        signals[count++] = (signal_t){field->key, field->device, value >= 0 ? value : -1,
                                      field->kind == KIND_PIN};
    }
    return count;
}

static const char *device_name(device_t device)
{
    switch (device) {
    case DEV_TFT: return "tft";
    case DEV_SD: return "sd";
    case DEV_DAC: return "dac";
    case DEV_BLUETOOTH: return "bluetooth";
    default: return "";
    }
}

/* What a device needs of its bus: the bus's pins by key. */
static void check_bus(const board_config_t *config, board_config_report_t *report,
                      device_t device, const char *const *keys, size_t count)
{
    for (size_t i = 0U; i < count; ++i) {
        const field_t *field = field_named(keys[i], strlen(keys[i]));
        if (field == NULL) continue;
        if (*pin_at(config, field) < 0) {
            add_issue(report, false, BOARD_ISSUE_BUS_UNWIRED, device_name(device), keys[i], -1);
        }
    }
}

void board_config_validate(const board_config_t *config, board_config_report_t *report)
{
    if (report == NULL) return;
    memset(report, 0, sizeof(*report));
    if (config == NULL) return;

    signal_t signals[SIGNALS_MAX];
    const size_t count = collect_signals(config, signals);

    /* One pin, one signal. A bus is one signal however many devices share
     * it, so sharing one is never a conflict - and a bus pin reused as a
     * chip select is. Reported once per pin. */
    for (size_t i = 0U; i < count; ++i) {
        if (signals[i].gpio < 0) continue;
        bool first = true;
        bool shared = false;
        for (size_t j = 0U; j < count; ++j) {
            if (j == i || signals[j].gpio != signals[i].gpio) continue;
            shared = true;
            if (j < i) first = false;
        }
        if (shared && first) {
            add_issue(report, false, BOARD_ISSUE_PIN_CONFLICT, signals[i].key, NULL,
                      signals[i].gpio);
        }
    }

    for (size_t i = 0U; i < count; ++i) {
        const signal_t *signal = &signals[i];
        const int gpio = signal->gpio;
        if (gpio < 0) {
            if (signal->required) {
                add_issue(report, false, BOARD_ISSUE_PIN_MISSING, signal->key, NULL, -1);
            }
            continue;
        }
        if (!on_header(gpio)) {
            add_issue(report, false, BOARD_ISSUE_PIN_NOT_ON_HEADER, signal->key, NULL, gpio);
            continue;
        }
        // The module is always the N16R8: octal PSRAM drives 35-37 itself.
        if (gpio >= 35 && gpio <= 37) {
            add_issue(report, false, BOARD_ISSUE_PIN_PSRAM, signal->key, NULL, gpio);
        } else if (gpio == 43 || gpio == 44) {
            add_issue(report, false, BOARD_ISSUE_PIN_CONSOLE, signal->key, NULL, gpio);
        } else if ((gpio == USB_DP_PIN || gpio == USB_DM_PIN) && signal->device != DEV_USB) {
            add_issue(report, false, BOARD_ISSUE_PIN_USB, signal->key, NULL, gpio);
        }
    }

    // The USB pair is the chip's, not a choice.
    if (config->usb_dp >= 0 && config->usb_dp != USB_DP_PIN) {
        add_issue(report, false, BOARD_ISSUE_USB_PIN_FIXED, "usb_dp", NULL, config->usb_dp);
    }
    if (config->usb_dm >= 0 && config->usb_dm != USB_DM_PIN) {
        add_issue(report, false, BOARD_ISSUE_USB_PIN_FIXED, "usb_dm", NULL, config->usb_dm);
    }

    static const char *const k_spi2[] = {"spi2_sclk", "spi2_mosi"};
    static const char *const k_spi3[] = {"spi3_sclk", "spi3_mosi", "spi3_miso"};
    static const char *const k_i2s0[] = {"i2s0_bclk", "i2s0_lrck", "i2s0_dout"};
    static const char *const k_uart1[] = {"uart1_tx", "uart1_rx"};
    (void)k_spi2;  // SPI2's pins are fixed and always there
    if (device_enabled(config, DEV_SD) && config->sd_spi == 3U) {
        check_bus(config, report, DEV_SD, k_spi3, 3U);
    }
    if (config->dac_i2s == 0U) check_bus(config, report, DEV_DAC, k_i2s0, 3U);
    if (device_enabled(config, DEV_BLUETOOTH)) {
        if (config->bt_uart == 1U) check_bus(config, report, DEV_BLUETOOTH, k_uart1, 2U);
        if (config->bt_i2s == 0U) check_bus(config, report, DEV_BLUETOOTH, k_i2s0, 3U);
    }

    // Only RTC pins can wake the chip.
    if (config->button_sleep >= 0 && config->button_sleep > RTC_GPIO_MAX) {
        add_issue(report, true, BOARD_ISSUE_SLEEP_NOT_RTC, "button_sleep", NULL,
                  config->button_sleep);
    }
    if (config->ir_receiver >= 0 && config->ir_receiver > RTC_GPIO_MAX) {
        add_issue(report, true, BOARD_ISSUE_IR_NOT_RTC, "ir_receiver", NULL, config->ir_receiver);
    }
    // The panel's 40 MHz bus holds on SPI2's own pins; the select may stray.
    if (config->tft_cs >= 0 && config->tft_cs != SPI2_IOMUX_CS) {
        add_issue(report, true, BOARD_ISSUE_SPI_NOT_IOMUX, "tft_cs", NULL, config->tft_cs);
    }

    for (size_t i = 0U; i < FIELD_COUNT; ++i) {
        const field_t *field = &k_fields[i];
        if ((field->kind == KIND_CHOICE || field->kind == KIND_BOOL) &&
            *byte_at(config, field) == BOARD_CONFIG_INVALID) {
            add_issue(report, false, BOARD_ISSUE_BAD_VALUE, field->key, NULL, -1);
        }
    }
}

const char *board_config_issue_name(board_issue_code_t code)
{
    switch (code) {
    case BOARD_ISSUE_PIN_CONFLICT: return "pin_conflict";
    case BOARD_ISSUE_PIN_MISSING: return "pin_missing";
    case BOARD_ISSUE_PIN_NOT_ON_HEADER: return "pin_not_on_header";
    case BOARD_ISSUE_PIN_PSRAM: return "pin_psram";
    case BOARD_ISSUE_PIN_CONSOLE: return "pin_console";
    case BOARD_ISSUE_PIN_USB: return "pin_usb";
    case BOARD_ISSUE_USB_PIN_FIXED: return "usb_pin_fixed";
    case BOARD_ISSUE_BUS_UNWIRED: return "bus_unwired";
    case BOARD_ISSUE_BAD_VALUE: return "bad_value";
    case BOARD_ISSUE_SLEEP_NOT_RTC: return "sleep_not_rtc";
    case BOARD_ISSUE_IR_NOT_RTC: return "ir_not_rtc";
    case BOARD_ISSUE_SPI_NOT_IOMUX: return "spi_not_iomux";
    }
    return "";
}

/* ---- Writing ---------------------------------------------------------- */

static size_t append(char *out, size_t capacity, size_t used, const char *format, const char *a,
                     const char *b)
{
    char scratch[1];
    char *at = used < capacity ? out + used : scratch;
    const size_t room = used < capacity ? capacity - used : 0U;
    const int written = snprintf(at, room, format, a, b);
    return written > 0 ? used + (size_t)written : used;
}

size_t board_config_to_csv(const board_config_t *config, char *out, size_t capacity)
{
    if (out != NULL && capacity > 0U) out[0] = '\0';
    if (config == NULL) return 0U;
    if (out == NULL) capacity = 0U;
    size_t used = append(out, capacity, 0U, "%s%s\n", "# key,value", "");
    for (size_t i = 0U; i < FIELD_COUNT; ++i) {
        const field_t *field = &k_fields[i];
        char value[BOARD_CONFIG_NAME_MAX];
        value[0] = '\0';
        switch (field->kind) {
        case KIND_IGNORED:
            snprintf(value, sizeof(value), "%s", field->fixed_text);
            break;
        case KIND_TEXT:
            snprintf(value, sizeof(value), "%s", (const char *)((uintptr_t)config + field->offset));
            break;
        case KIND_PIN:
        case KIND_OPT_PIN:
        case KIND_FIXED_PIN: {
            const int8_t pin = *pin_at(config, field);
            if (pin == BOARD_PIN_RESET) snprintf(value, sizeof(value), "rst");
            else if (pin < 0) snprintf(value, sizeof(value), "none");
            else snprintf(value, sizeof(value), "%d", (int)pin);
            break;
        }
        case KIND_BOOL:
            snprintf(value, sizeof(value), "%u", (unsigned)*byte_at(config, field));
            break;
        case KIND_CHOICE: {
            const uint8_t id = *byte_at(config, field);
            for (size_t c = 0U; c < CHOICES_MAX && field->choices[c].name != NULL; ++c) {
                if (field->choices[c].id == id) {
                    snprintf(value, sizeof(value), "%s", field->choices[c].name);
                    break;
                }
            }
            break;
        }
        }
        used = append(out, capacity, used, "%s,%s\n", field->key, value);
    }
    return used;
}

/* ---- The partition ---------------------------------------------------- */

uint32_t board_config_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static uint32_t read_le32(const uint8_t *at)
{
    return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) |
           ((uint32_t)at[3] << 24);
}

static void write_le32(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
    at[3] = (uint8_t)(value >> 24);
}

board_blob_status_t board_config_blob_read(const uint8_t *blob, size_t blob_size,
                                           const char **text, size_t *text_length)
{
    if (text != NULL) *text = NULL;
    if (text_length != NULL) *text_length = 0U;
    if (blob == NULL || blob_size < BOARD_BLOB_HEADER_SIZE ||
        memcmp(blob, BOARD_BLOB_MAGIC, 4U) != 0) {
        return BOARD_BLOB_EMPTY;
    }
    const uint32_t version = (uint32_t)blob[4] | ((uint32_t)blob[5] << 8);
    if (version != BOARD_BLOB_VERSION) return BOARD_BLOB_VERSION_UNKNOWN;
    const uint32_t length = read_le32(blob + 8);
    if (length > blob_size - BOARD_BLOB_HEADER_SIZE) return BOARD_BLOB_DAMAGED;
    const uint8_t *payload = blob + BOARD_BLOB_HEADER_SIZE;
    if (board_config_crc32(payload, length) != read_le32(blob + 12)) return BOARD_BLOB_DAMAGED;
    if (text != NULL) *text = (const char *)payload;
    if (text_length != NULL) *text_length = length;
    return BOARD_BLOB_OK;
}

size_t board_config_blob_write(const char *text, size_t text_length, uint8_t *out,
                               size_t capacity)
{
    if (text == NULL || out == NULL || capacity < BOARD_BLOB_HEADER_SIZE ||
        text_length > capacity - BOARD_BLOB_HEADER_SIZE || text_length > UINT32_MAX) {
        return 0U;
    }
    memcpy(out, BOARD_BLOB_MAGIC, 4U);
    out[4] = (uint8_t)BOARD_BLOB_VERSION;
    out[5] = (uint8_t)(BOARD_BLOB_VERSION >> 8);
    out[6] = 0U;
    out[7] = 0U;
    write_le32(out + 8, (uint32_t)text_length);
    write_le32(out + 12, board_config_crc32((const uint8_t *)text, text_length));
    memcpy(out + BOARD_BLOB_HEADER_SIZE, text, text_length);
    return BOARD_BLOB_HEADER_SIZE + text_length;
}
