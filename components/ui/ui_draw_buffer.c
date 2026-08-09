#include "ui_draw_buffer.h"

size_t ui_rgb565_draw_buffer_size(size_t horizontal_pixels, size_t lines)
{
    return horizontal_pixels * lines * 2U;
}
