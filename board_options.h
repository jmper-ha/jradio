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
 * The last section is the one exception to "parts and wiring": the optional
 * features, which are not on the board but are decided the same way and want
 * to be found in the same place. board_features.h turns them into the 0/1
 * everything else tests.
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
    defined(ENCODER_LEFT_GPIO) || defined(TFT_BACKLIGHT_GPIO) ||              \
    defined(BUTTON_F1_GPIO) || defined(DISPLAY) || defined(AUDIO_DAC) ||      \
    defined(SDC_CS_GPIO) || defined(YANDEX_MUSIC)
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

/* Internal pull-ups, per group rather than for all seven pins at once.
 *
 * External pull-ups are fitted on the board. The buttons still needed the
 * internal ones on top - GPIO 45, 46 and 21 were unstable without them - but
 * the encoder never had that problem, and the two parallel pull-ups only add
 * current through its contacts on every detent. */
#define ENCODER_USE_INTERNAL_PULLUPS 0
#define BUTTONS_USE_INTERNAL_PULLUPS 1
/* There is no hardware debouncing on either group, and no interrupt line for
 * the encoder: both are polled and filtered in board_input.c. */

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

/* ======================================================================
 * SD card - microSD in SPI mode, on its own bus
 * ====================================================================== */

/* A second SPI peripheral rather than a share of the display's. The panel bus
 * has no MISO wired at all, which a card cannot work without, and the display
 * driver owns its bus with its own transactions - a card on it would have to
 * interleave with every frame. SPI2 is the panel's, so this is SPI3. */
#define SDC_SPI_PERIPHERAL 3

#define SDC_CS_GPIO 1
#define SDC_SCK_GPIO 41
#define SDC_MISO_GPIO 40
#define SDC_MOSI_GPIO 42
/* GPIO 40, 41 and 42 are MTDO/MTDI/MTMS, so the bus takes over the external
 * JTAG header. No loss in practice: the built-in USB Serial/JTAG shares its
 * pins with the USB host port, which is already wired to the drive socket. */

/* No card-detect or write-protect line is wired: the socket's switch pins are
 * unconnected, so a card that appears after boot cannot announce itself the
 * way a USB drive does. Insertion has to be found by trying to mount. */
#define SDC_HAS_CARD_DETECT 0

/* ======================================================================
 * Optional features - what is built into this firmware
 * ====================================================================== */

/* Yandex Music. Not a part on the board, but the same kind of decision, and
 * made in the same place: built out, nothing on the device mentions an account
 * it cannot use - the source is absent from the home screen and its switch is
 * absent from Settings. Built in, the switch in Settings > General decides
 * whether the source appears, so a firmware that has it can still be run
 * without it.
 *
 * FEATURE_OFF and deleting the line mean the same thing. */
#define YANDEX_MUSIC FEATURE_ON
