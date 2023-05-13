#include <stdint.h>
#include "png_utils.h"

struct stream_ptr_t
{
   const uint8_t *data;
   size_t size;
   size_t byte_index;
   uint8_t bit_index;
};

enum zlib_status_t
{ 
   ZLIB_COMPLETE=0, 
   ZLIB_BUSY, 
   ZLIB_BAD_HEADER, 
   ZLIB_BAD_DEFLATE_HEADER, 
   ZLIB_ADLER32_FAILED, 
   ZLIB_ADLER32_CHECKSUM_MISSING
};

int decompress_zlib(struct stream_ptr_t *bitstream, uint8_t *output);