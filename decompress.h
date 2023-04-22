#include <stdint.h>
#include "png_utils.h"

enum decompression_status_t
{
   STREAM_STATUS_IDLE,
   STREAM_STATUS_BUSY,
   STREAM_STATUS_PARTIAL,
   STREAM_STATUS_COMPLETE
};

struct stream_ptr_t
{
   uint8_t *data;
   size_t size;
   size_t byte_index;
   uint8_t bit_index;
   struct
   {
      uint8_t status;
      uint32_t adler32;
   } zlib;
   struct
   {
      uint8_t status;
      uint8_t BFINAL;
      uint8_t BTYPE;
      struct
      {
         struct
         {
            uint16_t LEN;
         } header;
         size_t bytes_read;
      } uncompressed;
      struct
      {
         struct
         {
            uint8_t HCLEN;
            uint8_t HLIT;
            uint8_t HDIST;
         } header;
      } dynamic;
   } inflate;
};

int decompress(struct stream_ptr_t *bitstream, uint8_t *output);