#pragma once

#include "board_parts.h"

/* Which external parts are fitted, and how they are wired.
 *
 * This is the one file to edit when repeating the board, swapping a part or
 * moving one to a different pin. It holds nothing else: everything that
 * *follows* from a choice made here - a panel's resolution, the I2S sample
 * format, DMA sizing - lives in the profile header for that part
 * (board_display_profile.h, board_audio_format.h), because those are
 * properties of the part and of the driver, not decisions about this board.
 *
 * Values are plain numbers rather than driver enums so the file also compiles
 * in the host test build, which has no ESP-IDF headers.
 *
 * Board revision 1, ESP32-S3 QFN56, 16 MB flash, 8 MB PSRAM.
 *
 * These names are deliberately unprefixed, which puts them in the same
 * namespace as ESP-IDF's. Nothing collides today, but a future IDF release
 * defining e.g. its own I2S_DOUT_GPIO would otherwise silently win or lose
 * depending on include order. The guard below turns that into a build error
 * naming the option, so include this header *after* the driver headers - as
 * every file here does.
 */

#if defined(TFT_CS_GPIO) || defined(I2S_DOUT_GPIO) || defined(USB_DP_GPIO) || \
    defined(ENCODER_LEFT_GPIO) || defined(BACKLIGHT_PWM_HZ) ||                \
    defined(INPUT_POLL_MS) || defined(DISPLAY) || defined(AUDIO_DAC)
#error "board_options.h: a name here is already defined elsewhere - most likely \
an ESP-IDF header now uses it. Re-prefix the affected option in this file and \
at its use sites."
#endif

/* ======================================================================
 * Display - TFT panel on SPI
 * ====================================================================== */

#define DISPLAY DISPLAY_ILI9341_320

/* SPI peripheral index: 2 selects SPI2, 3 selects SPI3. Mapped to the driver's
 * host enum in board.c, since the numbering of that enum is not the peripheral
 * number. */
#define DISPLAY_SPI_PERIPHERAL 2
#define DISPLAY_PIXEL_CLOCK_HZ (20 * 1000 * 1000)

#define TFT_CS_GPIO 10
#define TFT_DC_GPIO 47
#define TFT_MOSI_GPIO 11
#define TFT_SCLK_GPIO 12
/* No MISO: the panel is written to and never read back.
 * No RST pin either - the panel's reset is tied to the ESP32 reset line, so it
 * costs no GPIO and cannot be driven separately. */

/* ======================================================================
 * Display backlight - PWM on a single pin
 * ====================================================================== */

#define TFT_BACKLIGHT_GPIO 2
#define BACKLIGHT_PWM_HZ 5000
/* Duty resolution in bits. The driver's ledc_timer_bit_t enumerators are the
 * bit counts themselves, which board.c verifies rather than assumes. */
#define BACKLIGHT_DUTY_BITS 13

/* ======================================================================
 * Controls - rotary encoder with push button, plus four function buttons
 * ====================================================================== */

#define ENCODER_RIGHT_GPIO 5
#define ENCODER_LEFT_GPIO 7
#define ENCODER_BUTTON_GPIO 6

#define BUTTON_F1_GPIO 45
#define BUTTON_F2_GPIO 21
#define BUTTON_F3_GPIO 46
#define BUTTON_F4_GPIO 9

/* External pull-ups are fitted, but GPIO 45, 46 and 21 were still unstable
 * without the internal ones enabled on top. */
#define INPUT_USE_INTERNAL_PULLUPS 1
/* There is no hardware debouncing, so the contacts are filtered in software:
 * a level must hold for INPUT_DEBOUNCE_MS across polls at INPUT_POLL_MS to
 * count. The encoder is polled rather than interrupt-driven at the same
 * interval. */
#define INPUT_POLL_MS 5
#define INPUT_DEBOUNCE_MS 25
/* Hold F1 this long to clear the saved Wi-Fi networks. */
#define INPUT_F1_HOLD_MS 5000

/* ======================================================================
 * Audio output - I2S DAC, line out
 * ====================================================================== */

#define AUDIO_DAC DAC_PCM5102

#define I2S_DOUT_GPIO 16
#define I2S_BCLK_GPIO 18
#define I2S_LRCK_GPIO 17
/* No MCLK pin is wired, so the DAC has to derive its clock from BCLK. */
#define AUDIO_DAC_HAS_MCLK 0

/* ======================================================================
 * USB host - flash drive, internal PHY
 * ====================================================================== */

#define USB_DM_GPIO 19
#define USB_DP_GPIO 20
/* VBUS is permanently powered and not switched by firmware. A drive left
 * attached across an ESP32 reset is recovered by power-cycling the root port
 * logically (usb_host_lib_set_root_port_power), which toggles the controller's
 * PRTPWR bit and needs no GPIO of its own. */
#define USB_VBUS_SWITCHED 0
