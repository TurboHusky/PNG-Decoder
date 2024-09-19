#include "filter_tests.h"

#include <stdio.h>

#define TEST_FILTER_SIZE 1000

struct output_settings_t *filter_setup()
{
    static uint8_t data[2 * TEST_FILTER_SIZE + 2];

    static struct output_settings_t settings = {
        .filter_type = 1,
        .pixel.rgb_size = 1,
        .pixel.size = 1,
        .pixel.index = 0,
        .pixel.bit_depth = 8,
        .pixel.color_type = Greyscale,
        .palette.buffer = NULL,
        .palette.alpha = NULL,
        .palette.size = 0,
        .image_width = TEST_FILTER_SIZE,
        .subimage.image_index = 0,
        .subimage.row_index = 0,
        .subimage.images[0].scanline_size = TEST_FILTER_SIZE + 1,
        .subimage.images[0].scanline_count = 1,
        .subimage.images[0].px_offset = 0,
        .subimage.images[0].px_stride = 1,
        .subimage.images[0].row_offset = 0,
        .subimage.images[0].row_stride = 1,
        .scanline.buffer = data,
        .scanline.buffer_size = 2 * TEST_FILTER_SIZE + 2,
        .scanline.last = data,
        .scanline.new = data + TEST_FILTER_SIZE + 1,
        .scanline.stride = 1,
        .scanline.index = 0};

    return &settings;
}

MunitResult interlacing_setup_test(const MunitParameter params[], void *png_data)
{
    (void)png_data;
    (void)params;

    const uint8_t colour_types[] = {0, 2, 3, 4, 6}; // Greyscale, Truecolour, Indexed, Greyscale Alpha, Truecolour Alpha
    const uint8_t pixel_size[] = {1, 0, 3, 1, 2, 0, 4};
    const uint8_t bit_depths[] = {1, 2, 4, 8, 16};

    struct png_header_t header;
    struct sub_image_t subimages[8];
    header.colour_type = colour_types[munit_rand_int_range(0, 4)];
    header.width = munit_rand_uint32();
    header.height = munit_rand_uint32();
    switch (header.colour_type)
    {
    case 0:
        header.bit_depth = bit_depths[munit_rand_int_range(0, 4)];
        break;
    case 2:
    case 4:
    case 6:
        header.bit_depth = bit_depths[munit_rand_int_range(3, 4)];
        break;
    case 3:
        header.bit_depth = bit_depths[munit_rand_int_range(0, 3)];
        break;
    default:
        header.bit_depth = 0;
        break;
    }

    uint32_t bits_per_pixel = pixel_size[header.colour_type] * header.bit_depth;
    uint64_t scanline_size = FILTER_BYTE_SIZE + (((header.width * bits_per_pixel) + 7) >> 3);

    header.interlace_method = PNG_INTERLACE_NONE;
    set_interlacing(&header, bits_per_pixel, subimages);

    munit_assert_uint32(subimages[0].scanline_size, ==, scanline_size);
    munit_assert_uint32(subimages[0].scanline_count, ==, header.height);

    header.interlace_method = PNG_INTERLACE_ADAM7;
    set_interlacing(&header, bits_per_pixel, subimages);

    const uint8_t y_pad[] = {7, 7, 3, 3, 1, 1, 0};
    const uint8_t y_shift[] = {3, 3, 3, 2, 2, 1, 1};
    const uint8_t x_pad[] = {7, 3, 3, 1, 1, 0, 0};
    const uint8_t x_shift[] = {3, 3, 2, 2, 1, 1, 0};
    for (int i = 0; i < 7; ++i)
    {
        uint64_t subimage_size = (header.width + x_pad[i]) >> x_shift[i];
        uint64_t line_bytes = FILTER_BYTE_SIZE + ((subimage_size * bits_per_pixel + 7) >> 3);
        munit_assert_uint64(subimages[i].scanline_size, ==, line_bytes);
        munit_assert_uint32(subimages[i].scanline_count, ==, (header.height + y_pad[i]) >> y_shift[i]);
    }

    return MUNIT_OK;
}

MunitResult deinterlacing_test(const MunitParameter params[], void *data)
{
    (void)params;
    (void)data;

    struct png_header_t png_header = {
        .width = munit_rand_int_range(1, 16),
        .height = munit_rand_int_range(1, 16),
        .bit_depth = 8,
        .colour_type = Greyscale,
        .interlace_method = PNG_INTERLACE_ADAM7};

    size_t source_image_size = png_header.width * png_header.height;
    uint8_t *source_image = malloc(source_image_size);
    munit_rand_memory(source_image_size, source_image);

    size_t test_image_size = source_image_size * 2; // Allow at least 15 interlaced lines for every 8 scanlines
    uint8_t *test_image = calloc(test_image_size, 1);
    struct data_buffer_t output = {
        .data = calloc(test_image_size, sizeof(uint8_t)),
        .index = 0};

    const uint32_t y_start[] = {0, 0, 4, 0, 2, 0, 1};
    const uint32_t y_spacing[] = {8, 8, 8, 4, 4, 2, 2};
    const uint32_t x_start[] = {0, 4, 0, 2, 0, 1, 0};
    const uint32_t x_spacing[] = {8, 8, 4, 4, 2, 2, 1};

    size_t index = 0;
    test_image_size = source_image_size;
    for (int i = 0; i < 7; ++i)
    {
        for (uint32_t y = y_start[i]; y < png_header.height; y += y_spacing[i])
        {
            if (x_start[i] >= png_header.width)
            {
                break;
            }
            test_image[index] = 0; // filter
            ++index;
            ++test_image_size;
            for (uint32_t x = x_start[i]; x < png_header.width; x += x_spacing[i])
            {
                *(test_image + index) = *(source_image + x + (y * png_header.width));
                ++index;
            }
        }
    }

    const uint8_t bytes_per_pixel[7] = {1, 0, 3, 1, 2, 0, 4};
    const uint32_t bits_per_pixel = png_header.bit_depth * bytes_per_pixel[png_header.colour_type];
    const uint8_t palette_scale = (png_header.colour_type == Indexed_colour) ? 3 : 1;

    struct output_settings_t settings = {
        .pixel.rgb_size = palette_scale * bytes_per_pixel[png_header.colour_type] * ((png_header.bit_depth + 0x07) >> 3),
        .pixel.size = palette_scale * bytes_per_pixel[png_header.colour_type] * ((png_header.bit_depth + 0x07) >> 3),
        .pixel.index = 0,
        .pixel.bit_depth = png_header.bit_depth,
        .pixel.color_type = png_header.colour_type,
        .subimage.image_index = 0,
        .subimage.row_index = 0,
        .image_width = png_header.width};

    set_interlacing(&png_header, bits_per_pixel, settings.subimage.images);

    settings.scanline.stride = (bits_per_pixel + 0x07) >> 3;
    const uint32_t scanline_pixel_byte_count = (png_header.width * bits_per_pixel + 0x07) >> 3;
    const uint32_t scanline_buffer_size = (settings.scanline.stride + scanline_pixel_byte_count);
    uint8_t *scanline_buffers = calloc(scanline_buffer_size * 2, sizeof(uint8_t));
    settings.scanline.buffer = scanline_buffers;
    settings.scanline.buffer_size = scanline_buffer_size * 2;
    settings.scanline.new = scanline_buffers + settings.scanline.stride;
    settings.scanline.last = settings.scanline.new + scanline_buffer_size;
    settings.scanline.index = 0;

    for (size_t i = 0; i < test_image_size; ++i)
    {
        filter(test_image[i], &output, &settings);
    }

    munit_assert_memory_equal(source_image_size, source_image, output.data);

    free(test_image);
    free(source_image);
    free(output.data);
    free(scanline_buffers);
    return MUNIT_OK;
}

MunitResult filter_1_test(const MunitParameter params[], void *data)
{
    (void)params;
    (void)data;

    struct output_settings_t *settings = filter_setup();
    struct data_buffer_t output = {
        .data = calloc(TEST_FILTER_SIZE, sizeof(uint8_t)),
        .index = 0};

    munit_rand_memory(2 * TEST_FILTER_SIZE + 2, settings->scanline.buffer);
    settings->scanline.index = 0;

    filter(1, &output, settings);
    filter(settings->scanline.new[0], &output, settings);
    for (int i = 1; i < TEST_FILTER_SIZE; ++i)
    {
        filter(settings->scanline.new[i] - settings->scanline.new[i - 1], &output, settings);
    }

    munit_assert_memory_equal(TEST_FILTER_SIZE, output.data, settings->scanline.new);

    free(output.data);
    return MUNIT_OK;
}

MunitResult filter_2_test(const MunitParameter params[], void *data)
{
    (void)params;
    (void)data;

    struct output_settings_t *settings = filter_setup();
    struct data_buffer_t output = {
        .data = calloc(TEST_FILTER_SIZE, sizeof(uint8_t)),
        .index = 0};

    munit_rand_memory(2 * TEST_FILTER_SIZE + 2, settings->scanline.buffer);
    settings->scanline.index = 0;

    filter(2, &output, settings);

    filter(settings->scanline.new[0], &output, settings);
    for (int i = 1; i < TEST_FILTER_SIZE; ++i)
    {
        filter(settings->scanline.new[i] - settings->scanline.last[i], &output, settings);
    }

    munit_assert_memory_equal(TEST_FILTER_SIZE, output.data, settings->scanline.new);

    free(output.data);
    return MUNIT_OK;
}

MunitResult filter_3_test(const MunitParameter params[], void *data)
{
    (void)params;
    (void)data;

    struct output_settings_t *settings = filter_setup();
    struct data_buffer_t output = {
        .data = calloc(TEST_FILTER_SIZE, sizeof(uint8_t)),
        .index = 0};

    munit_rand_memory(2 * TEST_FILTER_SIZE + 2, settings->scanline.buffer);
    settings->scanline.index = 0;

    filter(3, &output, settings);

    filter(settings->scanline.new[0], &output, settings);
    for (int i = 1; i < TEST_FILTER_SIZE; ++i)
    {
        filter(settings->scanline.new[i] - ((settings->scanline.new[i - 1] + settings->scanline.last[i]) >> 1), &output, settings);
    }

    munit_assert_memory_equal(TEST_FILTER_SIZE, output.data, settings->scanline.new);

    free(output.data);
    return MUNIT_OK;
}

MunitResult filter_4_test(const MunitParameter params[], void *data)
{
    (void)params;
    (void)data;

    struct output_settings_t *settings = filter_setup();
    struct data_buffer_t output = {
        .data = calloc(TEST_FILTER_SIZE, sizeof(uint8_t)),
        .index = 0};

    munit_rand_memory(2 * TEST_FILTER_SIZE + 2, settings->scanline.buffer);
    settings->scanline.index = 0;

    filter(4, &output, settings);

    filter(settings->scanline.new[0], &output, settings);
    for (int i = 1; i < TEST_FILTER_SIZE; ++i)
    {
        int p = settings->scanline.new[i - 1] + settings->scanline.last[i] - settings->scanline.last[i - 1];
        int pa = abs(p - settings->scanline.new[i - 1]);
        int pb = abs(p - settings->scanline.last[i]);
        int pc = abs(p - settings->scanline.last[i - 1]);

        filter(pa <= pb && pa <= pc ? pa : pb <= pc ? pb : pc, &output, settings);
    }

    munit_assert_memory_equal(TEST_FILTER_SIZE, output.data, settings->scanline.new);

    free(output.data);
    return MUNIT_OK;
}