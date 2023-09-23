cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)

project(png_decoder LANGUAGES C VERSION 1.0 DESCRIPTION "PNG decoder")

# Find packages

include_directories(../include)
add_compile_options(-Wall -Wextra -Werror -Wpedantic -Winline -std=c17)
add_executable(png zlib.c filter.c png.c)
# set_property(TARGET png PROPERTY C_STANDARD C17) # Needs cmake 3.21 to recognise C17