#include <stdint.h>
#include "png_utils.h"

#define ZLIB_HEADER_SIZE 2
#define ZLIB_ADLER32_SIZE 4

enum zliberr_t { ZLIB_NO_ERR=0, ZLIB_FCHECK_FAIL, ZLIB_UNSUPPORTED_CM, ZLIB_INVALID_CM, ZLIB_INVALID_CINFO, ZLIB_UNSUPPORTED_FDICT };

enum zliberr_t zlib_header_check(const uint8_t* data);
int deflate(struct stream_ptr_t sp, size_t len, struct png_header_t header, uint8_t *output);