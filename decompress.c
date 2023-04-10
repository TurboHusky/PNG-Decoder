#include "decompress.h"

#include <stdio.h>
#include "adler32.h"

#define ALPHABET_SIZE 16
#define ALPHABET_LIMIT 0x8000
#define UNUSED_CODE_LENGTH 0x80
#define CODE_LENGTH_ALPHABET_SIZE 19
#define DYNAMIC_BLOCK_HEADER_SIZE 14
#define CM_DEFLATE 8
#define CM_RESERVED 15
#define CINFO_WINDOW_MAX 7

enum decompression_status_t { STREAM_STATUS_IDLE, STREAM_STATUS_BUSY, STREAM_STATUS_COMPLETE, DC_FAILED };

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
      struct {
         struct {
            uint16_t LEN;
         } header;
         size_t bytes_read;
      } uncompressed;
      struct {
         struct { 
            uint8_t HCLEN;
            uint8_t HLIT;
            uint8_t HDIST;
         } header;
      } dynamic;
   } inflate;
};

static inline void increment_streampointer(struct stream_ptr_t* ptr, uint8_t bits)
{
   ptr->bit_index += bits;
   ptr->byte_index += (ptr->bit_index) >> 3;
   ptr->bit_index &= 0x07;
}

#define ZLIB_HEADER_SIZE 2
#define ZLIB_ADLER32_SIZE 4

enum zliberr_t { ZLIB_NO_ERR=0, ZLIB_FCHECK_FAIL, ZLIB_UNSUPPORTED_CM, ZLIB_INVALID_CM, ZLIB_INVALID_CINFO, ZLIB_UNSUPPORTED_FDICT };

enum zliberr_t zlib_header_check(const uint8_t* data)
 {
   struct zlib_header_t
   {
      uint8_t CM : 4;
      uint8_t CINFO : 4;
      uint8_t FCHECK : 5;
      uint8_t FDICT : 1;
      uint8_t FLEVEL : 2;
   } zlib_header = *(struct zlib_header_t *)data;

    uint16_t FCHECK_RESULT = (((*data) << 8) | (*(data+1))) % 31;

    printf("\tCINFO: %02X\n\tCM: %02X\n\tFLEVEL: %02X\n\tFDICT: %02X\n\tFCHECK: %02X (%d)\n", zlib_header.CINFO, zlib_header.CM, zlib_header.FLEVEL, zlib_header.FDICT, *(data+1) & 0x1f, FCHECK_RESULT);

    if(zlib_header.CM != CM_DEFLATE)
    {
        if(zlib_header.CM == CM_RESERVED)
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
    if(zlib_header.CINFO > CINFO_WINDOW_MAX)
    {
        printf("zlib error: Invalid window size specified in header\n");
        return ZLIB_INVALID_CINFO;
    }
    if(zlib_header.FDICT)
    {
        printf("zlib error: Dictionary cannot be specified for PNG in header\n");
        return ZLIB_UNSUPPORTED_FDICT;
    }
    if(FCHECK_RESULT)
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
   {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}, {8, 0}, {9, 0}, {10, 0},
   {11, 1}, {13, 1}, {15, 1}, {17, 1}, {19, 2}, {23, 2}, {27, 2}, {31, 2},
   {35, 3}, {43, 3}, {51, 3}, {59, 3}, {67, 4}, {83, 4}, {99, 4}, {115, 4},
   {131, 5}, {163, 5}, {195, 5}, {227, 5}, {258, 0}
}; // Lookup for 257-285

static const alphabet_t distance_alphabet[30] = {
   {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 1}, {7, 1}, {9, 2}, {13, 2},
   {17, 3}, {25, 3}, {33, 4}, {49, 4}, {65, 5}, {97, 5}, {129, 6}, {193, 6},
   {257, 7}, {385, 7}, {513, 8}, {769, 8}, {1025, 9}, {1537, 9}, {2049, 10}, {3073, 10},
   {4097, 11}, {6145, 11}, {8193, 12}, {12289, 12}, {16385, 13}, {24577, 13}
};

static const uint8_t reverse_nibble_lookup[16] = { 0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF };

static inline uint8_t reverse_byte(uint8_t n)
{
   return (reverse_nibble_lookup[n & 0x0F] << 4) | reverse_nibble_lookup[n >> 4];
}

void inflate_fixed(struct stream_ptr_t *bitstream, uint8_t *out)
{ 
   printf("Decompressing fixed...\n");
   uint8_t *buf = bitstream->data;
   union dbuf input;

   input.buffer[3] = reverse_byte(*buf);
   input.buffer[2] = reverse_byte(*(buf+1));
   input.buffer[1] = reverse_byte(*(buf+2));
   input.buffer[0] = reverse_byte(*(buf+3));

   uint32_t shift = bitstream->bit_index;
   input.stream <<= shift;

   size_t index = bitstream->byte_index;
   int out_index = 0;
   while(input.buffer[3]>2 && index<bitstream->size) // 7-bit 0 maps to end of block (code 256)
   {
      alphabet_t test_alphabet = {0,0};
      if(input.buffer[3] < 48) // 7-bit codes, 1-47 maps to 257 - 279
      {
         test_alphabet = length_alphabet[(input.buffer[3] >> 1) - 1]; // map to 0-22 for lookup
         input.stream <<= 7;
         shift += 7;
      }
      else if(input.buffer[3] < 192) // 8-bit literals, 48-191 maps to 0-143
      {
         *(out+out_index) = input.buffer[3]-48;
         out_index++;
         // printf("%d ", input.buffer[3]-48);
         input.stream <<= 8;
         shift += 8;
      }
      else if(input.buffer[3] < 200) // 8-bit codes, 192-199 maps to 280-287
      {
         test_alphabet = length_alphabet[input.buffer[3]-169]; // map to 23-28 for lookup
         input.stream <<= 8;
         shift += 8;
      }
      else // 9-bit literals, 200-255, read extra bit and map to 144-255
      {
         input.stream <<= 1;
         *(out+out_index) = input.buffer[3];
         out_index++;
         // printf("%d ", input.buffer[3]);
         input.stream <<= 8;
         shift += 9;
      }

      if(test_alphabet.value)
      {
         uint16_t extra = 0;
         for (int i = 0; i < test_alphabet.extra; i++)
         {
            extra |= (input.buffer[3] & 0x80) >> (7 - i); // Read extra bits as machine integer (MSB first).
            input.stream <<= 1;
            shift++;
         }
         int copy_size = test_alphabet.value + extra;
         // printf("%d:", test_alphabet.value + extra);
         test_alphabet = distance_alphabet[input.buffer[3] >> 3];
         input.stream <<= 5;
         shift += 5;

         extra = 0;
         for (int i = 0; i < test_alphabet.extra; i++)
         {
            extra |= (input.split[1] & 0x8000) >> (15 - i); // Read extra bits as machine integer (MSB first).
            input.stream <<= 1;
            shift++;
         }
         int offset = test_alphabet.value + extra;
         // printf("%d ", test_alphabet.value + extra);
         while (copy_size > 0)
         {
            *(out+out_index) = *(out+out_index-offset);
            out_index++;
            copy_size--;
         }
         
      }
      uint32_t loop = shift >> 3;
      if(loop && index<bitstream->size-4)
      {
         shift = shift % 8;
         input.stream >>= shift;
         for(int i=(int)loop-1; i>=0; i--)
         {
            input.buffer[i] = reverse_byte(*(buf+index+4));
            index++;
         }
         input.stream <<= shift;
      }
   }
}

void build_huffman(uint16_t *codes, int len, uint16_t *output, uint16_t *limits)
{
   struct
   {
      uint16_t count;
      uint16_t min;
   } code_builder[ALPHABET_SIZE] = { 0 };

   for(int i=0; i<len; i++)
   {
      code_builder[*(codes+i)].count++;
   }
   for(int i=2; i<ALPHABET_SIZE; i++) 
   { 
      code_builder[i].min = (code_builder[i-1].count + code_builder[i-1].min) << 1; // Minimum starting values for each code length code
      *(limits+i) = code_builder[i].min;
   }
   code_builder[0].count = 0;
   for(int i=0; i<len; i++)
   {
      *(output+i) = code_builder[*(codes+i)].count > 0 ? (*(limits+(*(codes+i))))++ : ALPHABET_LIMIT;
   }
   // printf("\nHuffman builder:\nBits:\tCount:\tMin:\n");
   // for(int i=0; i<ALPHABET_SIZE; i++){ printf("%d\t%d\t%d\n", i, code_builder[i].count, code_builder[i].min); }
}

void inflate_dynamic(struct stream_ptr_t *bitstream, uint8_t *output, int *output_index)
{
   struct huf_build
   {
      uint32_t count;
      uint32_t min;
      uint32_t max;
   };
 
   uint8_t *buf = bitstream->data;
   uint8_t code_length[CODE_LENGTH_ALPHABET_SIZE] = { 0 };
   union dbuf input;

   input.stream = *(uint32_t *)(bitstream->data + bitstream->byte_index);
   input.stream >>= bitstream->bit_index;
   
   uint16_t HLIT = (input.buffer[0] & 0x1f) + 257;
   input.stream >>= 5;
   uint8_t HDIST = (input.buffer[0] & 0x1f) + 1;
   input.stream >>= 5;
   uint8_t HCLEN =  (input.buffer[0] & 0x0f) + 4; 
   printf("\t\tHLIT: %d\n\t\tHDIST: %d\n\t\tHCLEN: %d\nDecompressing dynamic...\n", HLIT, HDIST, HCLEN);

   bitstream->bit_index += DYNAMIC_BLOCK_HEADER_SIZE;
   size_t index = bitstream->byte_index + (bitstream->bit_index >> 3); // Skip header bytes
   input.stream = *(uint32_t *)(buf + index);

   uint32_t shift = bitstream->bit_index % 8;
   input.stream >>= shift; // Apply offset from bit consumed by header

   // Read 4 - 19 code lengths
   uint8_t code_order[CODE_LENGTH_ALPHABET_SIZE] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
   struct huf_build code_length_builder[8] = { 0 };
   for (int i=0; i<HCLEN; i++)
   {
      code_length[code_order[i]] = input.buffer[0] & 0x07;
      code_length_builder[code_length[code_order[i]]].count++;
      input.stream >>= 3;
      shift += 3;

      // Load new byte into buffer
      if(shift>7 && index<bitstream->size)
      {
         shift-=8;
         input.stream <<= shift;
         input.buffer[3] = *(buf+index+4);
         input.stream >>= shift;
         index++;
      }
   }
   // Calculate starting values for Huffman codes
   for(int i=2; i<8; i++) 
   { 
      code_length_builder[i].min = (code_length_builder[i-1].count + code_length_builder[i-1].min) << 1; // Minimum starting values for each code length code
      code_length_builder[i].max = code_length_builder[i].min;
   }
   // Build Huffman tree
   code_length_builder[0].count = 0;
   for(int i=0; i<CODE_LENGTH_ALPHABET_SIZE; i++)
   {
      code_length[i] = code_length_builder[code_length[i]].count > 0 ? code_length_builder[code_length[i]].max++ : UNUSED_CODE_LENGTH;
   }

   // printf("\tCode length Huffman codes : ");
   // for(int i=0; i<CODE_LENGTH_ALPHABET_SIZE; i++) { printf("%d ", code_length[i]); }
   // printf("\n");

   // Read Literal/Length length codes
   int alphabet_code_count = 0;
   uint16_t alphabet_codes[HLIT+HDIST];
   while(alphabet_code_count<HLIT+HDIST)
   {
      uint8_t test = input.buffer[0]&0x01;
      input.stream >>= 1;
      shift++;
      for(int code_bit_count=1; code_bit_count<=7; code_bit_count++)
      {
         if(test<code_length_builder[code_bit_count].max)
         {
            int index = 0;
            while(code_length[index] != test && index < CODE_LENGTH_ALPHABET_SIZE)
            {
               index++;
            }
            uint8_t extra_bits = 0;
            switch(index)
            {
               case 16:
                  extra_bits = input.buffer[0] & 0x03;
                  input.stream >>= 2;
                  shift+=2;
                  for(int k=0; k<3+extra_bits; k++)
                  {
                     alphabet_codes[alphabet_code_count] = alphabet_codes[alphabet_code_count-1];
                     alphabet_code_count++;
                  }
                  break;
               case 17:
                  extra_bits = input.buffer[0] & 0x07;
                  input.stream >>= 3;
                  shift+=3;
                  for(int k=0; k<3+extra_bits; k++)
                  {
                     alphabet_codes[alphabet_code_count] = 0;
                     alphabet_code_count++;
                  }
                  break;
               case 18:
                  extra_bits = input.buffer[0] & 0x7f;
                  input.stream >>= 7;
                  shift+=7;
                  for(int k=0; k<11+extra_bits; k++)
                  {
                     alphabet_codes[alphabet_code_count] = 0;
                     alphabet_code_count++;
                  }
                  break;
               case 19:
                  printf("ERROR: Unrecognised code.\n");
                  break;
               default:
                  alphabet_codes[alphabet_code_count] = index;
                  alphabet_code_count++;
                  break;
            }
            break; // Found Huffman code, leave loop
         }
         test = (test << 1) | (input.buffer[0]&0x01);
         input.stream >>= 1;
         shift++;
      }
      // Load new byte(s) into buffer
      index += shift >> 3;
      shift = shift % 8;
      input.stream = *((uint32_t*)(buf+index));
      input.stream >>= shift;
   }

   uint16_t literal_length_limits[ALPHABET_SIZE] = {0};
   build_huffman(alphabet_codes, HLIT, alphabet_codes, literal_length_limits);
   // printf("\n\tLiteral/Length alphabet codes: ");
   // for(int i=0; i<HLIT; i++) {printf("%d ", alphabet_codes[i]);}

   uint16_t distance_limits[ALPHABET_SIZE] = {0};
   build_huffman(&alphabet_codes[HLIT], HDIST, &alphabet_codes[HLIT], distance_limits);
   // printf("\n\n\tDistance alphabet codes: ");
   // for(int i=0; i<HDIST; i++) {printf("%d ", alphabet_codes[HLIT+i]);}
   // printf("\n\nData: ");

   int out_index = *output_index;
   int literal_length_index = 0;
   while(literal_length_index != 256 && index<(bitstream->size))
   {
      index += shift >> 3;
      shift = shift % 8;
      input.stream = *((uint32_t*)(buf+index));
      input.stream >>= shift;

      uint16_t literal_length_code = 0;
      for(int bits=1; bits<=ALPHABET_SIZE; bits++)
      {
         // Read bit from stream
         literal_length_code = (literal_length_code << 1) | (input.buffer[0]&0x01);

         input.stream >>= 1;
         shift++;
         if(literal_length_code<literal_length_limits[bits])
         {
            // Literal alphabet lookup
            literal_length_index = 0;
            while((alphabet_codes[literal_length_index] != literal_length_code) && (literal_length_index < HLIT))
            {
               literal_length_index++;
            }
            if(literal_length_index >= HLIT)
            {
               printf("Error reading compressed data, bad literal/length code\n");
            }

            if(literal_length_index<256)
            {
               // Literal
               *(output + out_index) = literal_length_index;
               out_index++;
            }
            else if(literal_length_index == 256)
            {
               // End code
               printf("End of block\n");
               // printf("Bits read: %d, limit: %d\n", bits, literal_length_limits[bits]);
               // printf("Code: %d, %d Index: %d, Length: %d, Output bytes: %d\n", literal_length_code, literal_length_index, index, len, out_index);
            }
            else if(literal_length_index <286)
            {
               // Length code
               alphabet_t tmp = length_alphabet[literal_length_index-257];
               uint16_t length = tmp.value + (input.buffer[0] & (0xff >> (8-tmp.extra)));
               input.stream >>= tmp.extra;
               shift += tmp.extra;

               index += shift >> 3;
               shift = shift % 8;
               input.stream = *((uint32_t*)(buf+index));
               input.stream >>= shift;

               // Read Distance code
               uint16_t distance_code = 0;
               for(int distance_bit_count=1; distance_bit_count<=ALPHABET_SIZE; distance_bit_count++)
               {
                  distance_code = (distance_code << 1) | (input.buffer[0]&0x01);
                  input.stream >>= 1;
                  shift++;
                  if(distance_code<distance_limits[distance_bit_count])
                  {
                     int dist_index = HLIT;
                     while(alphabet_codes[dist_index] != distance_code && dist_index < HLIT+HDIST)
                     {
                        dist_index++;
                     }
                     alphabet_t dist_tmp = distance_alphabet[dist_index - HLIT];
                     uint32_t distance = dist_tmp.value + (input.stream & (0x0000ffff >> (16-dist_tmp.extra)));
                     if(dist_index >= HLIT+HDIST)
                     {
                        printf("Error reading compressed data, bad distance code\n");
                     }

                     if((int)distance > out_index)
                     {
                        printf("Error, distance code points outside buffer\n");
                     }
                     for(int i=0; i<length; i++)
                     {
                        *(output + out_index) = *(output + out_index - distance);
                        out_index++;
                     }
                     input.stream >>= dist_tmp.extra;
                     shift += dist_tmp.extra;
                     break;
                  }
               }
            }
            else 
            {
               // Error
               printf(" ERROR::Unrecognised alphabet code\n");
            }
            break;
         }
      }
   }
   printf("Last Byte read: %02X Buffer: %08X Shift: %d, %08X\n", *(buf+index), *((uint32_t*)(buf+index)), shift, input.stream);
   bitstream->byte_index = index + (shift >> 3);
   bitstream->bit_index = shift % 8;
   *output_index = out_index;

   uint32_t zlib_checksum = order_png32_t(*(uint32_t*)(buf + bitstream->size));
   uint32_t A32 = adler32(output, out_index); // 192 Adler based on Deflate data (byte after FLG byte to byte before ADLER32 byte)
   // printf("%08X - %08X final count: %d\n", zlib_checksum, A32, out_index);
   if(A32 != zlib_checksum)
   {
      printf("ERROR::Invalid zlib checksum\n");
   }

   // printf("Decompressed data:\n");
   // for(int i=0; i<out_index; i++) { printf("%02X ", *(output + i)); }
   // printf("\nAdler32:\tComputed: %08X\n\t\tFile:     %08X\n", A32, zlib_checksum);
   FILE* out;
   out = fopen("test.rgb", "wb");
   fwrite(output, 1, out_index, out);
   fclose(out);

   // printf("HLIT+HDIST: %d, Codes Read: %d, Bytes Read: %d, Shift: %d\n", HLIT+HDIST, alphabet_code_count, index, shift);
}

int read_uncompressed(struct stream_ptr_t *bitstream, uint8_t *output) {
   (void) output;
   
   if (bitstream->inflate.status != STREAM_STATUS_BUSY)
   {
      uint16_t block_header = *(uint16_t *) (bitstream->data + bitstream->byte_index);
      block_header = block_header >> bitstream->bit_index;
            
      bitstream->inflate.BFINAL = block_header & 0x01;
      bitstream->inflate.BTYPE = (block_header >> 1) & 0x03;
      increment_streampointer(bitstream, 3);

      printf("\tINFLATE:\n\t\tBFINAL: %01x\n\t\tBTYPE: %01x\n", bitstream->inflate.BFINAL, bitstream->inflate.BTYPE);
      uint8_t unused_bit_len = (-bitstream->bit_index) & 0x07;
      increment_streampointer(bitstream, unused_bit_len);

      uint16_t LEN, NLEN;
      if ((bitstream->size - bitstream->byte_index) < (sizeof(LEN) + sizeof(NLEN)))
      {
         printf("Incomplete block, cannot load LEN/NLEN\n");
         return 0; // Incomplete block, load next chunk to continue
      }

      LEN = *(uint16_t*)(bitstream->data + bitstream->byte_index);
      NLEN = *(uint16_t*)(bitstream->data + bitstream->byte_index + sizeof(LEN));
      printf("\t\tLEN: %04X\t(%d bytes)\n\t\tNLEN: %04X\nUncompressed...\n", LEN, LEN, NLEN);
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
   
   while(bitstream->inflate.uncompressed.bytes_read < bitstream->inflate.uncompressed.header.LEN && bitstream->byte_index < bitstream->size)
   {
      // printf("%02x ", *(bitstream->data + bitstream->byte_index));
      bitstream->inflate.uncompressed.bytes_read++;
      bitstream->byte_index++;
   }
   printf("\tRead %llu of %d\n", bitstream->inflate.uncompressed.bytes_read, bitstream->inflate.uncompressed.header.LEN);
   if (bitstream->inflate.uncompressed.bytes_read >= bitstream->inflate.uncompressed.header.LEN)
   {
      bitstream->inflate.status = STREAM_STATUS_COMPLETE;
   }

   return 0; 
}

int read_fixed(struct stream_ptr_t *bitstream, uint8_t *output) { (void) output; printf("%llu\n", bitstream->size); return 0; }
int read_dynamic(struct stream_ptr_t *bitstream, uint8_t *output) { (void) output; printf("%llu\n", bitstream->size); return 0; }
int btype_error(struct stream_ptr_t *bitstream, uint8_t *output) { (void) output; printf("%llu\n", bitstream->size); return 0; }
typedef int(*block_read_t)(struct stream_ptr_t* bitstream, uint8_t *output);

int decompress(uint8_t *data, size_t size, uint8_t *output)
{
   (void) output;
   static struct stream_ptr_t bitstream = {
      .zlib.status = STREAM_STATUS_IDLE,
      .inflate.status = STREAM_STATUS_IDLE
   };
   bitstream.data = data;
   bitstream.size = size;
   bitstream.bit_index = 0;
   bitstream.byte_index = 0; // TODO: Correct for leftover stream data

   if (bitstream.zlib.status != STREAM_STATUS_BUSY)
   {
      if (zlib_header_check(data) != ZLIB_NO_ERR)
      {
         printf("zlib header check failed\n");
         return -1; 
      }
      bitstream.byte_index += ZLIB_HEADER_SIZE;
      bitstream.zlib.status = STREAM_STATUS_BUSY;
   }

   block_read_t reader_functions[4] = { &read_uncompressed, &read_fixed, &read_dynamic, &btype_error };
   reader_functions[bitstream.inflate.BTYPE](&bitstream, NULL);
   printf("Stream pointer: %llu bytes %d bits\n\tzlib    status: %02x\n\tinflate status: %02x\n", bitstream.byte_index, bitstream.bit_index, bitstream.zlib.status, bitstream.inflate.status);

   if(bitstream.inflate.status == STREAM_STATUS_COMPLETE)
   {
      printf("Adler32: %08x\n", *(uint32_t*)(bitstream.data + bitstream.byte_index));
      bitstream.zlib.status = STREAM_STATUS_COMPLETE;
   }

   return 0;
}