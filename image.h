#include <stdlib.h>
#include <stdint.h>

struct interlacing_t{
    uint32_t width;
    uint32_t height;
}; // 56 bytes

void set_interlacing(struct interlacing_t *sub_images, uint32_t width, uint32_t height);