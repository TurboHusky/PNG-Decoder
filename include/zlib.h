#include <stdint.h>
#include "png_utils.h"

#define HLIT_MAX 286
#define HDIST_MAX 32

struct zlib_header_t
{
   uint8_t CM : 4;
   uint8_t CINFO : 4;
   uint8_t FCHECK : 5;
   uint8_t FDICT : 1;
   uint8_t FLEVEL : 2;
};

struct block_header_t
{
   uint8_t BFINAL;
   uint8_t BTYPE;
   uint16_t LEN;
   uint16_t NLEN;
   uint8_t HLIT;
   uint8_t HDIST;
   uint8_t HCLEN;
};

enum zlib_status_t
{
   ZLIB_COMPLETE = 0,
   ZLIB_INCOMPLETE,
   ZLIB_BAD_HEADER,
   ZLIB_BAD_DEFLATE_HEADER,
   ZLIB_ADLER32_FAILED,
   ZLIB_ADLER32_CHECKSUM_MISSING
};

struct huffman_decoder_t
{
   uint8_t bitlength;
   uint16_t threshold;
   uint16_t offset;
};

struct dynamic_block_t
{
   enum block_state_t
   {
      READ_CODE_LENGTHS,
      READ_HUFFMAN_CODES,
      READ_DATA
   } state;

   struct huffman_decoder_t code_length_decoder[7];
   uint16_t code_length_lookup[19];

   uint16_t lit_dist_codes[HLIT_MAX + HDIST_MAX];
   int code_total;
   int code_count;

   struct huffman_decoder_t lit_decoder[15];
   uint16_t lit_lookup[HLIT_MAX];
   struct huffman_decoder_t dist_decoder[15];
   uint16_t dist_lookup[HDIST_MAX];
}; // ~1548 bytes

struct zlib_stream_t
{
   struct zlib_header_t zlib_header;
   struct block_header_t block_header;

   enum zlib_state_t
   {
      READING_ZLIB_HEADER,
      READING_INFLATE_BLOCK_HEADER,
      READING_INFLATE_BLOCK_DATA,
      READING_ADLER32_CHECKSUM
   } state;

   uint8_t *LZ77_buffer;

   struct dynamic_block_t dynamic_block;

   uint8_t *zlib_output_index;
};

int decompress_zlib(struct stream_ptr_t *bitstream, uint8_t *output);