#ifndef _DATALOADERS_
#define _DATALOADERS_

#include "munit.h"

void *load_png_interlaced(const MunitParameter params[], void *user_data);
void *load_png_no_compression(const MunitParameter params[], void *user_data);
void *load_png_fixed_compression(const MunitParameter params[], void *user_data);
void *load_png_dynamic_compression(const MunitParameter params[], void *user_data);
void close_png(void *fixture);

#endif