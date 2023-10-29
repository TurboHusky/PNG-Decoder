cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)

project(png_loader LANGUAGES C VERSION 1.0 DESCRIPTION "PNG shared library loader")

add_subdirectory(src)

add_compile_options(-Wall -Wextra -Werror -Wpedantic -Winline -std=c17)
list(APPEND SOURCE_FILES main.c)
add_executable(png_loader)

target_include_directories(png_loader PRIVATE ./include)
target_sources(png_loader PRIVATE ${SOURCE_FILES})
set_target_properties(png_loader PROPERTIES C_STANDARD 17)
