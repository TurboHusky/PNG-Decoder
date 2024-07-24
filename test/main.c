#include "test_utils.h"
#include "zlib_tests.h"
#include "filter_tests.h"

static char* header_tests[] = { "1 - Unsupported compression method", "2 - Invalid compression method", "3 - Invalid LZ77 window", "4 - PNG cannot use dictionary", "5 - Bad check bits", "6 - Bad DEFLATE type", NULL };
static MunitParameterEnum zlib_header_params[] = {{"Bad header test", header_tests}, {NULL, NULL}};

MunitTest png_tests[] = {
    {"/zlib/uncompressed", zlib_uncompressed_test, load_png_no_compression, close_png, MUNIT_TEST_OPTION_NONE, NULL},
    {"/zlib/compressed_static", zlib_compressed_static_test, load_png_fixed_compression, close_png, MUNIT_TEST_OPTION_NONE, NULL},
    {"/zlib/compressed_dynamic", zlib_compressed_dynamic_test, load_png_dynamic_compression, close_png, MUNIT_TEST_OPTION_NONE, NULL},
    {"/zlib/btype_error", zlib_btype_error_test, NULL, NULL, MUNIT_TEST_OPTION_NONE, zlib_header_params},
    {"/filter/interlacing_setup", interlacing_setup_test, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/filter/deinterlacing", deinterlacing_test, load_png_interlaced, close_png, MUNIT_TEST_OPTION_NONE, NULL},
    {"/filter/type 1", filter_1_test, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/filter/type 2", filter_2_test, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/filter/type 3", filter_3_test, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {"/filter/type 4", filter_4_test, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite = {
    "/PNG_Tests", png_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, const char *argv[])
{
    char *user_data = "Pass to setup/tests\n";

    return munit_suite_main(&test_suite, user_data, argc, (char *const *)argv);
}