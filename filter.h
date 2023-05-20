#include <stdint.h>

#include "png_utils.h"

struct sub_image_t{
    uint32_t scanline_width;
    uint32_t scanline_size;
    uint32_t scanline_count;
}; // 56 bytes

void set_interlacing(const struct png_header_t *png_header, struct sub_image_t *sub_images);

void png_filter(uint8_t *scanline, uint32_t scanline_width, uint32_t scanline_count, uint8_t stride);