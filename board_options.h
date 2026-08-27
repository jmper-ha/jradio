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
    defined(SDC_CS_GPIO) || defined(YANDEX_MUSIC) || defined(FM_TUNER) ||     \
    defined(BLUETOOTH) || defined(DLNA)
#error "board_options.h: a name here is already defined elsewhere - most likely \
an ESP-IDF header now uses it. Re-prefix the affected option in this file and \
at its use sites."
#endif

/* ======================================================================
 * Display - TFT panel on SPI
 * ====================================================================== */

/* The panel, and with it which way up it is mounted: DISPLAY_ILI9341_320_240
 * is the landscape module revision 1 is built with, DISPLAY_ILI9341_240_320
 * the same one stood on end. The choice lays out every screen in the firmware,
 * and it picks the boot splash to match - both are compiled in, so changing
 * this line needs no regeneration. */
#define DISPLAY DISPLAY_ILI9341_320_240

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

/* Two of the four are named for what they do rather than for the silkscreen:
 * the third and fourth buttons are the track keys everywhere they do anything
 * at all, so a name that says F3 only makes the wiring harder to read back. */
#define BUTTON_F1_GPIO 45
#define BUTTON_F2_GPIO 21
#define BUTTON_PREV_GPIO 46
#define BUTTON_NEXT_GPIO 9

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
 * FM radio - tuner module on I2C  (not fitted)
 * ====================================================================== */

/* Nothing on revision 1 receives FM, and no driver is written. The block is
 * here rather than absent because "which parts can this firmware drive" is a
 * question this file should answer, including with a no: an option that only
 * exists in someone's memory gets re-invented differently next time.
 *
 * Uncommenting the four lines is what fits a tuner: the source then appears
 * on the home screen and in the web interface, which is exactly the point at
 * which the missing driver becomes the next thing to write.
 *
 * The bus is its own, not the card's: SPI3 has no spare pins left, and a
 * tuner is an I2C part anyway. */
// #define FM_TUNER FM_TUNER_RDA5807
// #define FM_I2C_PERIPHERAL 0
// #define FM_I2C_SDA_GPIO 8
// #define FM_I2C_SCL_GPIO 3

/* ======================================================================
 * Bluetooth audio - not possible on this part
 * ====================================================================== */

/* The ESP32-S3 has no classic Bluetooth at all - its radio is Wi-Fi and BLE -
 * and A2DP is a classic-Bluetooth profile. So this is not "not fitted yet"
 * the way FM is: on this chip it cannot be fitted, and playing from a phone
 * over Bluetooth needs a receiver module of its own feeding the I2S input, or
 * a different part.
 *
 * Left named so the answer is written down where the question is asked. */
// #define BLUETOOTH BLUETOOTH_A2DP_SINK

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

/* DLNA renderer: play what a phone or a PC on the LAN pushes to the device.
 * Nothing implements it yet, and it is off for the same reason FM is
 * commented out - the option is written down so that turning it on is one
 * line here, and so that the home screen does not offer what the firmware
 * cannot do. */
#define DLNA FEATURE_OFF
