#include <assert.h>
#include <stdio.h>

#include "board_config.h"

int main(void)
{
    assert(BOARD_TFT_WIDTH == 320);
    assert(BOARD_TFT_HEIGHT == 240);
    assert(BOARD_TFT_RGB_ORDER_BGR == 1);
    assert(BOARD_TFT_SWAP_XY == 1);
    assert(BOARD_TFT_MIRROR_X == 0);
    assert(BOARD_TFT_MIRROR_Y == 0);
    assert(BOARD_LCD_WAIT_FOR_TRANSFER == 1);
    assert(BOARD_INPUT_USE_INTERNAL_PULLUPS == 1);
    assert(BOARD_TFT_CS_GPIO == 10);
    assert(BOARD_TFT_DC_GPIO == 47);
    assert(BOARD_TFT_MOSI_GPIO == 11);
    assert(BOARD_TFT_SCLK_GPIO == 12);
    assert(BOARD_I2S_DOUT_GPIO == 16);
    assert(BOARD_I2S_BCLK_GPIO == 18);
    assert(BOARD_I2S_LRCK_GPIO == 17);
    assert(BOARD_AUDIO_DEFAULT_SAMPLE_RATE == 44100);
    assert(BOARD_AUDIO_BITS_PER_SAMPLE == 16);
    assert(BOARD_AUDIO_CHANNEL_COUNT == 2);
    assert(BOARD_I2S_SLOT_BIT_WIDTH == 16);
    assert(BOARD_I2S_DMA_DESC_NUM == 8);
    assert(BOARD_I2S_DMA_FRAME_NUM == 512);
    assert((BOARD_I2S_DMA_DESC_NUM * BOARD_I2S_DMA_FRAME_NUM * 1000U) /
               BOARD_AUDIO_DEFAULT_SAMPLE_RATE >=
           90U);
    assert(BOARD_I2S_AUTO_CLEAR_AFTER_CB == 1);
    assert(BOARD_I2S_PRELOAD_SILENCE == 1);
    assert(BOARD_USB_DM_GPIO == 19);
    assert(BOARD_USB_DP_GPIO == 20);
    puts("board_config tests passed");
    return 0;
}
