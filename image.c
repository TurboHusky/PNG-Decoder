#include "image.h"

void set_interlacing(struct interlacing_t *sub_images, uint32_t width, uint32_t height)
{
    sub_images->width = (width + 7) >> 3;
    sub_images->height = (height + 7) >> 3;
    (sub_images + 1)->width = (width + 3) >> 3;
    (sub_images + 1)->height = sub_images->height;
    (sub_images + 2)->width = (width + 3) >> 2;
    (sub_images + 2)->height = (height + 3) >> 3;
    (sub_images + 3)->width = (width + 1) >> 2;
    (sub_images + 3)->height = (height + 3) >> 2;
    (sub_images + 4)->width = (width + 1) >> 1;
    (sub_images + 4)->height = (height + 1) >> 2;
    (sub_images + 5)->width = width >> 1;
    (sub_images + 5)->height = (height + 1) >> 1;
    (sub_images + 6)->width = width;
    (sub_images + 6)->height = height >> 1;
}