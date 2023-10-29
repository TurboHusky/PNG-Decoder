cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)

project(png_decoder LANGUAGES C VERSION 1.0 DESCRIPTION "PNG decoder")

# Find packages

add_compile_options(-Wall -Wextra -Werror -Wpedantic -Winline -std=c17)
list(APPEND LIB_SOURCE_FILES png.c filter.c zlib.c)
add_library(png SHARED)

target_include_directories(png PRIVATE ../include)
target_sources(png PRIVATE ${LIB_SOURCE_FILES})
set_target_properties(png PROPERTIES VERSION ${PROJECT_VERSION} C_STANDARD 17 PUBLIC_HEADER ../include/png.h) # RUNTIME_OUTPUT_DIRECTORY /home/Pictures