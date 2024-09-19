#include "test_utils.h"

#include <stdio.h>
#include <string.h>

static void *load_png(const char *filename)
{
    FILE *png_file = fopen(filename, "rb");

    fseek(png_file, 0L, SEEK_END);
    long filesize = ftell(png_file); // Not POSIX compliant
    fseek(png_file, 0L, SEEK_SET);
    uint8_t *data = calloc(filesize, sizeof(uint8_t));
    fread(data, sizeof(uint8_t), filesize, png_file);
    fclose(png_file);

    return data;
}

static char *build_path(const char *path, size_t path_len, const char *filename, char *buffer)
{
    memcpy(buffer, path, path_len);
    if (path[path_len - 1] != '/')
    {
        buffer[path_len] = '/';
        ++path_len;
    }
    memcpy(buffer + path_len, filename, strlen(filename) + 1);
    return buffer;
}

void *load_png_no_compression(const MunitParameter params[], void *user_data)
{
    (void)user_data;
    size_t path_len = strlen(params->value);
    char *image_path = malloc(path_len + 14);

    // Image: 32px x 32px, truecolour, bit depth = 8
    // Data per line: 1 byte for filter type, 32 x 3 x 8 bits = 96 bytes for pixel data
    // Decompressed image: 97 x 32 = 3104 bytes
    void *data = load_png(build_path(params->value, path_len, "z00n2c08.png", image_path));

    free(image_path);
    return data;
}

void *load_png_fixed_compression(const MunitParameter params[], void *user_data)
{
    (void)user_data;
    size_t path_len = strlen(params->value);
    char *image_path = malloc(path_len + 14);

    // Image: 32px x 32px, greyscale, bit depth = 2
    // Data per line: 1 byte for filter type, 32 x 2 bits = 8 bytes for pixel data
    // Decompressed image: 9 x 32 = 288 bytes
    void *data = load_png(build_path(params->value, path_len, "basn0g02.png", image_path));

    free(image_path);
    return data;
}

void *load_png_dynamic_compression(const MunitParameter params[], void *user_data)
{
    (void)user_data;
    size_t path_len = strlen(params->value);
    char *image_path = malloc(path_len + 14);

    // Image: 32px x 32px, truecolour, bit depth = 8
    // Data per line: 1 byte for filter type, 32 x 3 x 8 bits = 96 bytes for pixel data
    // Decompressed image: 97 x 32 = 3104 bytes
    void *data = load_png(build_path(params->value, path_len, "z09n2c08.png", image_path));

    free(image_path);
    return data;
}

void *load_png_interlaced(const MunitParameter params[], void *user_data)
{
    (void)user_data;
    size_t path_len = strlen(params->value);
    char *image_path = malloc(path_len + 14);

    // Image: 32px x 32px, greyscale, bit depth = 1
    // Data per line: 1 byte for filter type, 32 x 1 x 1 bits = 5 bytes for pixel data
    // Decompressed image: 5 x 32 = 160 bytes
    void *data = load_png(build_path(params->value, path_len, "basi0g01.png", image_path));

    free(image_path);
    return data;
}

void close_png(void *fixture)
{
    free(fixture);
}