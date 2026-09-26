#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The board as a file: board.csv, the wiring the firmware reads at boot
 * instead of taking every pin from board_options.h at compile time.
 *
 * One binary per panel shape can then serve every board built around it -
 * the web flasher writes the wiring from its form into the `board`
 * partition, and a build writes the one its header describes. The file's
 * vocabulary is the wiring editor's (flasher/hardware_core.js): buses own
 * their pins, devices name the bus they sit on and add their own. The two
 * readers of it - that page and this - are held to the same answers by
 * tests/fixtures/board_cases.json, which both test suites run.
 *
 * Pure: no ESP-IDF here, so the parsing and the rules are tested on the
 * host. Reading the partition is board_config_load(), device only. */

/* A pin that is not wired, and the display's reset tied to the module's
 * own RST pad - which costs no GPIO and cannot be driven. */
#define BOARD_PIN_NONE ((int8_t)-1)
#define BOARD_PIN_RESET ((int8_t)-2)

#define BOARD_CONFIG_NAME_MAX 48
#define BOARD_CONFIG_INVALID 0xFFU
#define BOARD_CONFIG_FORMAT 1

typedef struct {
    char board_name[BOARD_CONFIG_NAME_MAX];
    /* The part ids of board_parts.h: DISPLAY_*, DAC_*, BLUETOOTH_*. The
     * display is only checked against the one compiled in - it decides the
     * layouts, which are compiled - never switched at run time. */
    uint8_t display;
    uint8_t dac;
    uint8_t bluetooth;
    /* The switches below are 0 or 1. They are bytes rather than bools, and a
     * choice above holds BOARD_CONFIG_INVALID, when the file said something
     * else: the editor reports that as bad_value, and a bool could not hold
     * it long enough for the rules to see it. */

    // SPI2 is the display's and its pins are the chip's own (IOMUX).
    int8_t spi2_sclk;
    int8_t spi2_mosi;
    int8_t spi3_sclk;
    int8_t spi3_mosi;
    int8_t spi3_miso;
    int8_t i2s0_bclk;
    int8_t i2s0_lrck;
    int8_t i2s0_dout;
    int8_t uart1_tx;
    int8_t uart1_rx;

    int8_t tft_cs;
    int8_t tft_dc;
    int8_t tft_reset;
    int8_t tft_backlight;

    int8_t encoder_right;
    int8_t encoder_left;
    int8_t encoder_button;
    uint8_t encoder_pullups;
    int8_t button_sleep;
    int8_t button_quick_menu;
    int8_t button_prev;
    int8_t button_next;
    uint8_t buttons_pullups;

    int8_t ir_receiver;
    int8_t dac_mute;
    int8_t amp_enable;
    uint8_t amp_on_level;
    int8_t peripheral_power;
    uint8_t peripheral_power_on_level;
    // Fixed at 20/19 by the chip; BOARD_PIN_NONE when the port is not fitted.
    int8_t usb_dp;
    int8_t usb_dm;
    int8_t sd_cs;

    uint8_t yandex_music;
    uint8_t dlna;

    /* Which bus each device names. Each has one possible value today and is
     * kept only so a file naming another is refused rather than obeyed:
     * tft_spi 2, sd_spi 3, dac_i2s 0, bt_uart 1, bt_i2s 0. */
    uint8_t sd_spi;
    uint8_t dac_i2s;
    uint8_t bt_uart;
    uint8_t bt_i2s;
} board_config_t;

typedef enum {
    BOARD_ISSUE_PIN_CONFLICT = 0,
    BOARD_ISSUE_PIN_MISSING,
    BOARD_ISSUE_PIN_NOT_ON_HEADER,
    BOARD_ISSUE_PIN_PSRAM,
    BOARD_ISSUE_PIN_CONSOLE,
    BOARD_ISSUE_PIN_USB,
    BOARD_ISSUE_USB_PIN_FIXED,
    BOARD_ISSUE_BUS_UNWIRED,
    BOARD_ISSUE_BAD_VALUE,
    // Warnings: the board works, but not the way its builder expects.
    BOARD_ISSUE_SLEEP_NOT_RTC,
    BOARD_ISSUE_IR_NOT_RTC,
    BOARD_ISSUE_SPI_NOT_IOMUX,
} board_issue_code_t;

typedef struct {
    board_issue_code_t code;
    // The key the issue is about; for BUS_UNWIRED the device, for CONFLICT
    // the first of the keys sharing the pin.
    const char *key;
    // For BUS_UNWIRED: the bus pin's key, "spi3_miso".
    const char *detail;
    int gpio;
} board_issue_t;

#define BOARD_CONFIG_ISSUES_MAX 24

typedef struct {
    board_issue_t errors[BOARD_CONFIG_ISSUES_MAX];
    size_t error_count;
    board_issue_t warnings[BOARD_CONFIG_ISSUES_MAX];
    size_t warning_count;
    // Past the arrays: counted, so a long list is not mistaken for a short one.
    size_t dropped;
} board_config_report_t;

typedef struct {
    // Keys this firmware does not know: a newer file, kept rather than failed.
    size_t unknown_keys;
    // Lines that are not "key,value", or a value that is not of its kind.
    size_t bad_lines;
    // The first bad line, 1-based, for the log; 0 when there is none.
    size_t first_bad_line;
} board_config_parse_result_t;

/* Every pin none, every switch off, the fixed buses at their one value. What
 * a parse starts from when nothing else is given. */
void board_config_clear(board_config_t *config);

/* The board this firmware was built for, out of board_options.h: what a key
 * the file leaves out falls back to. */
void board_config_compiled(board_config_t *config);

/* Reads board.csv over `config`, which holds the defaults: a key the file
 * does not name keeps its value, which is what makes a short file - or one
 * from an older page - mean "the compiled board, except". Keys the chip or
 * the firmware decide (board_format, module, tft_spi, spi2_*) are read past.
 * Never fails as a whole: see the result. */
void board_config_parse_csv(const char *text, size_t length, board_config_t *config,
                            board_config_parse_result_t *result);

/* The rules of the wiring editor's validate(), for the same answers. Errors
 * make a board unusable; warnings are notes. */
void board_config_validate(const board_config_t *config, board_config_report_t *report);

/* The code as the editor names it: "pin_conflict", "bus_unwired". */
const char *board_config_issue_name(board_issue_code_t code);

/* Writes the board as board.csv, every key in the editor's order. Returns
 * the length it needed, like snprintf; the text is cut but terminated when
 * that is more than `capacity`. */
size_t board_config_to_csv(const board_config_t *config, char *out, size_t capacity);

/* ---- The `board` partition --------------------------------------------
 *
 * board.csv behind a 16-byte header: "JRBD", a version, the length of the
 * text and its CRC32, all little-endian. An erased partition reads as all
 * 0xFF and fails the magic; a torn write fails the CRC. Either way the
 * compiled board is used - a half-written wiring must never be obeyed. */
#define BOARD_BLOB_MAGIC "JRBD"
#define BOARD_BLOB_VERSION 1U
#define BOARD_BLOB_HEADER_SIZE 16U
// The partition's size; the text has to fit behind the header.
#define BOARD_BLOB_MAX 4096U

typedef enum {
    BOARD_BLOB_OK = 0,
    BOARD_BLOB_EMPTY,     // erased or never written
    BOARD_BLOB_VERSION_UNKNOWN,
    BOARD_BLOB_DAMAGED,   // length past the end, or the CRC does not match
} board_blob_status_t;

/* Finds the text in a partition image. On OK, `*text` points into `blob`. */
board_blob_status_t board_config_blob_read(const uint8_t *blob, size_t blob_size,
                                           const char **text, size_t *text_length);

/* Puts `text` behind a header. Returns the blob's length, or 0 when it does
 * not fit in `capacity`. */
size_t board_config_blob_write(const char *text, size_t text_length, uint8_t *out,
                               size_t capacity);

// IEEE CRC-32 (zlib's), the one the flasher's page computes too.
uint32_t board_config_crc32(const uint8_t *data, size_t length);

/* ---- On the device ------------------------------------------------------
 *
 * Reads the `board` partition once, early in app_main, and keeps the answer:
 * the file over the compiled board when it is there and passes the rules,
 * the compiled board otherwise. Says which in the log, with every issue the
 * rules found. */
typedef enum {
    BOARD_CONFIG_SOURCE_COMPILED = 0,
    BOARD_CONFIG_SOURCE_PARTITION,
} board_config_source_t;

void board_config_load(void);
const board_config_t *board_config_get(void);
board_config_source_t board_config_source(void);

#ifdef __cplusplus
}
#endif
