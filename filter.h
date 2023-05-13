#include <stdint.h>

struct interlacing_t{
    uint32_t width;
    uint32_t height;
}; // 56 bytes

void set_interlacing(struct interlacing_t *sub_images, uint32_t width, uint32_t height);

void png_filter(uint8_t *scanline, uint32_t scanline_width, uint32_t scanline_count, uint8_t stride);