cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)

include(CMakePrintHelpers)

project(png2ppm_runtime LANGUAGES C VERSION 1.0 DESCRIPTION "PNG shared library runtime loader")

add_subdirectory(src)

add_compile_options(-Wall -Wextra -Werror -Wpedantic -Winline -std=c17)

if(BUILD_RUNTIME)

list(APPEND SOURCE_FILES main.c src/logger.c)
add_executable(png2ppm)

target_include_directories(png2ppm PRIVATE ./include)
target_sources(png2ppm PRIVATE ${SOURCE_FILES})
set_target_properties(png2ppm PROPERTIES C_STANDARD 17)

install(TARGETS png2ppm DESTINATION build/bin)

endif()