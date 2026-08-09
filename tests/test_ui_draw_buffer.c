#include <assert.h>
#include <stddef.h>

#include "ui_draw_buffer.h"

int main(void)
{
    /* 320 pixels x 20 lines x 2 bytes per RGB565 pixel. */
    assert(ui_rgb565_draw_buffer_size(320U, 20U) == 12800U);
    return 0;
}
