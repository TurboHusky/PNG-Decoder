#include "zlib.h"

#include <stdio.h>
#include "adler32.h"

#define ALPHABET_SIZE 16
#define ALPHABET_LIMIT 0x8000
#define UNUSED_CODE_LENGTH 0x80
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
#define DISTANCE_SIZE 5
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

struct zlib_header_t
{
   uint8_t CM : 4;
   uint8_t CINFO : 4;
   uint8_t FCHECK : 5;
   uint8_t FDICT : 1;
   uint8_t FLEVEL : 2;
};

static inline enum zlib_header_status_t zlib_header_check(struct stream_ptr_t *bitstream, struct zlib_header_t *zlib_header)
{
   if (bitstream->size - bitstream->byte_index < sizeof(struct zlib_header_t))
   {
      return ZLIB_HEADER_INCOMPLETE;
   }

   zlib_header = (struct zlib_header_t *) bitstream->data;
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

static const alphabet_t length_alphabet[29] = {
    {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0}, {11, 1}, {13, 1}, {15, 1}, {17, 1}, {19, 2}, {23, 2}, {27, 2}, {31, 2}, {35, 3}, {43, 3}, {51, 3}, {59, 3}, {67, 4}, {83, 4}, {99, 4}, {115, 4}, {131, 5}, {163, 5}, {195, 5}, {227, 5}, {258, 0}}; // Lookup for 257-285

static const alphabet_t distance_alphabet[30] = {
    {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 1}, {7, 1}, {9, 2}, {13, 2}, {17, 3}, {25, 3}, {33, 4}, {49, 4}, {65, 5}, {97, 5}, {129, 6}, {193, 6}, {257, 7}, {385, 7}, {513, 8}, {769, 8}, {1025, 9}, {1537, 9}, {2049, 10}, {3073, 10}, {4097, 11}, {6145, 11}, {8193, 12}, {12289, 12}, {16385, 13}, {24577, 13}};

static inline uint8_t reverse_byte(uint8_t n)
{
   static const uint8_t reverse_nibble_lookup[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};
   return (reverse_nibble_lookup[n & 0x0F] << 4) | reverse_nibble_lookup[n >> 4];
}

struct block_header_t {
   uint8_t BFINAL;
   uint8_t BTYPE;
   uint16_t LEN;
   uint16_t NLEN;
   uint8_t HLIT;
   uint8_t HDIST;
   uint8_t HCLEN;
};

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

enum inflate_status_t inflate_uncompressed(struct stream_ptr_t *bitstream, struct block_header_t *block_header, struct data_buffer_t *output)
{
   while (output->index < block_header->LEN && bitstream->byte_index < bitstream->size)
   {
      output->data[output->index] = *(bitstream->data + bitstream->byte_index);
      bitstream->byte_index++;
      output->index++;
   }

   return output->index < block_header->LEN ? READ_INCOMPLETE : READ_COMPLETE;
}

enum inflate_status_t inflate_fixed(struct stream_ptr_t *bitstream, struct block_header_t *block_header, struct data_buffer_t *output)
{
   (void) block_header;
   union dbuf input;
   alphabet_t length, distance;

   while (bitstream->byte_index < bitstream->size)
   {
      input.u8[1] = reverse_byte(*(bitstream->data + bitstream->byte_index));
      input.u8[0] = reverse_byte(*(bitstream->data + bitstream->byte_index + 1));
      input.u16[0] <<= bitstream->bit_index;

      length.value = length.extra = 0;
      distance.value = distance.extra = 0;

      if (input.u8[1] < 2) // Exit code is 7 MSB bits 0, ignore LSB 0|1
      {
         if(bitstream->byte_index < bitstream->size)
         {
            printf("\tEnd of data code read.\n");
            stream_add_bits(bitstream, 7);
            return READ_COMPLETE;
         }
         else
         {
            return READ_INCOMPLETE;
         }
      }
      else if (input.u8[1] < 48) // 7-bit codes, 1-47 maps to 257 - 279
      {
         length = length_alphabet[(input.u8[1] >> 1) - 1]; // map to 0-22 for lookup
         stream_add_bits(bitstream, 7);
      }
      else if (input.u8[1] < 192) // 8-bit literals, 48-191 maps to 0-143
      {
         output->data[output->index] = input.u8[1] - 48;
         output->index++;
         stream_add_bits(bitstream, 8);
      }
      else if (input.u8[1] < 200) // 8-bit codes, 192-197 maps to 280-285 (286|287 unused)
      {
         length = length_alphabet[input.u8[1] - 169]; // map to 23-28 for lookup
         stream_add_bits(bitstream, 8);
      }
      else // 9-bit literals, 200-255, read extra bit and map to 144-255
      {
         input.u16[0] <<= 1;
         output->data[output->index] = input.u8[1];
         output->index++;
         stream_add_bits(bitstream, 9);
      }

      if (length.value)
      {
         input.u16[0] = *(uint16_t*)(bitstream->data + bitstream->byte_index);
         input.u16[0] >>= bitstream->bit_index;

         length.value += input.u8[0] & (0xff >> (8 - length.extra));
         stream_add_bits(bitstream, length.extra);

         input.u8[1] = reverse_byte(*(bitstream->data + bitstream->byte_index));
         input.u8[0] = reverse_byte(*(bitstream->data + bitstream->byte_index + 1));
         input.u16[0] <<= bitstream->bit_index;
         
         distance = distance_alphabet[input.u8[1] >> (8 - DISTANCE_SIZE)];
         stream_add_bits(bitstream, DISTANCE_SIZE);

         input.u32 = *(uint32_t *)(bitstream->data + bitstream->byte_index);
         input.u32 >>= bitstream->bit_index;

         distance.value += input.u16[0] & (0xffff >> (16 - distance.extra));
         stream_add_bits(bitstream, distance.extra);

         if (bitstream->byte_index >= bitstream->size)
         {
            break;
         }

         for(int i=0; i<length.value; i++)
         {
            output->data[output->index] = output->data[output->index - distance.value];
            output->index++;
         }
      }
   }

   if (bitstream->byte_index >= bitstream->size)
   {
      size_t bit_offset = 7;
      if (length.value)
      {
         printf("\tDiscarding invalid length/distance.\n");
         bit_offset = (length.value < 280) ? 7 : 8;
         bit_offset += length.extra + DISTANCE_SIZE + distance.extra;
      }
      else
      {
         printf("\tDiscarding invalid literal value.\n");
         output->index--;
         bit_offset = (output->data[output->index] < 144) ? 8 : 9;
      }

      stream_remove_bits(bitstream, bit_offset);
   }

   return READ_INCOMPLETE;
}

struct huffman_t
{
   uint8_t bitlength;
   uint16_t threshold;
   uint16_t offset;
};

static inline uint16_t huffman_read(struct stream_ptr_t *bitstream, struct huffman_t *decoder, uint16_t *lookup)
{
   union dbuf input;

   input.u8[3] = reverse_byte(*(bitstream->data + bitstream->byte_index));
   input.u8[2] = reverse_byte(*(bitstream->data + bitstream->byte_index + 1));
   input.u8[1] = reverse_byte(*(bitstream->data + bitstream->byte_index + 2));
   input.u32 <<= bitstream->bit_index;

   uint8_t decoder_index = -1;
   uint16_t huffman_code_in;
   do
   {
      decoder_index++;
      huffman_code_in = input.u16[1] >> (16 - decoder[decoder_index].bitlength);
   } while (huffman_code_in >= decoder[decoder_index].threshold && decoder[decoder_index].bitlength);

   stream_add_bits(bitstream, decoder[decoder_index].bitlength);

   return lookup[huffman_code_in - decoder[decoder_index].offset];
}

void build_huffman_lookup(const uint16_t *input, const uint16_t input_size, uint16_t *lookup, struct huffman_t *decoder, const uint8_t decoder_length)
{
   struct
   {
      uint16_t count;
      uint16_t start;
      uint16_t total;
   } temp[16] = {0};

   for (int i = 0; i < input_size; i++)
   {
      temp[input[i]].count++;
   }
   temp[0].count = 0;
   int j = 0;
   for (int i = 1; i < decoder_length; i++)
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

   int i = 0;
   int lookup_index = 0;
   while (decoder[i].bitlength)
   {
      for (int j = 0; j < input_size; j++)
      {
         if (input[j] == decoder[i].bitlength)
         {
            lookup[lookup_index] = j;
            lookup_index++;
         }
      }
      i++;
   }
}

enum inflate_status_t inflate_dynamic(struct stream_ptr_t *bitstream, struct block_header_t *block_header, struct data_buffer_t *output)
{
   static enum
   {
      READ_CODE_LENGTHS,
      READ_HUFFMAN_CODES,
      READ_DATA
   } state = READ_CODE_LENGTHS;
   union dbuf input;

   static uint16_t code_length_lookup[19] = {0};
   static struct huffman_t code_length_decoder[7] = {0};
   static int code_length_count;

   if (state == READ_CODE_LENGTHS)
   {
      printf("\tReading code lengths\n");
      size_t code_length_bytes = ((block_header->HCLEN + HCLEN_OFFSET) * HCLEN_BITS + 0x07) >> 3;

      if(bitstream->byte_index + code_length_bytes > bitstream->size)
      {
         printf("\tIncomplete block, cannot load all code length values\n");
         return READ_INCOMPLETE;
      }

      uint16_t code_length[HCLEN_MAX] = {0};
      uint8_t code_order[HCLEN_MAX] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

      for (int i = 0; i < block_header->HCLEN + HCLEN_OFFSET; i++)
      {
         code_length[code_order[i]] = (*(uint16_t *)(bitstream->data + bitstream->byte_index) >> bitstream->bit_index) & 0x07;
         stream_add_bits(bitstream, 3);
      }

      build_huffman_lookup(code_length, HCLEN_MAX, code_length_lookup, code_length_decoder, 8);

      code_length_count = 0;
      state = READ_HUFFMAN_CODES;
   }

   static uint16_t lit_dist_code_length[HLIT_MAX + HDIST_MAX] = {0};
   int code_total = block_header->HLIT + HLIT_OFFSET + block_header->HDIST + HDIST_OFFSET;
   uint16_t lookup_result;
   alphabet_t code_length_huffman = {0};

   if (state == READ_HUFFMAN_CODES)
   {
      printf("\tReading Huffman codes\n");
      while (code_length_count < code_total && bitstream->byte_index < bitstream->size)
      {
         lookup_result = huffman_read(bitstream, code_length_decoder, code_length_lookup);

         if (lookup_result <= 15)
         {
            lit_dist_code_length[code_length_count] = lookup_result;
            code_length_count++;
         }
         else if (lookup_result <= 18)
         {
            input.u16[0] = (*(uint16_t *)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
            if (lookup_result == 16)
            {
               code_length_huffman.value = lit_dist_code_length[code_length_count - 1];
               code_length_huffman.extra = (input.u8[0] & 0x03) + 3;
               stream_add_bits(bitstream, 2);
            }
            else if (lookup_result == 17)
            {
               code_length_huffman.value = 0;
               code_length_huffman.extra = (input.u8[0] & 0x07) + 3;
               stream_add_bits(bitstream, 3);
            }
            else
            {
               code_length_huffman.value = 0;
               code_length_huffman.extra = (input.u8[0] & 0x7f) + 11;
               stream_add_bits(bitstream, 7);
            }
            if (bitstream->byte_index < bitstream->size)
            {
               for (int i = 0; i < code_length_huffman.extra; i++)
               {
                  lit_dist_code_length[code_length_count] = code_length_huffman.value;
                  code_length_count++;
               }
            }
         }
         else
         {
            printf("Error, invalid Huffman code detected.\n");
            return READ_ERROR;
         }
      }

      if (bitstream->byte_index >= bitstream->size)
      {
         int reverse_lookup_index = 0;
         while(code_length_lookup[reverse_lookup_index] != lookup_result)
         {
            reverse_lookup_index++;
         }
         int reverse_decoder_index = 0;
         while(reverse_lookup_index + code_length_decoder[reverse_decoder_index].offset >= code_length_decoder[reverse_decoder_index].threshold)
         {
            reverse_decoder_index++;
         }

         uint8_t bit_offset = code_length_decoder[reverse_decoder_index].bitlength;
         if (lookup_result < 16)
         {
            code_length_count--;
         }
         else if (lookup_result == 16)
         {
            bit_offset += 2;
         }
         else if (lookup_result == 17)
         {
            bit_offset += 3;
         }
         else if (lookup_result == 18)
         {
            bit_offset += 7;
         }
         stream_remove_bits(bitstream, bit_offset);

         printf("\tEnd of buffer, read %d of %d codes\n", code_length_count, code_total);
         return READ_INCOMPLETE;
      }
      state = READ_DATA;
   }

   uint16_t lit_lookup[HLIT_MAX] = {0};
   struct huffman_t lit_decoder[15] = {0};
   uint16_t dist_lookup[HDIST_MAX] = {0};
   struct huffman_t dist_decoder[15] = {0};
   build_huffman_lookup(lit_dist_code_length, block_header->HLIT + HLIT_OFFSET, lit_lookup, lit_decoder, 16);
   build_huffman_lookup(lit_dist_code_length + block_header->HLIT + HLIT_OFFSET, block_header->HDIST + HDIST_OFFSET, dist_lookup, dist_decoder, 16);

   alphabet_t lit_huffman = {0};
   alphabet_t dist_huffman = {0};
   uint16_t dist_result;
   if(state == READ_DATA)
   {
      printf("\tReading data\n");
      while (bitstream->byte_index < bitstream->size)
      {
         lookup_result = huffman_read(bitstream, lit_decoder, lit_lookup);

         if (lookup_result == 256)
         {
            if (bitstream->byte_index < bitstream->size)
            {
               printf("\tEnd of data code read.\n");
               state = READ_CODE_LENGTHS;
               return READ_COMPLETE;
            }
            break;
         }
         else if (lookup_result < 256)
         {
            output->data[output->index] = lookup_result;
            output->index++;
         }
         else if (lookup_result < 286)
         {
            lit_huffman = length_alphabet[lookup_result - 257];
            uint16_t length = lit_huffman.value;
            if (lookup_result > 264)
            {
               input.u16[0] = (*(uint16_t*)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
               length += (input.u8[0] & (0xff >> (8 - lit_huffman.extra)));
               stream_add_bits(bitstream, lit_huffman.extra);
            }         

            dist_result = huffman_read(bitstream, dist_decoder, dist_lookup);
            dist_huffman = distance_alphabet[dist_result];
            uint16_t distance = dist_huffman.value;
            if (dist_result > 3) 
            {
               input.u32 = (*(uint32_t*)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
               distance += (input.u16[0] & (0xffff >> (16 - dist_huffman.extra)));
               stream_add_bits(bitstream, dist_huffman.extra);
            }

            if(bitstream->byte_index < bitstream->size)
            {
               for(int i=0; i<length; i++)
               {
                  output->data[output->index] = output->data[output->index - distance];
                  output->index++;
               }
            }
         }
         else
         {
            printf("Error, invalid literal/length value.\n");
            return READ_ERROR;
         }
      }

      if(bitstream->byte_index >= bitstream->size)
      {
         int reverse_lit_lookup_index = 0;
         
         while(lit_lookup[reverse_lit_lookup_index] != lookup_result)
         {
            reverse_lit_lookup_index++;
         }
         int reverse_lit_decoder_index = 0;
         while(reverse_lit_lookup_index + lit_decoder[reverse_lit_decoder_index].offset >= lit_decoder[reverse_lit_decoder_index].threshold)
         {
            reverse_lit_decoder_index++;
         }
         uint8_t bit_offset = lit_decoder[reverse_lit_decoder_index].bitlength;
         printf("Bit offset: %d -> \t", bit_offset);
         if (lookup_result == 256) {}
         else if (lookup_result < 256)
         {
            output->index--;
         }
         else
         {
            int reverse_dist_lookup_index = 0;
            while(dist_lookup[reverse_dist_lookup_index] != dist_result)
            {
               reverse_dist_lookup_index++;
            }
            int reverse_dist_decoder_index = 0;
            while(reverse_dist_lookup_index + dist_decoder[reverse_dist_decoder_index].offset >= dist_decoder[reverse_dist_decoder_index].threshold)
            {
               reverse_dist_decoder_index++;
            }
            bit_offset += lit_huffman.extra + dist_decoder[reverse_dist_decoder_index].bitlength + dist_huffman.extra;
         }
         printf("%d\n", bit_offset);
         printf("Reverting %llu:%u --> ", bitstream->byte_index, bitstream->bit_index);
         stream_remove_bits(bitstream, bit_offset);
         printf("%llu:%u\n", bitstream->byte_index, bitstream->bit_index);
         printf("\tEnd of buffer, read %llu bytes\n", output->index);
      }
   }
   return READ_INCOMPLETE;
}

enum inflate_status_t btype_error(struct stream_ptr_t *bitstream, struct block_header_t *block_header, struct data_buffer_t *output)
{
   (void)bitstream;
   (void)block_header;
   (void)output;
   printf("Invalid BTYPE flag\n");
   return READ_ERROR;
}

typedef enum inflate_status_t (*block_read_t)(struct stream_ptr_t *bitstream, struct block_header_t *block_header, struct data_buffer_t *output);

int decompress_zlib(struct stream_ptr_t *bitstream, uint8_t *output)
{
   static enum { 
      READING_ZLIB_HEADER,
      READING_INFLATE_BLOCK_HEADER,
      READING_INFLATE_BLOCK_DATA,
      READING_ADLER32_CHECKSUM 
   } state = READING_ZLIB_HEADER;

   static struct block_header_t deflate_block_header;
   static size_t output_index = 0;

   if (state == READING_ZLIB_HEADER)
   {
      struct zlib_header_t zlib_header;
      enum zlib_header_status_t zlib_header_status = zlib_header_check(bitstream, &zlib_header);
      if (zlib_header_status == ZLIB_HEADER_INCOMPLETE)
      {
         return ZLIB_INCOMPLETE;
      }
      else if (zlib_header_status != ZLIB_HEADER_NO_ERR)
      {
         printf("zlib header check failed\n");
         return ZLIB_BAD_HEADER;
      }
      printf("\tSet LZ77 buffer to %d bytes\n", 1 << (zlib_header.CINFO + 8));
      state = READING_INFLATE_BLOCK_HEADER;
   }

   enum inflate_status_t block_read_result = READ_INCOMPLETE;
   do
   {
      if (state == READING_INFLATE_BLOCK_HEADER)
      {
         if (read_block_header(bitstream, &deflate_block_header) != 0)
         {
            printf("deflate header block read failed\n");
            return ZLIB_BAD_DEFLATE_HEADER;
         }
         state = READING_INFLATE_BLOCK_DATA;
      }

      if (state == READING_INFLATE_BLOCK_DATA)
      {
         block_read_t read_block_data[4] = {inflate_uncompressed, inflate_fixed, inflate_dynamic, btype_error};
         struct data_buffer_t var = { .data=output, .index=output_index };
         block_read_result = read_block_data[deflate_block_header.BTYPE](bitstream, &deflate_block_header, &var );
         output_index = var.index;
         if(block_read_result == READ_COMPLETE)
         {
            state = deflate_block_header.BFINAL ? READING_ADLER32_CHECKSUM : READING_INFLATE_BLOCK_HEADER;
         }
      }
   } while (block_read_result == READ_COMPLETE && !deflate_block_header.BFINAL);

   if (state == READING_ADLER32_CHECKSUM)
   {
      size_t adler32_index = (bitstream->bit_index == 0) ? bitstream->byte_index : bitstream->byte_index + 1;
      if ((bitstream->size - adler32_index) < ZLIB_ADLER32_SIZE)
      {
         printf("zlib incomplete checksum\n");
         return ZLIB_ADLER32_CHECKSUM_MISSING;
      }

      uint32_t adler32_check = order_png32_t(*(uint32_t*)(bitstream->data + adler32_index));
      uint32_t adler32_result = adler32(output, output_index);
      printf("zlib complete\n");
      if(adler32_result != adler32_check)
      {
         printf("zlib adler32 checksum failed\n");
         return ZLIB_ADLER32_FAILED;
      }
      return ZLIB_COMPLETE;
   }

   return ZLIB_INCOMPLETE;
}