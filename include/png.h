#include <stdio.h>
#include <stdint.h>

enum pixel_format_t
{
    INVALID=0,
    G=1,
    GA,
    RGB,
    RGBA
};

struct image_t
{
    uint8_t *data;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    enum pixel_format_t mode;
};

void debug_image(const struct image_t *image);

int load_png(const char *filename, struct image_t *output);
void close_png(struct image_t *image);