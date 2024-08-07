#include "include/png.h"

#include "include/logger.h"

#ifdef __MINGW32__
#define OS_TARGET "windows"
#include <windows.h>

typedef struct image_t image;
typedef int(__cdecl *f_load_png_t)(const char *filename, struct image_t *output);
typedef void(__cdecl *f_close_png_t)(struct image_t *image);
#endif

#ifdef __linux__
#define OS_TARGET "linux"
#include <dlfcn.h>

typedef int (*f_load_png_t)(const char *filename, struct image_t *output);
typedef void (*f_close_png_t)(struct image_t *image);
#endif

#include <stdio.h>

void export_ppm(struct image_t *image)
{
    FILE *fp = fopen("png_decoder_test.ppm", "wb"); /* b - binary mode */
    FILE *fp_alpha = fopen("png_decoder_test_alpha.ppm", "wb");

    fprintf(fp, "P6\n%d %d\n255\n", image->width, image->height);
    fprintf(fp_alpha, "P6\n%d %d\n255\n", image->width, image->height);

    uint32_t inc = image->bit_depth == 16 ? 2 : 1;
    switch (image->mode)
    {
    case G:
        for (uint32_t i = 0; i < image->size; i += inc)
        {
            uint8_t px[3] = {image->data[i], image->data[i], image->data[i]};
            fwrite(px, 3, 1, fp);
        }
        break;
    case RGB:
        for (uint32_t i = 0; i < image->size; i += 3 * inc)
        {
            fwrite(image->data + i, 1, 1, fp);
            fwrite(image->data + i + inc, 1, 1, fp);
            fwrite(image->data + i + 2 * inc, 1, 1, fp);
        }
        break;
    case GA:
        for (uint32_t i = 0; i < image->size; i += 2 * inc)
        {
            uint8_t px[3] = {image->data[i], image->data[i], image->data[i]};
            fwrite(px, 3, 1, fp);
            uint8_t px_alpha[3] = {image->data[i + inc], image->data[i + inc], image->data[i + inc]};
            fwrite(px_alpha, 3, 1, fp_alpha);
        }
        break;
    case RGBA:
        for (uint32_t i = 0; i < image->size; i += 4 * inc)
        {
            fwrite(image->data + i, 1, 1, fp);
            fwrite(image->data + i + inc, 1, 1, fp);
            fwrite(image->data + i + 2 * inc, 1, 1, fp);
            uint8_t px_alpha[3] = {image->data[i + 3 * inc], image->data[i + 3 * inc], image->data[i + 3 * inc]};
            fwrite(px_alpha, 3, 1, fp_alpha);
        }
        break;
    case INVALID:
        break;
    }
    fclose(fp);
    fclose(fp_alpha);
}

int main(int argc, char *argv[])
{
    (void) argc;
    char os_msg[32];
    sprintf(os_msg ,"OS: %s", OS_TARGET);
    log_debug(os_msg);

    f_load_png_t f_load_png;
    f_close_png_t f_close_png;

#ifdef __MINGW32__
    HINSTANCE hGetProcIDDLL = LoadLibrary("./libpng.dll");

    if (!hGetProcIDDLL)
    {
        log_error("Could not load the dynamic library");
        return EXIT_FAILURE;
    }

#pragma GCC diagnostic ignored "-Wcast-function-type"
    f_load_png = (f_load_png_t)GetProcAddress(hGetProcIDDLL, "load_png");
    f_close_png = (f_close_png_t)GetProcAddress(hGetProcIDDLL, "close_png");
#pragma GCC diagnostic pop

    if (!f_load_png || !f_close_png)
    {
        log_error("Could not locate dll functions");
        return EXIT_FAILURE;
    }
    log_debug("f_load_png() and f_close_png() loaded from dll");
#endif

#ifdef __linux__
    void *handle = dlopen("./libpng.so", RTLD_LAZY | RTLD_LOCAL);

    if (!handle)
    {
        log_error("Could not load the dynamic library");
        return -1;
    }

    *(void **)(&f_load_png) = dlsym(handle, "load_png");
    *(void **)(&f_close_png) = dlsym(handle, "close_png");

    if (!f_load_png || !f_close_png)
    {
        log_error("Could not locate dll functions");
        return -1;
    }

    log_debug("f_load_png() and f_close_png() loaded from so");
#endif

    struct image_t test;
    log_info("PNG dynamic loader");
    log_info("==================");
    f_load_png(argv[1], &test);
    log_debug("\tWidth: %4d", test.width);
    log_debug("\tHeight: %04d", test.height);
    log_debug("\tMode: %d", test.mode);
    log_debug("\tBit depth: %d", test.bit_depth);
    log_debug("\tSize: %d", test.size);
    export_ppm(&test);
    f_close_png(&test);
    return 0;
}