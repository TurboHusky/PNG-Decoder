#include <stdint.h>
#include "png_utils.h"

enum zlib_status_t
{ 
   ZLIB_IDLE=0, 
   ZLIB_BUSY, 
   ZLIB_BAD_HEADER, 
   ZLIB_BAD_DEFLATE_HEADER, 
   ZLIB_ADLER32_FAILED, 
   ZLIB_ADLER32_CHECKSUM_MISSING,
   ZLIB_COMPLETE
};

int decompress_zlib(struct stream_ptr_t *bitstream, uint8_t *output);