#ifndef _PNG_FILTER_
#define _PNG_FILTER_

#include <stdint.h>

#include "png_utils.h"

#define PNG_INTERLACE_NONE 0
#define PNG_INTERLACE_ADAM7 1
#define FILTER_BYTE_SIZE 1

struct sub_image_t {
    uint32_t scanline_width;
    uint32_t scanline_size;
    uint32_t scanline_count;
}; // 56 bytes

struct scanline_buffer_t {
    uint8_t *buffer;
    uint8_t *last;
    uint8_t *current;
    uint32_t index;
    const uint8_t stride;
};

struct filter_settings_t {
    struct sub_image_t images[8];
    struct scanline_buffer_t scanline;
    uint32_t row_index;
    uint8_t image_index;
    uint8_t bit_depth;
    uint8_t interlace_method;
};

void filter(uint8_t byte, void* payload);

void set_interlacing_mode_2(const struct png_header_t *png_header, struct sub_image_t *sub_images, const uint32_t bits_per_pixel);
void set_interlacing(const struct png_header_t *png_header, struct sub_image_t *sub_images);

void png_filter(uint8_t *scanline, uint32_t scanline_width, uint32_t scanline_count, uint8_t stride);

#endif