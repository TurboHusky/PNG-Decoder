cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)

project(png_decoder LANGUAGES C VERSION 1.0 DESCRIPTION "PNG decoder")

list(APPEND SOURCE_FILES main.c png.c filter.c zlib.c)

# Find packages

include_directories(../include)
add_compile_options(-Wall -Wextra -Werror -Wpedantic -Winline -std=c17)
add_executable(png)

target_sources(png PRIVATE ${SOURCE_FILES})

set_property(TARGET png PROPERTY C_STANDARD 17)