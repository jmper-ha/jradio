#include <assert.h>
#include <stdio.h>

#include "board_audio_startup.h"

static void test_startup_prefills_the_complete_dma_ring_with_silence(void)
{
    assert(board_audio_startup_silence_bytes(8U, 2048U) == 16384U);
}

static void test_startup_prefill_rejects_an_empty_dma_layout(void)
{
    assert(board_audio_startup_silence_bytes(0U, 2048U) == 0U);
    assert(board_audio_startup_silence_bytes(8U, 0U) == 0U);
}

int main(void)
{
    test_startup_prefills_the_complete_dma_ring_with_silence();
    test_startup_prefill_rejects_an_empty_dma_layout();
    puts("board_audio_startup tests passed");
    return 0;
}
