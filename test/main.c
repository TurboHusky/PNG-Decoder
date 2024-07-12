#include "zlib_tests.h"

static char* header_tests[] = { "1 - Unsupported compression method", "2 - Invalid compression method", "3 - Invalid LZ77 window", "4 - PNG cannot use dictionary", "5 - Bad check bits", "6 - Bad DEFLATE type", NULL };
static MunitParameterEnum zlib_header_params[] = {{"Bad header test", header_tests}, {NULL, NULL}};

MunitTest zlib_tests[] = {
    {"/zlib_uncompressed_test", zlib_uncompressed_test, load_png_no_compression, close_test_png, MUNIT_TEST_OPTION_NONE, NULL},
    {"/zlib_compressed_static_test", zlib_compressed_static_test, load_png_fixed_compression, close_test_png, MUNIT_TEST_OPTION_NONE, NULL},
    {"/zlib_compressed_dynamic_test", zlib_compressed_dynamic_test, load_png_dynamic_compression, close_test_png, MUNIT_TEST_OPTION_NONE, NULL},
    {"/zlib_btype_error_test", zlib_btype_error_test, NULL, NULL, MUNIT_TEST_OPTION_NONE, zlib_header_params},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite = {
    "/zlib", zlib_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, const char *argv[])
{
    char *user_data = "Pass to setup/tests\n";

    return munit_suite_main(&test_suite, user_data, argc, (char *const *)argv);
}