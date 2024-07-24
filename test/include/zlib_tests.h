#ifndef _ZLIBTESTS_
#define _ZLIBTESTS_

#include "munit.h"
#include "zlib.h"

void zlib_callback_stub(uint8_t byte, struct data_buffer_t *output_image, void *output_settings);

MunitResult zlib_uncompressed_test(const MunitParameter params[], void *uncompressed_png_data);
MunitResult zlib_compressed_static_test(const MunitParameter params[], void *uncompressed_png_data);
MunitResult zlib_compressed_dynamic_test(const MunitParameter params[], void *uncompressed_png_data);
MunitResult zlib_btype_error_test(const MunitParameter params[], void *png_data);

#endif