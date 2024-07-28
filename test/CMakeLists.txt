cmake_minimum_required(VERSION 3.21)

project(png_decoder_unit_tests LANGUAGES C VERSION 0.0.1 DESCRIPTION "PNG decoder Unit Tests")

# Output directories configured in CMakePresets.json

##  Unit Tests ##
list(APPEND TEST_SOURCE_FILES main.c munit/munit.c src/test_utils.c src/filter_tests.c src/zlib_tests.c)
add_executable(test)

target_include_directories(test PRIVATE munit include ../include)
target_sources(test PRIVATE ${TEST_SOURCE_FILES})
target_link_libraries(test PUBLIC png)
set_target_properties(test PROPERTIES C_STANDARD 17)