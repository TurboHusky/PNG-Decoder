#include "zlib_tests.h"

void zlib_callback_stub(uint8_t byte, struct data_buffer_t *output_image, void *output_settings)
{
    (void)output_settings;
    output_image->data[output_image->index] = byte;
    output_image->index++;
}

MunitResult zlib_uncompressed_test(const MunitParameter params[], void *uncompressed_png_data)
{
    (void)params;

    struct zlib_t zlib;
    struct stream_ptr_t bitstream;
    struct data_buffer_t output;
    int zlib_callback_settings;

    bitstream.data = (const uint8_t *)uncompressed_png_data;
    bitstream.data += 0x29;
    bitstream.size = 0x0C63 - 0x0029;
    bitstream.byte_index = 0;
    bitstream.bit_index = 0;

    zlib.state = READING_ZLIB_HEADER;
    zlib.bytes_read = 0;
    adler32_init(&zlib.adler32);

    output.data = calloc(0x0C20, sizeof(uint8_t));
    output.index = 0;

    int result = decompress_zlib(&zlib, &bitstream, &output, zlib_callback_stub, &zlib_callback_settings);

    munit_assert_int(result, ==, READ_COMPLETE);

    munit_assert_uint8(zlib.header.CINFO, ==, 7);
    munit_assert_uint8(zlib.header.CM, ==, 8);
    munit_assert_uint8(zlib.header.FLEVEL, ==, 3);
    munit_assert_uint8(zlib.header.FDICT, ==, 0);
    munit_assert_uint8(zlib.header.FCHECK, ==, 26);
    
    munit_assert_uint8(zlib.block_header.BFINAL, ==, 0x01);
    munit_assert_uint8(zlib.block_header.BTYPE, ==, 0x00);
    munit_assert_uint16(zlib.block_header.LEN, ==, 0xC20);
    munit_assert_uint16(zlib.block_header.NLEN, ==, 0xF3DF);

    munit_assert_size(bitstream.byte_index, ==, 0x0C27);
    munit_assert_size(output.index, ==, zlib.block_header.LEN);
    for (int i = 0; i < 128; i++)
    {
        int test_index = munit_rand_int_range(0, 0x0C20 - 1);
        munit_assert_uint8(output.data[test_index], ==, bitstream.data[test_index + 7]);
    }

    free(output.data);
    
    return MUNIT_OK;
}

MunitResult zlib_compressed_static_test(const MunitParameter params[], void *uncompressed_png_data)
{
    (void)params;

    struct zlib_t zlib;
    struct stream_ptr_t bitstream;
    struct data_buffer_t output;
    int zlib_callback_settings;

    bitstream.data = (const uint8_t *)uncompressed_png_data;
    bitstream.data += 0x39;
    bitstream.size = 0x0067 - 0x0039;
    bitstream.byte_index = 0;
    bitstream.bit_index = 0;

    zlib.state = READING_ZLIB_HEADER;
    zlib.bytes_read = 0;
    adler32_init(&zlib.adler32);

    output.data = calloc(288, sizeof(uint8_t));
    output.index = 0;

    int result = decompress_zlib(&zlib, &bitstream, &output, zlib_callback_stub, &zlib_callback_settings);

    munit_assert_int(result, ==, READ_COMPLETE);

    munit_assert_uint8(zlib.header.CINFO, ==, 7);
    munit_assert_uint8(zlib.header.CM, ==, 8);
    munit_assert_uint8(zlib.header.FLEVEL, ==, 2);
    munit_assert_uint8(zlib.header.FDICT, ==, 0);
    munit_assert_uint8(zlib.header.FCHECK, ==, 28);
    
    munit_assert_uint8(zlib.block_header.BFINAL, ==, 0x01);
    munit_assert_uint8(zlib.block_header.BTYPE, ==, 0x01);

    munit_assert_size(bitstream.byte_index, ==, 26);
    munit_assert_size(output.index, ==, 288);

    free(output.data);
    
    return MUNIT_OK;
}

MunitResult zlib_compressed_dynamic_test(const MunitParameter params[], void *uncompressed_png_data)
{
    (void)params;

    struct zlib_t zlib;
    struct stream_ptr_t bitstream;
    struct data_buffer_t output;
    int zlib_callback_settings;

    bitstream.data = (const uint8_t *)uncompressed_png_data;
    bitstream.data += 0x29;
    bitstream.size = 0x00DF - 0x0029;
    bitstream.byte_index = 0;
    bitstream.bit_index = 0;

    zlib.state = READING_ZLIB_HEADER;
    zlib.bytes_read = 0;
    adler32_init(&zlib.adler32);
    zlib.LZ77_buffer.data = malloc(1024);

    output.data = calloc(3104, sizeof(uint8_t));
    output.index = 0;

    int result = decompress_zlib(&zlib, &bitstream, &output, zlib_callback_stub, &zlib_callback_settings);

    munit_assert_int(result, ==, READ_COMPLETE);

    munit_assert_uint8(zlib.header.CINFO, ==, 7);
    munit_assert_uint8(zlib.header.CM, ==, 8);
    munit_assert_uint8(zlib.header.FLEVEL, ==, 3);
    munit_assert_uint8(zlib.header.FDICT, ==, 0);
    munit_assert_uint8(zlib.header.FCHECK, ==, 26);
    
    munit_assert_uint8(zlib.block_header.BFINAL, ==, 0x01);
    munit_assert_uint8(zlib.block_header.BTYPE, ==, 0x02);
    munit_assert_uint8(zlib.block_header.HLIT, ==, 0x16);
    munit_assert_uint8(zlib.block_header.HDIST, ==, 0x11);
    munit_assert_uint8(zlib.block_header.HCLEN, ==, 0x0E);

    munit_assert_size(bitstream.byte_index, ==, 162);
    munit_assert_size(output.index, ==, 3104);

    free(zlib.LZ77_buffer.data);
    free(output.data);
    
    return MUNIT_OK;
}

MunitResult zlib_btype_error_test(const MunitParameter params[], void *png_data)
{
    (void)png_data;

    int expected_result = ZLIB_BAD_HEADER;
    uint8_t CM = 8;
    uint8_t CINFO = munit_rand_int_range(0, 7);
    uint8_t FDICT = 0;
    uint8_t FLEVEL = munit_rand_int_range(0,3);
    uint16_t temp = (CINFO << 12) + (CM << 8) + (FLEVEL << 6) + (FDICT << 5);
    uint8_t FCHECK = (0x1F - (temp % 0x1F));
    uint8_t deflate_header_bits = 0x01;

    uint8_t cm_invalid[] = {0,1,2,3,4,5,6,7,9,10,11,12,13,14};
    uint8_t bad_fcheck = ~FCHECK & 0x17;

    switch(params[0].value[0])
    {
        case '1':
            CM = 15;
            break;
        case '2':
            CM = cm_invalid[munit_rand_int_range(0, 13)];
            break;
        case '3':
            CINFO = munit_rand_int_range(8, 15);
            break;
        case '4':
            FDICT = 1;
            break;
        case '5':
            FCHECK = bad_fcheck;
            break;
        case '6':
            deflate_header_bits = 0x07;
            expected_result = ZLIB_BAD_DEFLATE_HEADER;
            break;
        default: break;   
    }

    struct stream_ptr_t bitstream;
    struct zlib_t zlib;
    struct data_buffer_t output;
    int zlib_callback_settings;

    uint8_t test_data[4] = {0};
    test_data[0] = (CINFO << 4) | CM;
    test_data[1] = (FLEVEL << 6) | (FDICT << 5) | FCHECK;
    test_data[2] = deflate_header_bits;
    bitstream.data = test_data;
    bitstream.size = 4;
    bitstream.byte_index = 0;
    bitstream.bit_index = 0;
    zlib.state = READING_ZLIB_HEADER;

    int result = decompress_zlib(&zlib, &bitstream, &output, zlib_callback_stub, &zlib_callback_settings);
    munit_assert_int(result, ==, expected_result);

    return MUNIT_OK;
}