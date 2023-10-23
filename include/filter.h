#ifndef _PNG_FILTER_
#define _PNG_FILTER_

#include <stdint.h>

#include "png_utils.h"

#define PNG_INTERLACE_NONE 0
#define PNG_INTERLACE_ADAM7 1
#define FILTER_BYTE_SIZE 1

struct sub_image_t
{
    uint32_t scanline_size;
    uint32_t scanline_count;
    uint8_t px_offset;
    uint8_t px_stride;
    uint8_t row_offset;
    uint8_t row_stride;
};

struct scanline_t
{
    uint8_t *buffer;
    uint8_t *last;
    uint8_t *new;
    uint32_t buffer_size;
    uint32_t index;
    uint8_t stride;
};

struct output_settings_t
{
    struct
    {
        struct sub_image_t images[8];
        uint32_t row_index;
        uint8_t image_index;
    } subimage;
    struct scanline_t scanline;
    struct
    {
        struct rgb_t *buffer;
        uint8_t *alpha;
        uint8_t size;
    } palette;
    struct
    {
        uint8_t bit_depth;
        uint8_t color_type;
        uint8_t rgb_size;
        uint8_t size;
        uint8_t index;
    } pixel;
    uint32_t image_width;
    uint8_t filter_type;
};

void filter(uint8_t byte, struct data_buffer_t *output_image, void *output_settings);

void set_interlacing(const struct png_header_t *png_header, struct sub_image_t *sub_images, const uint32_t bits_per_pixel);

void png_filter(uint8_t *scanline, uint32_t scanline_width, uint32_t scanline_count, uint8_t stride);

#endif