#include "png.h"

#ifdef __MINGW32__
#define OS_TARGET "windows"
#endif

#ifdef __linux__
#define OS_TARGET "linux"
#endif

void export_ppm(struct image_t *image)
{
    FILE *fp = fopen("png_decoder_test.ppm", "wb"); /* b - binary mode */
    FILE *fp_alpha = fopen("png_decoder_test_alpha.ppm", "wb");

    (void)fprintf(fp, "P6\n%d %d\n255\n", image->width, image->height);
    (void)fprintf(fp_alpha, "P6\n%d %d\n255\n", image->width, image->height);

    uint32_t inc = image->bit_depth == 16 ? 2 : 1;
    switch (image->mode)
    {
    case G:
        for (uint32_t i = 0; i < image->size; i += inc)
        {
            uint8_t px[3] = {image->data[i], image->data[i], image->data[i]};
            (void)fwrite(px, 3, 1, fp);
        }
        break;
    case RGB:
        for (uint32_t i = 0; i < image->size; i += 3 * inc)
        {
            (void)fwrite(image->data + i, 1, 1, fp);
            (void)fwrite(image->data + i + inc, 1, 1, fp);
            (void)fwrite(image->data + i + 2 * inc, 1, 1, fp);
        }
        break;
    case GA:
        for (uint32_t i = 0; i < image->size; i += 2 * inc)
        {
            uint8_t px[3] = {image->data[i], image->data[i], image->data[i]};
            (void)fwrite(px, 3, 1, fp);
            uint8_t px_alpha[3] = {image->data[i + inc], image->data[i + inc], image->data[i + inc]};
            (void)fwrite(px_alpha, 3, 1, fp_alpha);
        }
        break;
    case RGBA:
        for (uint32_t i = 0; i < image->size; i += 4 * inc)
        {
            (void)fwrite(image->data + i, 1, 1, fp);
            (void)fwrite(image->data + i + inc, 1, 1, fp);
            (void)fwrite(image->data + i + 2 * inc, 1, 1, fp);
            uint8_t px_alpha[3] = {image->data[i + 3 * inc], image->data[i + 3 * inc], image->data[i + 3 * inc]};
            (void)fwrite(px_alpha, 3, 1, fp_alpha);
        }
        break;
    }
    (void)fclose(fp);
    (void)fclose(fp_alpha);
}

int main(int argc, char *argv[])
{
    (void)argv[argc - 1];
    printf("OS: %s\n", OS_TARGET);

    struct image_t test;
    if (load_png(argv[1], &test) == 0)
    {
        export_ppm(&test);
    }
    close_png(&test);

    return 0;
}