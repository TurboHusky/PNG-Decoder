#include <stdint.h>
#include "png_utils.h"

struct stream_ptr_t
{
   const uint8_t *data;
   size_t size;
   size_t byte_index;
   uint8_t bit_index;
};

int decompress_zlib(struct stream_ptr_t *bitstream, uint8_t *output);