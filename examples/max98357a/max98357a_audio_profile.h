/* Copy to the repository root as board_options.local.h, then build.
 * Audio bring-up profile: one existing encoder + OK, standard quick-menu key.
 * The second encoder and new button semantics are separate work. */
#undef DISPLAY
#define DISPLAY DISPLAY_ST7789_320_240
#define TFT_RESET_GPIO 4
#undef AUDIO_DAC
#define AUDIO_DAC DAC_MAX98357A
#define AUDIO_AMP_GPIO 15
#define AUDIO_AMP_ON_LEVEL 0
#define AUDIO_MAX98357A_CEILING_DBFS (-12)
/* Existing I2S output pins: DOUT16, BCLK18, LRCK17. */
#define FM_TUNER FM_TUNER_RDA5807
#define FM_I2C_PERIPHERAL 0
#define FM_I2C_SDA_GPIO 8
#define FM_I2C_SCL_GPIO 9
#define FM_I2S_PERIPHERAL 0
#define I2S_DIN_GPIO 39
#define RTC_CHIP RTC_CHIP_DS3231
#define RTC_I2C_PERIPHERAL 0
#undef BUTTON_SLEEP_GPIO
#undef BUTTON_PREV_GPIO
#undef BUTTON_NEXT_GPIO
#undef BUTTON_QUICK_MENU_GPIO
#define BUTTON_QUICK_MENU_GPIO 21
#undef USB_DP_GPIO
#undef USB_DM_GPIO
#undef SDC_CS_GPIO
#undef SDC_SCK_GPIO
#undef SDC_MOSI_GPIO
#undef SDC_MISO_GPIO
#undef IR_RECEIVER_GPIO
#undef BLUETOOTH
#define BLUETOOTH BLUETOOTH_NONE
#undef TOUCH_CHIP
#define TOUCH_CHIP TOUCH_CHIP_NONE
#undef AUDIO_ADC
#define AUDIO_ADC AUDIO_ADC_NONE
#undef AUDIO_SPDIF_GPIO
#undef YANDEX_MUSIC
#define YANDEX_MUSIC FEATURE_OFF
#undef DLNA
#define DLNA FEATURE_OFF
