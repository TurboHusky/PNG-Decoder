#include "zlib.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "adler32.h"

#define HLIT_MAX 286
#define HLIT_OFFSET 257
#define HDIST_MAX 32
#define HDIST_OFFSET 1
#define HCLEN_MAX 19
#define HCLEN_OFFSET 4
#define HCLEN_BITS 3
#define CM_DEFLATE 8
#define CM_RESERVED 15
#define CINFO_WINDOW_MAX 7
#define DISTANCE_BITS 5
#define ZLIB_HEADER_SIZE sizeof(struct zlib_header_t)
#define ZLIB_ADLER32_SIZE sizeof(uint32_t)

enum zlib_header_status_t
{
   ZLIB_HEADER_NO_ERR = 0,
   ZLIB_HEADER_INCOMPLETE,
   ZLIB_HEADER_FCHECK_FAIL,
   ZLIB_HEADER_UNSUPPORTED_CM,
   ZLIB_HEADER_INVALID_CM,
   ZLIB_HEADER_INVALID_CINFO,
   ZLIB_HEADER_UNSUPPORTED_FDICT
};

enum inflate_status_t
{
   READ_COMPLETE,
   READ_INCOMPLETE,
   READ_ERROR
};

static inline enum zlib_header_status_t zlib_header_check(struct stream_ptr_t *bitstream, struct zlib_header_t *zlib_header)
{
   if (bitstream->size - bitstream->byte_index < sizeof(struct zlib_header_t))
   {
      return ZLIB_HEADER_INCOMPLETE;
   }

   *zlib_header = *(struct zlib_header_t *) bitstream->data;
   bitstream->byte_index += ZLIB_HEADER_SIZE;
   
   uint16_t fcheck_result = (((*(uint8_t *) zlib_header) << 8) | *(((uint8_t *) zlib_header) + 1)) % 31;
   printf("\tCINFO: %02X\n\tCM: %02X\n\tFLEVEL: %02X\n\tFDICT: %02X\n\tFCHECK: %02X (%d)\n", zlib_header->CINFO, zlib_header->CM, zlib_header->FLEVEL, zlib_header->FDICT, zlib_header->FCHECK, fcheck_result);
   
   if (zlib_header->CM != CM_DEFLATE)
   {
      if (zlib_header->CM == CM_RESERVED)
      {
         printf("zlib error: Unsupported compression method specified in header\n");
         return ZLIB_HEADER_UNSUPPORTED_CM;
      }
      else
      {
         printf("zlib error: Invalid compression method specified in header\n");
         return ZLIB_HEADER_INVALID_CM;
      }
   }
   if (zlib_header->CINFO > CINFO_WINDOW_MAX)
   {
      printf("zlib error: Invalid window size specified in header\n");
      return ZLIB_HEADER_INVALID_CINFO;
   }
   if (zlib_header->FDICT)
   {
      printf("zlib error: Dictionary cannot be specified for PNG in header\n");
      return ZLIB_HEADER_UNSUPPORTED_FDICT;
   }
   if (fcheck_result)
   {
      printf("zlib error: FCHECK failed\n");
      return ZLIB_HEADER_FCHECK_FAIL;
   }

   return ZLIB_HEADER_NO_ERR;
}

typedef struct extra_bits
{
   uint16_t value;
   uint8_t extra;
} alphabet_t;

static const alphabet_t length_alphabet[30] = {
    {0, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0}, {11, 1}, {13, 1}, {15, 1}, {17, 1}, {19, 2}, {23, 2}, {27, 2}, {31, 2}, {35, 3}, {43, 3}, {51, 3}, {59, 3}, {67, 4}, {83, 4}, {99, 4}, {115, 4}, {131, 5}, {163, 5}, {195, 5}, {227, 5}, {258, 0}}; // Lookup for 257-285

static const alphabet_t distance_alphabet[30] = {
    {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 1}, {7, 1}, {9, 2}, {13, 2}, {17, 3}, {25, 3}, {33, 4}, {49, 4}, {65, 5}, {97, 5}, {129, 6}, {193, 6}, {257, 7}, {385, 7}, {513, 8}, {769, 8}, {1025, 9}, {1537, 9}, {2049, 10}, {3073, 10}, {4097, 11}, {6145, 11}, {8193, 12}, {12289, 12}, {16385, 13}, {24577, 13}};

static inline uint8_t reverse_byte(uint8_t n)
{
   static const uint8_t reverse_nibble_lookup[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};
   return (reverse_nibble_lookup[n & 0x0F] << 4) | reverse_nibble_lookup[n >> 4];
}

enum inflate_status_t read_block_header(struct stream_ptr_t *bitstream, struct block_header_t *header)
{
   enum block_type_t { INFLATE_UNCOMPRESSED = 0, INFLATE_FIXED, INFLATE_DYNAMIC, INFLATE_ERROR };

   uint16_t block_header = *(uint16_t *)(bitstream->data + bitstream->byte_index);
   block_header = block_header >> bitstream->bit_index;

   header->BFINAL = block_header & 0x01;
   header->BTYPE = (block_header >> 1) & 0x03;
   stream_add_bits(bitstream, 3);

   if(bitstream->byte_index >= bitstream->size)
   {
      stream_remove_bits(bitstream, 3);
      return READ_INCOMPLETE;
   }

   printf("\tINFLATE:\n\t\tBFINAL: %01x\n\t\tBTYPE: %02x\n", header->BFINAL, header->BTYPE);

   if(header->BTYPE == INFLATE_UNCOMPRESSED)
   {
      uint8_t unused_bit_len = (-bitstream->bit_index) & 0x07;
      stream_add_bits(bitstream, unused_bit_len);

      if ((bitstream->size - bitstream->byte_index) < (sizeof(header->LEN) + sizeof(header->NLEN)))
      {
         printf("Incomplete block, cannot load LEN/NLEN\n");
         stream_remove_bits(bitstream, 3 + unused_bit_len);
         return READ_INCOMPLETE; // Incomplete block, load next chunk to continue
      }

      header->LEN = *(uint16_t *)(bitstream->data + bitstream->byte_index);
      header->NLEN = *(uint16_t *)(bitstream->data + bitstream->byte_index + sizeof(header->LEN));
      bitstream->byte_index += sizeof(header->LEN) + sizeof(header->NLEN);
      printf("\t\tLEN: %04X\t(%d bytes)\n\t\tNLEN: %04X\n", header->LEN, header->LEN, header->NLEN);
      if ((header->LEN ^ header->NLEN) != 0xFFFF)
      {
         printf("LEN/NLEN check failed for uncompressed block\n");
         return READ_ERROR;
      }
   }
   else if (header->BTYPE == INFLATE_DYNAMIC)
   {
      if ((bitstream->size - bitstream->byte_index) < 3)
      {
         printf("Incomplete block, cannot load HLIT/HDIST/HCLEN\n");
         stream_remove_bits(bitstream, 3);
         return READ_INCOMPLETE;
      }
      uint32_t input = (*(uint32_t *)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
      header->HLIT = input & 0x1f;
      header->HDIST = (input >> 5) & 0x001f;
      header->HCLEN = (input >> 10) & 0x000f;
      stream_add_bits(bitstream, 14);
      printf("\t\tHLIT: %d\n\t\tHDIST: %d\n\t\tHCLEN: %d\n", header->HLIT, header->HDIST, header->HCLEN);
   }
   else if (header->BTYPE == INFLATE_ERROR)
   {
      printf("Invalid BTYPE in block header\n");
      return READ_ERROR;
   }

   return READ_COMPLETE;
}

enum inflate_status_t inflate_uncompressed(struct zlib_t *zlib, struct stream_ptr_t *bitstream, struct data_buffer_t *output, zlib_callback cb, void *payload)
{
   while (output->index < zlib->block_header.LEN && bitstream->byte_index < bitstream->size)
   {
      cb(bitstream->data[bitstream->byte_index], payload);
      output->data[output->index] = bitstream->data[bitstream->byte_index];
      adler32_update(&zlib->adler32, bitstream->data[bitstream->byte_index]);
      bitstream->byte_index++;
      output->index++;
   }

   return output->index < zlib->block_header.LEN ? READ_INCOMPLETE : READ_COMPLETE;
}

enum inflate_status_t inflate_fixed(struct zlib_t *zlib, struct stream_ptr_t *bitstream, struct data_buffer_t *output, zlib_callback cb, void *payload)
{
   (void) zlib;
   union dbuf input;
   alphabet_t length;
   alphabet_t distance;

   while (1)
   {
      input.u8[1] = reverse_byte(bitstream->data[bitstream->byte_index]);
      input.u8[0] = reverse_byte(bitstream->data[bitstream->byte_index + 1]);
      input.u16[0] <<= bitstream->bit_index;

      uint8_t bits_read = (input.u8[1] < 48) ? 7 : (input.u8[1] < 200) ? 8 : 9;
      stream_add_bits(bitstream, bits_read);

      if ((bitstream->byte_index < bitstream->size) || (bitstream->byte_index == bitstream->size && bitstream->bit_index == 0))
      {
         if (bits_read == 7) // 7-bit length, 1-47 maps to 257 - 279
         {
            length = length_alphabet[(input.u8[1] >> 1)]; // map to 0-23 for lookup
         }
         else if (bits_read == 9) // 9-bit literals, 200-255 maps to 144-255
         {
            input.u16[0] <<= 1;
            zlib->LZ77_buffer.data[zlib->LZ77_buffer.index] = input.u8[1];
            cb(input.u8[1], payload);
            output->data[output->index] = input.u8[1];
            adler32_update(&zlib->adler32, input.u8[1]);
            increment_ring_buffer(&zlib->LZ77_buffer);
            output->index++;
            continue;
         }
         else if (input.u8[1] < 192) // 8-bit literals, 48-191 maps to 0-143
         {
            zlib->LZ77_buffer.data[zlib->LZ77_buffer.index] = input.u8[1] - 48;
            cb(input.u8[1] - 48, payload);
            output->data[output->index] = input.u8[1] - 48;
            adler32_update(&zlib->adler32, input.u8[1] - 48);
            increment_ring_buffer(&zlib->LZ77_buffer);
            output->index++;
            continue;
         }
         else // 8-bit length, 192-197 maps to 280-285 (286|287 unused)
         {
            length = length_alphabet[input.u8[1] - 168]; // map to 23-28 for lookup
         }

         if (length.value == 0)
         {
            printf("\tEnd of data code read.\n");
            return READ_COMPLETE;
         }

         input.u16[0] = *(uint16_t*)(bitstream->data + bitstream->byte_index);
         input.u16[0] >>= bitstream->bit_index;
         length.value += input.u8[0] & (0xff >> (8 - length.extra));
         stream_add_bits(bitstream, length.extra);

         input.u8[1] = reverse_byte(bitstream->data[bitstream->byte_index]);
         input.u8[0] = reverse_byte(bitstream->data[bitstream->byte_index + 1]);
         input.u16[0] <<= bitstream->bit_index;
         distance = distance_alphabet[input.u8[1] >> (8 - DISTANCE_BITS)];
         stream_add_bits(bitstream, DISTANCE_BITS);

         input.u32 = *(uint32_t *)(bitstream->data + bitstream->byte_index);
         input.u32 >>= bitstream->bit_index;
         distance.value += input.u16[0] & (0xffff >> (16 - distance.extra));
         stream_add_bits(bitstream, distance.extra);

         if (bitstream->byte_index >= bitstream->size)
         {
            stream_remove_bits(bitstream, bits_read + length.extra + DISTANCE_BITS + distance.extra);
            return READ_INCOMPLETE;
         }

         uint16_t zlib_distance_index = (zlib->LZ77_buffer.index - distance.value) & zlib->LZ77_buffer.mask;
         for(int i=0; i<length.value; i++)
         {
            zlib->LZ77_buffer.data[zlib->LZ77_buffer.index] = zlib->LZ77_buffer.data[zlib_distance_index];
            cb(zlib->LZ77_buffer.data[zlib_distance_index], payload);
            output->data[output->index] = zlib->LZ77_buffer.data[zlib_distance_index];
            adler32_update(&zlib->adler32, zlib->LZ77_buffer.data[zlib_distance_index]);
            zlib_distance_index = (zlib_distance_index + 1) & zlib->LZ77_buffer.mask;
            increment_ring_buffer(&zlib->LZ77_buffer);
            output->index++;
         }         
      }
      else
      {
         stream_remove_bits(bitstream, bits_read);
         return READ_INCOMPLETE;
      }
   }
}

struct huffman_data_t {
   uint16_t code;
   uint16_t value;
   uint8_t index;
};

static inline struct huffman_data_t huffman_read(struct stream_ptr_t *bitstream, const struct huffman_decoder_t *decoder, const uint16_t *lookup)
{
   union dbuf input;

   input.u8[3] = reverse_byte(*(bitstream->data + bitstream->byte_index));
   input.u8[2] = reverse_byte(*(bitstream->data + bitstream->byte_index + 1));
   input.u8[1] = reverse_byte(*(bitstream->data + bitstream->byte_index + 2));
   input.u32 <<= bitstream->bit_index;

   struct huffman_data_t result;
   result.index = -1;
   do
   {
      result.index++;
      result.code = input.u16[1] >> (16 - decoder[result.index].bitlength);
   } while (result.code >= decoder[result.index].threshold && decoder[result.index].bitlength);

   result.value = lookup[result.code - decoder[result.index].offset];

   return result;
}

void build_huffman_lookup(const uint16_t *input, const uint16_t input_size, uint16_t *lookup, const size_t lookup_size, struct huffman_decoder_t *decoder, const size_t decoder_size)
{
   struct
   {
      uint16_t count;
      uint16_t start;
      uint16_t total;
   } temp[MAX_HUFFMAN_CODE_BITS + 1] = {0};

   memset(lookup, 0, sizeof(uint16_t) * lookup_size);
   memset(decoder, 0, sizeof(struct huffman_decoder_t) * decoder_size);

   for (int i = 0; i < input_size; i++)
   {
      temp[input[i]].count++;
   }
   temp[0].count = 0;
   int j = 0;
   for (size_t i = 1; i <= decoder_size; i++)
   {
      temp[i].start = (temp[i - 1].start + temp[i - 1].count) << 1;
      temp[i].total = temp[i - 1].total + temp[i].count;
      if (temp[i].count)
      {
         decoder[j].bitlength = i;
         decoder[j].offset = temp[i].start - temp[i - 1].total;
         decoder[j].threshold = decoder[j].offset + temp[i].total;
         j++;
      }
   }

   size_t decoder_index = 0;
   size_t lookup_index = 0;
   while (decoder[decoder_index].bitlength && lookup_index < lookup_size && decoder_index < decoder_size)
   {
      for (size_t i = 0; i < input_size; i++)
      {
         if (input[i] == decoder[decoder_index].bitlength)
         {
            lookup[lookup_index] = i;
            lookup_index++;
         }
      }
      decoder_index++;
   }
}

enum inflate_status_t inflate_dynamic(struct zlib_t *zlib, struct stream_ptr_t *bitstream, struct data_buffer_t *output, zlib_callback cb, void *payload)
{
   if (zlib->dynamic_block.state == READ_CODE_LENGTHS)
   {
      size_t code_length_size = ((zlib->block_header.HCLEN + HCLEN_OFFSET) * HCLEN_BITS + 0x07) >> 3;

      if(bitstream->byte_index + code_length_size > bitstream->size)
      {
         printf("\tIncomplete block, cannot load all code length values\n");
         return READ_INCOMPLETE;
      }

      printf("\tReading code lengths\n");
      uint16_t code_length_codes[HCLEN_MAX] = {0};
      uint8_t code_order[HCLEN_MAX] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

      for (int i = 0; i < zlib->block_header.HCLEN + HCLEN_OFFSET; i++)
      {
         code_length_codes[code_order[i]] = (*(uint16_t *)(bitstream->data + bitstream->byte_index) >> bitstream->bit_index) & 0x07;
         stream_add_bits(bitstream, 3);
      }

      build_huffman_lookup(code_length_codes, HCLEN_MAX, zlib->dynamic_block.code_length_lookup, CODE_LENGTH_MAX, zlib->dynamic_block.code_length_decoder, MAX_CODE_LENGTH_BITS);

      zlib->dynamic_block.code_size = zlib->block_header.HLIT + HLIT_OFFSET + zlib->block_header.HDIST + HDIST_OFFSET;
      zlib->dynamic_block.code_count = 0;
      memset(zlib->dynamic_block.lit_dist_codes, 0, sizeof(uint16_t) * (HLIT_MAX + HDIST_MAX));
      zlib->dynamic_block.state = READ_HUFFMAN_CODES;
   }

   if (zlib->dynamic_block.state == READ_HUFFMAN_CODES)
   {
      printf("\tReading Huffman codes\n");
      struct huffman_data_t huffman_code;
      uint16_t code_length;
      uint8_t repeat;

      while (zlib->dynamic_block.code_count < zlib->dynamic_block.code_size)
      {
         huffman_code = huffman_read(bitstream, zlib->dynamic_block.code_length_decoder, zlib->dynamic_block.code_length_lookup);
         stream_add_bits(bitstream, zlib->dynamic_block.code_length_decoder[huffman_code.index].bitlength);

         if ((bitstream->byte_index < bitstream->size) || (bitstream->byte_index == bitstream->size && bitstream->bit_index == 0))
         {
            union dbuf input;
            if (huffman_code.value <= 15)
            {
               zlib->dynamic_block.lit_dist_codes[zlib->dynamic_block.code_count] = huffman_code.value;
               zlib->dynamic_block.code_count++;
               continue;
            }
            if (huffman_code.value == 16)
            {
               input.u16[0] = (*(uint16_t *)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
               code_length = zlib->dynamic_block.lit_dist_codes[zlib->dynamic_block.code_count - 1];
               repeat = (input.u8[0] & 0x03) + 3;
               stream_add_bits(bitstream, 2);            
            }
            else if (huffman_code.value == 17)
            {
               input.u16[0] = (*(uint16_t *)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
               code_length = 0;
               repeat = (input.u8[0] & 0x07) + 3;
               stream_add_bits(bitstream, 3);
            }
            else if (huffman_code.value == 18)
            {
               input.u16[0] = (*(uint16_t *)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
               code_length = 0;
               repeat = (input.u8[0] & 0x7f) + 11;
               stream_add_bits(bitstream, 7);
            }
            else
            {
               printf("\tError, invalid Huffman code length detected.\n");
               return READ_ERROR;
            }
            if (bitstream->byte_index < bitstream->size)
            {
               for (int i = 0; i < repeat; i++)
               {
                  zlib->dynamic_block.lit_dist_codes[zlib->dynamic_block.code_count] = code_length;
                  zlib->dynamic_block.code_count++;
               }
            }
            else
            {
               uint8_t bit_offset = (huffman_code.value == 16) ? 2 : (huffman_code.value == 17) ? 3 : (huffman_code.value == 18) ? 7 : 0;
               bit_offset += zlib->dynamic_block.code_length_decoder[huffman_code.index].bitlength;
               stream_remove_bits(bitstream, bit_offset);
               return READ_INCOMPLETE;
            }
         }
         else
         {
            stream_remove_bits(bitstream, zlib->dynamic_block.code_length_decoder[huffman_code.index].bitlength);
            return READ_INCOMPLETE;            
         }
      }

      if (zlib->dynamic_block.code_count > zlib->dynamic_block.code_size)
      {  
         printf("\tError, too many Huffman codes read\n");
         return READ_ERROR;
      }

      printf("\tBuilding Huffman alphabets\n");
      build_huffman_lookup(zlib->dynamic_block.lit_dist_codes, zlib->block_header.HLIT + HLIT_OFFSET, zlib->dynamic_block.lit_lookup, HLIT_MAX, zlib->dynamic_block.lit_decoder, MAX_HUFFMAN_CODE_BITS);
      build_huffman_lookup(zlib->dynamic_block.lit_dist_codes + zlib->block_header.HLIT + HLIT_OFFSET, zlib->block_header.HDIST + HDIST_OFFSET, zlib->dynamic_block.dist_lookup, HDIST_MAX, zlib->dynamic_block.dist_decoder, MAX_HUFFMAN_CODE_BITS);
      zlib->dynamic_block.state = READ_DATA;
   }

   if (zlib->dynamic_block.state == READ_DATA)
   {
      struct huffman_data_t huff_code;
      struct huffman_data_t huff_code_distance;
      alphabet_t huff_length = {0};
      alphabet_t huff_distance = {0};

      while (1)
      {
         huff_code = huffman_read(bitstream, zlib->dynamic_block.lit_decoder, zlib->dynamic_block.lit_lookup);
         stream_add_bits(bitstream, zlib->dynamic_block.lit_decoder[huff_code.index].bitlength);

         if ((bitstream->byte_index < bitstream->size) || (bitstream->byte_index == bitstream->size && bitstream->bit_index == 0))
         {
            huff_length.value = huff_length.extra = 0;
            huff_distance.value = huff_distance.extra = 0;

            union dbuf input;
            if (huff_code.value == 256)
            {
               printf("\tEnd of data code read.\n");
               zlib->dynamic_block.state = READ_CODE_LENGTHS;
               return READ_COMPLETE;
            }
            if (huff_code.value < 256)
            {
               zlib->LZ77_buffer.data[zlib->LZ77_buffer.index] = huff_code.value;
               cb((uint8_t)huff_code.value, payload);
               output->data[output->index] = huff_code.value;
               adler32_update(&zlib->adler32, (uint8_t) huff_code.value);
               increment_ring_buffer(&zlib->LZ77_buffer);
               output->index++;
               continue;
            }
            if (huff_code.value < 286)
            {
               huff_length = length_alphabet[huff_code.value - 256];
               if (huff_length.value > 10) // Extra length bits
               {
                  input.u16[0] = (*(uint16_t*)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
                  huff_length.value += (input.u8[0] & (0xff >> (8 - huff_length.extra)));
                  stream_add_bits(bitstream, huff_length.extra);
               }         

               huff_code_distance = huffman_read(bitstream, zlib->dynamic_block.dist_decoder, zlib->dynamic_block.dist_lookup);
               stream_add_bits(bitstream, zlib->dynamic_block.dist_decoder[huff_code_distance.index].bitlength);

               huff_distance = distance_alphabet[huff_code_distance.value];
               if (huff_distance.value > 4) // Extra distance bits
               {
                  input.u32 = (*(uint32_t*)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
                  huff_distance.value += (input.u16[0] & (0xffff >> (16 - huff_distance.extra)));
                  stream_add_bits(bitstream, huff_distance.extra);
               }

               if(bitstream->byte_index < bitstream->size)
               {
                  uint16_t zlib_distance_index = (zlib->LZ77_buffer.index - huff_distance.value) & zlib->LZ77_buffer.mask;
                  for(int i=0; i<huff_length.value; i++)
                  {
                     uint8_t temp = zlib->LZ77_buffer.data[zlib_distance_index];
                     zlib->LZ77_buffer.data[zlib->LZ77_buffer.index] = temp;
                     cb(temp, payload);
                     output->data[output->index] = temp;
                     adler32_update(&zlib->adler32, temp);
                     increment_ring_buffer(&zlib->LZ77_buffer);
                     zlib_distance_index = (zlib_distance_index + 1) & zlib->LZ77_buffer.mask;
                     output->index++;
                  }
               }
               else
               {
                  stream_remove_bits(bitstream, zlib->dynamic_block.lit_decoder[huff_code.index].bitlength + huff_length.extra + zlib->dynamic_block.dist_decoder[huff_code_distance.index].bitlength + huff_distance.extra);
                  return READ_INCOMPLETE;
               }
            }
            else
            {
               printf("Error, invalid literal/length value.\n");
               return READ_ERROR;
            }
         }
         else
         {
            stream_remove_bits(bitstream, zlib->dynamic_block.lit_decoder[huff_code.index].bitlength);
            return READ_INCOMPLETE;
         }
      }
   }
   return READ_ERROR;
}

enum inflate_status_t btype_error(struct zlib_t *zlib, struct stream_ptr_t *bitstream, struct data_buffer_t *output, zlib_callback cb, void *payload)
{
   (void)zlib;
   (void)bitstream;
   (void)output;
   (void)cb;
   (void)payload;
   printf("Invalid BTYPE flag\n");
   return READ_ERROR;
}

typedef enum inflate_status_t (*block_read_t)(struct zlib_t *zlib, struct stream_ptr_t *bitstream, struct data_buffer_t *output, zlib_callback cb, void *payload);

int decompress_zlib(struct zlib_t *zlib, struct stream_ptr_t *bitstream, struct data_buffer_t *output, zlib_callback cb, void *payload)
{
   if (zlib->state == READING_ZLIB_HEADER)
   {
      enum zlib_header_status_t zlib_header_status = zlib_header_check(bitstream, &zlib->header);
      if (zlib_header_status == ZLIB_HEADER_INCOMPLETE)
      {
         return ZLIB_INCOMPLETE;
      }
      else if (zlib_header_status != ZLIB_HEADER_NO_ERR)
      {
         printf("zlib header check failed\n");
         return ZLIB_BAD_HEADER;
      }
      printf("\tSet LZ77 buffer to %u bytes\n", 0x0100 << zlib->header.CINFO);
      uint16_t lz77_size = 0x0100 << zlib->header.CINFO;
      zlib->LZ77_buffer.data = realloc(zlib->LZ77_buffer.data, lz77_size);
      zlib->LZ77_buffer.mask = lz77_size - 1;
      zlib->state = READING_INFLATE_BLOCK_HEADER;
   }

   enum inflate_status_t block_read_result = READ_INCOMPLETE;
   do
   {
      if (zlib->state == READING_INFLATE_BLOCK_HEADER)
      {
         if (read_block_header(bitstream, &zlib->block_header) != 0)
         {
            printf("deflate header block read failed\n");
            return ZLIB_BAD_DEFLATE_HEADER;
         }
         zlib->state = READING_INFLATE_BLOCK_DATA;
      }

      if (zlib->state == READING_INFLATE_BLOCK_DATA)
      {
         block_read_t read_block_data[4] = {inflate_uncompressed, inflate_fixed, inflate_dynamic, btype_error};
         block_read_result = read_block_data[zlib->block_header.BTYPE](zlib, bitstream, output, cb, payload );
         if(block_read_result == READ_COMPLETE)
         {
            zlib->state = zlib->block_header.BFINAL ? READING_ADLER32_CHECKSUM : READING_INFLATE_BLOCK_HEADER;
         }
      }
   } while (block_read_result == READ_COMPLETE && !zlib->block_header.BFINAL);

   if (zlib->state == READING_ADLER32_CHECKSUM)
   {
      size_t adler32_index = (bitstream->bit_index == 0) ? bitstream->byte_index : bitstream->byte_index + 1;
      if ((bitstream->size - adler32_index) < ZLIB_ADLER32_SIZE)
      {
         printf("zlib incomplete checksum\n");
         return ZLIB_ADLER32_CHECKSUM_MISSING;
      }

      uint32_t adler32_check = order_png32_t(*(uint32_t*)(bitstream->data + adler32_index));
      printf("zlib complete\n");
      if(zlib->adler32.checksum != adler32_check)
      {
         printf("zlib adler32 checksum failed\n");
         return ZLIB_ADLER32_FAILED;
      }
      return ZLIB_COMPLETE;
   }

   return ZLIB_INCOMPLETE;
}