#include <stdint.h>
#include "png_utils.h"

struct stream_ptr_t
{
   const uint8_t *data;
   size_t size;
   size_t byte_index;
   uint8_t bit_index;
};

void stream_init(struct stream_ptr_t *stream, const uint8_t *data, const size_t size);
int decompress(struct stream_ptr_t *bitstream, uint8_t *output);