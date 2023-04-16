#include "decompress.h"

#include <stdio.h>
// #include "adler32.h"

#define ALPHABET_SIZE 16
#define ALPHABET_LIMIT 0x8000
#define UNUSED_CODE_LENGTH 0x80
#define HLIT_MAX 286
#define HLIT_OFFSET 257
#define HDIST_MAX 32
#define HDIST_OFFSET 1
#define HCLEN_MAX 19
#define HCLEN_OFFSET 4
#define DYNAMIC_BLOCK_HEADER_SIZE 14
#define CM_DEFLATE 8
#define CM_RESERVED 15
#define CINFO_WINDOW_MAX 7
#define DISTANCE_BIT_COUNT 5

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

static inline void increment_streampointer(struct stream_ptr_t *ptr, uint8_t bits)
{
   ptr->bit_index += bits;
   ptr->byte_index += (ptr->bit_index) >> 3;
   ptr->bit_index &= 0x07;
}

#define ZLIB_HEADER_SIZE 2
#define ZLIB_ADLER32_SIZE 4

enum zliberr_t
{
   ZLIB_NO_ERR = 0,
   ZLIB_FCHECK_FAIL,
   ZLIB_UNSUPPORTED_CM,
   ZLIB_INVALID_CM,
   ZLIB_INVALID_CINFO,
   ZLIB_UNSUPPORTED_FDICT
};

static inline enum zliberr_t zlib_header_check(const uint8_t *data)
{
   struct zlib_header_t
   {
      uint8_t CM : 4;
      uint8_t CINFO : 4;
      uint8_t FCHECK : 5;
      uint8_t FDICT : 1;
      uint8_t FLEVEL : 2;
   } zlib_header = *(struct zlib_header_t *)data;

   uint16_t FCHECK_RESULT = (((*data) << 8) | (*(data + 1))) % 31;

   printf("\tCINFO: %02X\n\tCM: %02X\n\tFLEVEL: %02X\n\tFDICT: %02X\n\tFCHECK: %02X (%d)\n", zlib_header.CINFO, zlib_header.CM, zlib_header.FLEVEL, zlib_header.FDICT, *(data + 1) & 0x1f, FCHECK_RESULT);

   if (zlib_header.CM != CM_DEFLATE)
   {
      if (zlib_header.CM == CM_RESERVED)
      {
         printf("zlib error: Unsupported compression method specified in header\n");
         return ZLIB_UNSUPPORTED_CM;
      }
      else
      {
         printf("zlib error: Invalid compression method specified in header\n");
         return ZLIB_INVALID_CM;
      }
   }
   if (zlib_header.CINFO > CINFO_WINDOW_MAX)
   {
      printf("zlib error: Invalid window size specified in header\n");
      return ZLIB_INVALID_CINFO;
   }
   if (zlib_header.FDICT)
   {
      printf("zlib error: Dictionary cannot be specified for PNG in header\n");
      return ZLIB_UNSUPPORTED_FDICT;
   }
   if (FCHECK_RESULT)
   {
      printf("zlib error: FCHECK failed\n");
      return ZLIB_FCHECK_FAIL;
   }

   return ZLIB_NO_ERR;
}

struct extra_bits
{
   uint16_t value;
   uint8_t extra;
};

typedef struct extra_bits alphabet_t;

static const alphabet_t length_alphabet[29] = {
    {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0}, {11, 1}, {13, 1}, {15, 1}, {17, 1}, {19, 2}, {23, 2}, {27, 2}, {31, 2}, {35, 3}, {43, 3}, {51, 3}, {59, 3}, {67, 4}, {83, 4}, {99, 4}, {115, 4}, {131, 5}, {163, 5}, {195, 5}, {227, 5}, {258, 0}}; // Lookup for 257-285

static const alphabet_t distance_alphabet[30] = {
    {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 1}, {7, 1}, {9, 2}, {13, 2}, {17, 3}, {25, 3}, {33, 4}, {49, 4}, {65, 5}, {97, 5}, {129, 6}, {193, 6}, {257, 7}, {385, 7}, {513, 8}, {769, 8}, {1025, 9}, {1537, 9}, {2049, 10}, {3073, 10}, {4097, 11}, {6145, 11}, {8193, 12}, {12289, 12}, {16385, 13}, {24577, 13}};

static const uint8_t reverse_nibble_lookup[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};

static inline uint8_t reverse_byte(uint8_t n)
{
   return (reverse_nibble_lookup[n & 0x0F] << 4) | reverse_nibble_lookup[n >> 4];
}

int inflate_uncompressed(struct stream_ptr_t *bitstream, uint8_t *output)
{
   (void)output;

   if (bitstream->inflate.status == STREAM_STATUS_IDLE)
   {
      uint8_t unused_bit_len = (-bitstream->bit_index) & 0x07;
      increment_streampointer(bitstream, unused_bit_len);

      uint16_t LEN, NLEN;
      if ((bitstream->size - bitstream->byte_index) < (sizeof(LEN) + sizeof(NLEN)))
      {
         printf("Incomplete block, cannot load LEN/NLEN\n");
         return 0; // Incomplete block, load next chunk to continue
      }

      LEN = *(uint16_t *)(bitstream->data + bitstream->byte_index);
      NLEN = *(uint16_t *)(bitstream->data + bitstream->byte_index + sizeof(LEN));
      printf("\t\tLEN: %04X\t(%d bytes)\n\t\tNLEN: %04X\n", LEN, LEN, NLEN);
      if ((LEN ^ NLEN) != 0xFFFF)
      {
         printf("LEN/NLEN check failed for uncompressed block\n");
         return -1;
      }
      bitstream->byte_index += sizeof(LEN) + sizeof(NLEN);
      bitstream->inflate.uncompressed.bytes_read = 0;
      bitstream->inflate.uncompressed.header.LEN = LEN;
      bitstream->inflate.status = STREAM_STATUS_BUSY;
   }

   while (bitstream->inflate.uncompressed.bytes_read < bitstream->inflate.uncompressed.header.LEN && bitstream->byte_index < bitstream->size)
   {
      printf("%02x ", *(bitstream->data + bitstream->byte_index)); // TODO: Process pixel bytes
      bitstream->inflate.uncompressed.bytes_read++;
      bitstream->byte_index++;
   }
   printf("\nRead %llu of %d bytes\n", bitstream->inflate.uncompressed.bytes_read, bitstream->inflate.uncompressed.header.LEN);
   if (bitstream->inflate.uncompressed.bytes_read >= bitstream->inflate.uncompressed.header.LEN)
   {
      bitstream->inflate.status = STREAM_STATUS_COMPLETE;
   }

   return 0;
}

int inflate_fixed(struct stream_ptr_t *bitstream, uint8_t *output)
{
   static size_t output_index = 0;
   union dbuf input;
   alphabet_t length, distance;

   bitstream->inflate.status = STREAM_STATUS_BUSY;

   while (bitstream->byte_index < bitstream->size)
   {
      input.u8[1] = reverse_byte(*(bitstream->data + bitstream->byte_index));
      input.u8[0] = reverse_byte(*(bitstream->data + bitstream->byte_index + 1));
      input.u16[0] <<= bitstream->bit_index;

      length.value = length.extra = 0;
      distance.value = distance.extra = 0;

      if (input.u8[1] < 2) // Exit code is 7 MSB bits 0, LSB 0|1
      {
         bitstream->inflate.status = STREAM_STATUS_COMPLETE;
         printf("\nEnd of data code read.\n");
         break;
      }
      else if (input.u8[1] < 48) // 7-bit codes, 1-47 maps to 257 - 279
      {
         length = length_alphabet[(input.u8[1] >> 1) - 1]; // map to 0-22 for lookup
         increment_streampointer(bitstream, 7);
      }
      else if (input.u8[1] < 192) // 8-bit literals, 48-191 maps to 0-143
      {
         output[output_index] = input.u8[1] - 48;
         printf("%02x ", output[output_index]);
         output_index++;
         increment_streampointer(bitstream, 8);
      }
      else if (input.u8[1] < 200) // 8-bit codes, 192-197 maps to 280-285 (286|287 unused)
      {
         length = length_alphabet[input.u8[1] - 169]; // map to 23-28 for lookup
         increment_streampointer(bitstream, 8);
      }
      else // 9-bit literals, 200-255, read extra bit and map to 144-255
      {
         input.u16[0] <<= 1;
         output[output_index] = input.u8[1];
         printf("%02x ", output[output_index]);
         output_index++;
         increment_streampointer(bitstream, 9);
      }

      if (length.value)
      {
         input.u16[0] = *(bitstream->data + bitstream->byte_index);
         input.u16[0] >>= bitstream->bit_index;

         length.value += input.u8[0] & (0xff >> (8 - length.extra));
         increment_streampointer(bitstream, length.extra);

         input.u8[1] = reverse_byte(*(bitstream->data + bitstream->byte_index));
         input.u8[0] = reverse_byte(*(bitstream->data + bitstream->byte_index + 1));
         input.u16[0] <<= bitstream->bit_index;
         
         distance = distance_alphabet[input.u8[1] >> 3];
         increment_streampointer(bitstream, DISTANCE_BIT_COUNT);

         input.u32 = *(uint32_t *)(bitstream->data + bitstream->byte_index);
         input.u32 >>= bitstream->bit_index;

         distance.value += input.u16[0] & (0xffff >> (16 - distance.extra));
         increment_streampointer(bitstream, distance.extra);

         if (bitstream->byte_index >= bitstream->size)
         {
            break;
         }

         for(int i=0; i<length.value; i++)
         {
            output[output_index] = output[output_index - distance.value];
            printf("%02x ", output[output_index]);
            output_index++;
         }
      }
   }

   // Wind back if read length exceeded.
   if (bitstream->byte_index >= bitstream->size)
   {
      size_t bit_offset = 0;
      if (length.value)
      {
         bit_offset = (length.value < 280) ? 7 : 8;
         bit_offset += length.extra + DISTANCE_BIT_COUNT + distance.extra;
      }
      else
      {
         bit_offset = (*output < 144) ? 8 : 9;
      }
      bit_offset -= bitstream->bit_index;
      bitstream->byte_index -= (bit_offset >> 3);
      bitstream->bit_index = (8 - (bit_offset & 0x07)) & 0x07;
      bitstream->inflate.status = STREAM_STATUS_PARTIAL;
   }

   return 0;
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

   increment_streampointer(bitstream, decoder[decoder_index].bitlength);

   return lookup[huffman_code_in - decoder[decoder_index].offset];
}

void build_huffman_lookup(const uint16_t *input, const uint16_t code_max, uint16_t *lookup, struct huffman_t *decoder, const uint8_t decoder_length)
{
   struct
   {
      uint16_t count;
      uint16_t start;
      uint16_t total;
   } temp[16] = {0};

   for (int i = 0; i < code_max; i++)
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
      for (int j = 0; j < code_max; j++)
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

int inflate_dynamic(struct stream_ptr_t *bitstream, uint8_t *output)
{
   union dbuf input;

   static uint16_t code_length_lookup[19] = {0};
   static struct huffman_t code_length_decoder[7] = {0};
   static int huffman_count;
   if (bitstream->inflate.status == STREAM_STATUS_IDLE)
   {
      if ((bitstream->size - bitstream->byte_index) < 3)
      {
         printf("Incomplete block, cannot load HLIT/HDIST/HCLEN\n");
         bitstream->inflate.status = STREAM_STATUS_PARTIAL;
         return 1; // Incomplete block, load next chunk to continue
      }
      uint32_t input = (*(uint32_t *)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
      bitstream->inflate.dynamic.header.HLIT = input & 0x1f;
      bitstream->inflate.dynamic.header.HDIST = (input >> 5) & 0x001f;
      bitstream->inflate.dynamic.header.HCLEN = (input >> 10) & 0x000f;

      if ((bitstream->size - bitstream->byte_index) < 4llu + (((bitstream->inflate.dynamic.header.HCLEN + 4llu) * 3llu) >> 3))
      {
         printf("Incomplete block, cannot load code length codes\n");
         bitstream->inflate.status = STREAM_STATUS_PARTIAL;
         return 1;
      }
      printf("\t\tHLIT: %d\n\t\tHDIST: %d\n\t\tHCLEN: %d\nDecompressing dynamic...\n", bitstream->inflate.dynamic.header.HLIT, bitstream->inflate.dynamic.header.HDIST, bitstream->inflate.dynamic.header.HCLEN);
      increment_streampointer(bitstream, 14);

      uint16_t code_length[HCLEN_MAX] = {0};
      uint8_t code_order[HCLEN_MAX] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

      for (int i = 0; i < bitstream->inflate.dynamic.header.HCLEN + 4; i++)
      {
         code_length[code_order[i]] = (*(uint16_t *)(bitstream->data + bitstream->byte_index) >> bitstream->bit_index) & 0x07;
         increment_streampointer(bitstream, 3);
      }

      build_huffman_lookup(code_length, HCLEN_MAX, code_length_lookup, code_length_decoder, 8);

      huffman_count = 0;
      bitstream->inflate.status = STREAM_STATUS_BUSY;
   }

   uint16_t lit_dist_code_length[HLIT_MAX + HDIST_MAX] = {0};
   while (huffman_count < bitstream->inflate.dynamic.header.HLIT + HLIT_OFFSET + bitstream->inflate.dynamic.header.HDIST + HDIST_OFFSET && bitstream->byte_index < bitstream->size)
   {
      uint16_t lookup_result = huffman_read(bitstream, code_length_decoder, code_length_lookup);

      if (lookup_result <= 15)
      {
         lit_dist_code_length[huffman_count] = lookup_result;
         huffman_count++;
      }
      else if (lookup_result <= 18)
      {
         input.u16[0] = (*(uint16_t *)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
         if (lookup_result == 16)
         {
            for (int i = 0; i < (input.u8[0] & 0x03) + 3; i++)
            {
               lit_dist_code_length[huffman_count] = lit_dist_code_length[huffman_count - 1];
               huffman_count++;
            }
            increment_streampointer(bitstream, 2);
         }
         else if (lookup_result == 17)
         {
            for (int i = 0; i < (input.u8[0] & 0x07) + 3; i++)
            {
               lit_dist_code_length[huffman_count] = 0;
               huffman_count++;
            }
            increment_streampointer(bitstream, 3);
         }
         else
         {
            for (int i = 0; i < (input.u8[0] & 0x7f) + 11; i++)
            {
               lit_dist_code_length[huffman_count] = 0;
               huffman_count++;
            }
            increment_streampointer(bitstream, 7);
         }
      }
      else
      {
         printf("Error, unkown Huffman code entry.\n");
         return -1;
      }
   }

   uint16_t lit_lookup[HLIT_MAX] = {0};
   struct huffman_t lit_decoder[15] = {0};
   uint16_t dist_lookup[HDIST_MAX] = {0};
   struct huffman_t dist_decoder[15] = {0};
   build_huffman_lookup(lit_dist_code_length, bitstream->inflate.dynamic.header.HLIT + HLIT_OFFSET, lit_lookup, lit_decoder, 16);
   build_huffman_lookup(lit_dist_code_length + bitstream->inflate.dynamic.header.HLIT + HLIT_OFFSET, bitstream->inflate.dynamic.header.HDIST + HDIST_OFFSET, dist_lookup, dist_decoder, 16);

   static size_t output_count = 0;
   while (bitstream->byte_index < bitstream->size)
   {
      uint16_t lookup_result = huffman_read(bitstream, lit_decoder, lit_lookup);

      if (lookup_result == 256)
      {
         printf("\nEnd of data reached.\n");
         bitstream->inflate.status = STREAM_STATUS_COMPLETE;
         return 0;
      }
      else if (lookup_result < 256)
      {
         output[output_count] = lookup_result;
         output_count++;
         printf("%02x ", lookup_result);
      }
      else if (lookup_result < 286)
      {
         alphabet_t lit_huffman = length_alphabet[lookup_result - 257];
         uint16_t length = lit_huffman.value;
         if (lookup_result > 264)
         {
            input.u16[0] = (*(uint16_t*)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
            length += (input.u8[0] & (0xff >> (8 - lit_huffman.extra)));
            increment_streampointer(bitstream, lit_huffman.extra);
         }         

         uint16_t dist_result = huffman_read(bitstream, dist_decoder, dist_lookup);
         alphabet_t dist_huffman = distance_alphabet[dist_result];
         uint16_t distance = dist_huffman.value;
         if (dist_result > 3) 
         {
            input.u32 = (*(uint32_t*)(bitstream->data + bitstream->byte_index)) >> bitstream->bit_index;
            distance += (input.u16[0] & (0xffff >> (16 - dist_huffman.extra)));
            increment_streampointer(bitstream, dist_huffman.extra);
         }

         for(int i=0; i<length; i++)
         {
            output[output_count] = output[output_count - distance];
            printf("%02x ", output[output_count]);
            output_count++;
         }
      }
      else
      {
         printf("Error, invalid literal/length value.\n");
         return 1;
      }
   }

   // if(bitstream->byte_index >= bitstream->size)
   // {
   //    size_t bit_offset = code_length_decoder[decoder_index].bitlength;
   //    if (code_length_lookup[huffman_code_in - code_length_decoder[j].offset] == 16)
   //    {
   //       /* code */
   //    }
   //    else if (code_length_lookup[test - code_length_decoder[j].offset] == 17)
   //    {
   //    }
   //    else
   //    {
   //    }
   //    break;
   // }

   return 0;
}

int btype_error(struct stream_ptr_t *bitstream, uint8_t *output)
{
   (void)output;
   (void)bitstream;
   printf("Invalid BTYPE flag\n");
   return 0;
}

typedef int (*block_read_t)(struct stream_ptr_t *bitstream, uint8_t *output);

int decompress(uint8_t *data, size_t size, uint8_t *output)
{
   (void)output;
   static struct stream_ptr_t bitstream = {
       .zlib.status = STREAM_STATUS_IDLE,
       .inflate.status = STREAM_STATUS_IDLE};
   bitstream.data = data;
   bitstream.size = size;
   bitstream.bit_index = 0;
   bitstream.byte_index = 0; // TODO: Correct for leftover stream data

   if (bitstream.zlib.status != STREAM_STATUS_BUSY)
   {
      if (zlib_header_check(bitstream.data) != ZLIB_NO_ERR)
      {
         printf("zlib header check failed\n");
         return -1;
      }
      bitstream.byte_index += ZLIB_HEADER_SIZE;
      bitstream.zlib.status = STREAM_STATUS_BUSY;
   }

   if (bitstream.inflate.status != STREAM_STATUS_BUSY)
   {
      uint16_t block_header = *(uint16_t *)(bitstream.data + bitstream.byte_index);
      block_header = block_header >> bitstream.bit_index;

      bitstream.inflate.BFINAL = block_header & 0x01;
      bitstream.inflate.BTYPE = (block_header >> 1) & 0x03;
      increment_streampointer(&bitstream, 3);

      printf("\tINFLATE:\n\t\tBFINAL: %01x\n\t\tBTYPE: %02x\n", bitstream.inflate.BFINAL, bitstream.inflate.BTYPE);
   }

   block_read_t reader_functions[4] = {&inflate_uncompressed, &inflate_fixed, &inflate_dynamic, &btype_error};
   reader_functions[bitstream.inflate.BTYPE](&bitstream, output);
   printf("Stream pointer: %llu bytes %d bits\n\tzlib    status: %02x\n\tinflate status: %02x\n", bitstream.byte_index, bitstream.bit_index, bitstream.zlib.status, bitstream.inflate.status);

   if (bitstream.inflate.status == STREAM_STATUS_COMPLETE)
   {
      bitstream.zlib.status = STREAM_STATUS_COMPLETE;
      // TODO: Check inflate BFINAL for further processing
      //       Adler32 checksum on uncompressed data
   }

   return 0;
}