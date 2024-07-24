#ifndef _FILTERTESTS_
#define _FILTERTESTS_

#include "munit.h"
#include "filter.h"

MunitResult interlacing_setup_test(const MunitParameter params[], void *png_data);
MunitResult deinterlacing_test(const MunitParameter params[], void *data);
MunitResult filter_1_test(const MunitParameter params[], void *data);
MunitResult filter_2_test(const MunitParameter params[], void *data);
MunitResult filter_3_test(const MunitParameter params[], void *data);
MunitResult filter_4_test(const MunitParameter params[], void *data);

#endif