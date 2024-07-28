cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)

project(png_decoder LANGUAGES C VERSION 0.0.1 DESCRIPTION "PNG decoder")

# Output directories configured in CMakePresets.json

# Find packages

add_compile_options(-Wall -Wextra -Werror -Wpedantic -Winline -std=c17)

##  Library ##
list(APPEND LIB_SOURCE_FILES logger.c png.c filter.c zlib.c)

if(BUILD_SHARED_LIBS)
add_compile_options(-Wl,-rpath,${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(LIB_TYPE "SHARED")
else()
set(LIB_TYPE "STATIC")
endif()

add_library(png ${LIB_TYPE})

target_include_directories(png PRIVATE ../include)
target_sources(png PRIVATE ${LIB_SOURCE_FILES})
set_target_properties(png PROPERTIES VERSION ${PROJECT_VERSION} C_STANDARD 17 PUBLIC_HEADER ../include/png.h) # RUNTIME_OUTPUT_DIRECTORY /home/Pictures

install(TARGETS png LIBRARY DESTINATION ../build/bin PUBLIC_HEADER DESTINATION ../build/include)

if(NOT BUILD_RUNTIME)

## Application ##
list(APPEND APP_SOURCE_FILES main.c)
add_executable(png2ppm)

target_include_directories(png2ppm PRIVATE ../include)
target_sources(png2ppm PRIVATE ${APP_SOURCE_FILES})
target_link_libraries(png2ppm PUBLIC png)
set_target_properties(png2ppm PROPERTIES C_STANDARD 17)

endif()