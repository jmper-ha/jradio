#pragma once

/* Wiring and part selection for every external device on the board.
 *
 * This is the one file to edit when repeating the board or moving a part to a
 * different pin: nothing here is derived from anything else, and no firmware
 * logic lives here. Values are plain numbers rather than driver enums so the
 * file also compiles in the host test build, which has no ESP-IDF headers.
 *
 * Board revision 1, ESP32-S3 QFN56, 16 MB flash, 8 MB PSRAM.
 *
 * Each group names the part it describes. The *_CONTROLLER / *_DAC selectors
 * are checked at compile time by the driver that consumes them, so pointing
 * one at a part the firmware has no code for fails the build instead of
 * producing a device that initialises and then misbehaves.
 *
 * These names are deliberately unprefixed, which puts them in the same
 * namespace as ESP-IDF's. Nothing collides today, but a future IDF release
 * defining e.g. its own I2S_DOUT_GPIO would otherwise silently win or lose
 * depending on include order. The guard below turns that into a build error
 * naming the option, so include this header *after* the driver headers - as
 * every file here does.
 */

#if defined(TFT_CS_GPIO) || defined(I2S_DOUT_GPIO) || defined(USB_DP_GPIO) || \
    defined(ENCODER_LEFT_GPIO) || defined(AUDIO_CHANNEL_COUNT) ||             \
    defined(LCD_WAIT_FOR_TRANSFER) || defined(DISPLAY_CONTROLLER)
#error "board_options.h: a name here is already defined elsewhere - most likely \
an ESP-IDF header now uses it. Re-prefix the affected option in this file and \
at its use sites."
#endif

/* ======================================================================
 * Display - ILI9341 320x240 TFT on SPI
 * ====================================================================== */

#define DISPLAY_ILI9341 1
#define DISPLAY_CONTROLLER DISPLAY_ILI9341

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

#define TFT_WIDTH 320
#define TFT_HEIGHT 240
/* Verified on the connected TFT: blue data is rendered red with MADCTL RGB. */
#define TFT_RGB_ORDER_BGR 1
#define TFT_SWAP_XY 1
/* Rotate the current landscape image by 180 degrees. */
#define TFT_MIRROR_X 0
#define TFT_MIRROR_Y 0
/* Wait for each colour transfer to finish before reusing the draw buffer.
 * Required because LVGL is given a single buffer; see ui.c. */
#define LCD_WAIT_FOR_TRANSFER 1

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
 * a level must hold for INPUT_DEBOUNCE_MS across polls at
 * INPUT_POLL_MS to count. The encoder is polled rather than
 * interrupt-driven at the same interval. */
#define INPUT_POLL_MS 5
#define INPUT_DEBOUNCE_MS 25
/* Hold F1 this long to clear the saved Wi-Fi networks. */
#define INPUT_F1_HOLD_MS 5000

/* ======================================================================
 * Audio output - PCM5102 I2S DAC, line out
 * ====================================================================== */

#define AUDIO_DAC_PCM5102 1
#define AUDIO_DAC AUDIO_DAC_PCM5102

#define I2S_DOUT_GPIO 16
#define I2S_BCLK_GPIO 18
#define I2S_LRCK_GPIO 17
/* The PCM5102 derives its own clock from BCLK, so no MCLK pin is wired and
 * BCLK must stay at 32 x Fs. */
#define AUDIO_DAC_HAS_MCLK 0

/* The slots are fixed 16-bit stereo: mono sources are duplicated into both
 * channels by the players rather than reconfigured here. */
#define AUDIO_DEFAULT_SAMPLE_RATE 44100
#define AUDIO_BITS_PER_SAMPLE 16
#define AUDIO_CHANNEL_COUNT 2
#define I2S_SLOT_BIT_WIDTH 16

/* 8 x 512 frames provide about 92.9 ms of buffering at 44.1 kHz.
 * This geometry is deliberate: it fixed the audible clicking (see the
 * first-PCM I2S start design), so do not shrink it to reclaim the ~16 KB of
 * internal DMA RAM it holds - find that headroom elsewhere. It is also the
 * real tolerance to a source hiccup, which the radio prebuffer is sized
 * against. */
#define I2S_DMA_DESC_NUM 8
#define I2S_DMA_FRAME_NUM 512
#define I2S_AUTO_CLEAR_AFTER_CB 1
/* Start output with real samples after a silent clock pre-roll instead of
 * enabling TX into an empty ring: 93 ms of zeros exceeds the PCM5102's
 * zero-data detect (1024 frames) and would engage its analog mute. */
#define I2S_PRELOAD_SILENCE 1

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
