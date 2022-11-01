#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "crc.h"
#include "adler32.h"

#ifdef __MINGW32__
   #define OS_TARGET "windows"
#endif

#ifdef __linux__
   #define OS_TARGET "linux"
#endif

#define UNUSED_CODE_LENGTH 0x80
#define CODE_LENGTH_ALPHABET_SIZE 19
#define ALPHABET_SIZE 16
#define ALPHABET_LIMIT 0x8000
#define ADLER32_SIZE 4

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
   #define PNG_HEADER 0x0A1A0A0D474E5089
   #define PNG_IHDR 0x52444849
   #define PNG_PLTE 0x45544C50
   #define PNG_IDAT 0x54414449
   #define PNG_IEND 0x444e4549
   #define PNG_cHRM 0x4D524863
   #define PNG_gAMA 0x414D4167
   #define PNG_iCCP 0x50434369
   #define PNG_sBIT 0x54494273
   #define PNG_sRGB 0x42475273
   #define PNG_bKGD 0x44474B62
   #define PNG_hIST 0x54534968
   #define PNG_tRNS 0x534E5274
   #define PNG_pHYs 0x73594870
   #define PNG_sPLT 0x544C5073
   #define PNG_tIME 0x454D4974
   #define PNG_iTXt 0x74585469
   #define PNG_tEXt 0x74584574
   #define PNG_zTXt 0x7458547A
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
   #error Endianess not supported
   // #define PNG_HEADER 0x89504E470D0A1A0A                      
#else
   # error Endianess not defined
#endif

static inline uint32_t order_png32_t(uint32_t value)
{
   #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      return (value & 0x000000FF) << 24 | (value & 0x0000FF00) << 8 | (value & 0x00FF0000) >> 8 | (value & 0xFF000000) >> 24;
   #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      #error Endianess not supported
   #else
      # error, endianess not defined
   #endif
}

struct stream_ptr_t
{
   uint8_t *data;
   size_t len;
   size_t byte_index;
   uint8_t bit_index;
};

// Note name and crc do not contribute to chunk length
struct png_header_t
{
   uint32_t name;
   uint32_t width;
   uint32_t height;
   uint8_t bit_depth;
   uint8_t colour_type;
   uint8_t compression_method;
   uint8_t filter_method;
   uint8_t interlace_method;
   uint32_t crc;
} __attribute__((packed));

union dbuf 
{
   uint8_t byte;
   uint8_t buffer[4];
   uint16_t split[2];
   uint32_t stream;
};

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

struct lz_t
{
   uint32_t length;
   uint32_t distance;
};

struct filter_samples_t 
{
   uint8_t x;     // c b
   uint8_t a;     // a x
   uint8_t b;
   uint8_t c;
};

uint8_t reverse_byte(uint8_t n)
{
   return (reverse_nibble_lookup[n & 0x0F] << 4) | reverse_nibble_lookup[n >> 4];
}

void printbin(uint32_t n)
{
   uint32_t mask = 0x80000000;
   for(int i=31; i>=0; i--)
   {
      if(i%8 == 7)
      {
         printf(" ");
      }
      printf("%1d", (n&mask)>>i);
      mask>>=1;
   }
   printf("\n");
}

void decompress(struct stream_ptr_t sp, int len, uint8_t *out)
{ 
   printf("Decompressing fixed...\n");
   uint8_t *buf = sp.data;
   union dbuf input;

   input.buffer[3] = reverse_byte(*buf);
   input.buffer[2] = reverse_byte(*(buf+1));
   input.buffer[1] = reverse_byte(*(buf+2));
   input.buffer[0] = reverse_byte(*(buf+3));

   uint32_t shift = sp.bit_index;
   input.stream <<= shift;

   int index = sp.byte_index;
   int out_index = 0;
   while(input.buffer[3]>2 && index<len) // 7-bit 0 maps to end of block (code 256)
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
      if(loop && index<len-4)
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

uint8_t filter_type_0(uint8_t f, uint8_t a, uint8_t b, uint8_t c) {
   (void)a;
   (void)b;
   (void)c;
   return f;
}

uint8_t filter_type_1(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   (void)b;
   (void)c;
   return f+a;
}

uint8_t filter_type_2(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   (void)a;
   (void)c;
   return f+b;
}

uint8_t filter_type_3(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   (void)c;
   return f+((a+b)>>1);
}

uint8_t filter_type_4(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   int16_t p = a+b-c;
   int16_t pa = abs(p-a);
   int16_t pb = abs(p-b);
   int16_t pc = abs(p-c);
   return (pa<=pb) && (pa<=pc) ? f+a : (pb<=pc) ? f+b : f+c;
}

typedef uint8_t (*filter_t) (uint8_t, uint8_t, uint8_t, uint8_t);

int png_filter(uint8_t *buf, uint32_t scanline_width, uint32_t scanline_count, uint8_t stride) // scanline width includes filter byte
{
   filter_t filters[5];
   filters[0] = filter_type_0;
   filters[1] = filter_type_1;
   filters[2] = filter_type_2;
   filters[3] = filter_type_3;
   filters[4] = filter_type_4;

   uint8_t a = 0;    // c b
   uint8_t b = 0;    // a x
   uint8_t c = 0;
   uint8_t filter_type = *buf;
   uint32_t scanline_index = 1;

   if(filter_type > 4)
   {
      printf("Invalid filter type\n");
      scanline_index += scanline_width;
   }
   while(scanline_index <= stride)
   {
      *(buf + scanline_index) = filters[filter_type](*(buf + scanline_index), a, b, c);
      scanline_index++;
   }
   while(scanline_index < scanline_width)
   {
      a = *(buf + scanline_index - stride);
      *(buf + scanline_index) = filters[filter_type](*(buf + scanline_index), a, b, c);
      scanline_index++;
   }
   for(uint32_t j = 1; j < scanline_count; j++)
   {
      a = 0;
      c = 0;
      scanline_index = j * scanline_width;
      filter_type = *(buf + scanline_index);
      scanline_index++;
      if(filter_type > 4)
      {
         printf("Invalid filter type\n");
         scanline_index += scanline_width;
      }
      while (scanline_index <= j * scanline_width + stride)
      {
         b = *(buf + scanline_index - scanline_width);
         *(buf + scanline_index) = filters[filter_type](*(buf + scanline_index), a, b, c);
         scanline_index++;
      }
      while (scanline_index < (j + 1) * scanline_width)
      {
         a = *(buf + scanline_index - stride);
         b = *(buf + scanline_index - scanline_width);
         c = *(buf + scanline_index - scanline_width - stride);
         *(buf + scanline_index) = filters[filter_type](*(buf + scanline_index), a, b, c);
         scanline_index++;
      }
   }
   return 0;
}

void print_img_bytes(uint8_t *buf, uint32_t width, uint32_t height)
{
   for(uint32_t j=0; j<height; j++)
   {
      for(uint32_t i=0; i<width; i++)
      {
         printf("%02X ", *(buf + j*width + i));
      }
      printf("\n");
   }
}

enum colour_type_t { Greyscale=0, Truecolour=2, Indexed_colour=3, GreyscaleAlpha=4, TruecolourAlpha=6 };

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

void decompress_dynamic(struct stream_ptr_t *sp, int len, uint8_t *output, int *output_index)
{
   struct huf_build
   {
      uint32_t count;
      uint32_t min;
      uint32_t max;
   };
 
   uint8_t *buf = sp->data;
   uint8_t code_length[CODE_LENGTH_ALPHABET_SIZE] = { 0 };
   union dbuf input;

   input.stream = *(uint32_t *)(sp->data + sp->byte_index);
   input.stream >>= sp->bit_index;
   
   uint16_t HLIT = (input.buffer[0] & 0x1f) + 257;
   input.stream >>= 5;
   uint8_t HDIST = (input.buffer[0] & 0x1f) + 1;
   input.stream >>= 5;
   uint8_t HCLEN =  (input.buffer[0] & 0x0f) + 4; 
   printf("\t\tHLIT: %d\n\t\tHDIST: %d\n\t\tHCLEN: %d\nDecompressing dynamic...\n", HLIT, HDIST, HCLEN);

   sp->bit_index += 14;
   int index = sp->byte_index + (sp->bit_index >> 3); // Skip header bytes
   input.stream = *(uint32_t *)(buf + index);

   uint32_t shift = sp->bit_index % 8;
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
      if(shift>7 && index<len)
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
   while(literal_length_index != 256 && index<(len))
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
   sp->byte_index = index + (shift >> 3);
   sp->bit_index = shift % 8;
   *output_index = out_index;

   uint32_t zlib_checksum = order_png32_t(*(uint32_t*)(buf + len));
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

int decompress_zlib(struct stream_ptr_t sp, size_t len, struct png_header_t header, uint8_t *output)
{
   int out_index = 0;
   uint8_t *buf = sp.data;
   uint8_t BFINAL, BTYPE;
   uint16_t stream = *(uint16_t *) (buf + sp.byte_index); 
   BFINAL = (stream >> sp.bit_index) & 0x01;
   ++sp.bit_index;
   BTYPE = stream >> (sp.bit_index) & 0x03;
   sp.bit_index += 2;

   printf("\t\tBFINAL: %01X\n\t\tBTYPE: %01X\n", BFINAL, BTYPE);

   enum zlib_compression_t { NONE=0, FIXED, DYNAMIC, ERROR };
   switch (BTYPE)
   {
   case NONE:;
      sp.byte_index += sp.bit_index >> 3;
      sp.bit_index = 0;
      uint16_t LEN, NLEN;
      LEN = ((uint16_t) * (buf + 2) << 8) | *(buf + 1);
      NLEN = ((uint16_t) * (buf + 4) << 8) | *(buf + 3);
      printf("\t\tLEN: %04X\t(%d bytes)\n\t\tNLEN: %04X\nUncompressed\n", LEN, LEN, NLEN);

      if ((NLEN ^ LEN) != 0xFFFF)
      {
         printf("Error, LEN != NLEN\n");
         break;
      }
      break;
   case FIXED:
      decompress(sp, len, output);
      break;
   case DYNAMIC:
      do
      {
         decompress_dynamic(&sp, len, output, &out_index);
         printf("Len: %llu, Index: %llu, Output index: %d\n--------------------------------\n", len, sp.byte_index, out_index);
         if(sp.byte_index < len)
         {
            stream = *(uint16_t *) (buf + sp.byte_index);
            BFINAL = (stream >> sp.bit_index) & 0x01;
            ++sp.bit_index;
            BTYPE = stream >> (sp.bit_index) & 0x03;
            sp.bit_index += 2;
            printf("\t\tBFINAL: %01X\n\t\tBTYPE: %01X\n", BFINAL, BTYPE);
         }
      } while (!BFINAL && sp.byte_index < len);

      break;
   case ERROR:
      printf("Invalid Deflate compression in block header\n");
      printf("\t\tBFINAL: %01X\n\t\tBTYPE: %01X\t%llu\t%08X\n", BFINAL, BTYPE, len, header.name);
      return -1;
   }
   return 0;
}

int load_png(const char *filepath)
{
   size_t loaded;
   uint64_t file_header;
   uint8_t *temp_buffer;
   uint8_t *image; (void) image;

   // FILE *png_ptr = fopen("E:\\Users\\Ben\\Pictures\\bitmap.png", "rb");
   FILE *png_ptr = fopen(filepath, "rb");
   if (png_ptr == NULL)
   {
      printf("Failed to load file\n");
      return -1;
   }
   loaded = fread(&file_header, sizeof(file_header), 1, png_ptr);
   if (loaded != 1)
   {
      printf("Failed to read file\n");
      return -1;
   }
   if(file_header != PNG_HEADER)
   {
      printf("File is not a PNG\n");
      return -1;
   }
   
   long int filesize;
   fseek(png_ptr, 0L, SEEK_END);
   filesize = ftell(png_ptr);

   if(filesize < 0)
   {
      printf("Error determining file size\n");
      return -1;
   }
   printf("File size: %ld\n", filesize);
   fseek(png_ptr, sizeof(file_header), SEEK_SET);

   struct png_header_t png_header;

   uint32_t chunk_length;
   fread(&chunk_length, sizeof(chunk_length), 1, png_ptr);
   chunk_length = order_png32_t(chunk_length);
   if(chunk_length != sizeof(png_header) - sizeof(png_header.crc) - sizeof(png_header.name))
   {
      printf("Error, invalid header size\n");
      return -1;
   }

   fread(&png_header, sizeof(png_header), 1, png_ptr);
   if(png_header.name != PNG_IHDR)
   {
      printf("Error, header chunk missing\n");
      return -1;
   }
   uint32_t crc_check = compute_crc((uint8_t*)&png_header, 17);
   crc_check = order_png32_t(crc_check);
   if(crc_check != png_header.crc)
   {
      printf("CRC check failed - Header corrupt\n");
      return -1;
   }

   png_header.width = order_png32_t(png_header.width);
   png_header.height = order_png32_t(png_header.height);
   // printf("Chunk: %c%c%c%c, Length: %u\n", (char)(png_header.name>>24), (char)(png_header.name>>16), (char)(png_header.name>>8), (char)png_header.name, png_header.length);
   printf("Width:              %u\n", png_header.width);
   printf("Height:             %u\n", png_header.height);
   printf("Bit Depth:          %u\n", png_header.bit_depth);
   printf("Colour Type:        %u\n", png_header.colour_type);
   printf("Compression Method: %u\n", png_header.compression_method);
   printf("Filter Method:      %u\n", png_header.filter_method);
   printf("Interlace Method:   %u\n", png_header.interlace_method);

   if(png_header.width == 0)
   {
      printf("Zero width detected\n");
      return -1;
   }
   if(png_header.height == 0)
   {
      printf("Zero height detected\n");
      return -1;
   }
   if(png_header.compression_method != 0)
   {
      printf("Undefined compression method specified\n");
      return -1;
   }
   if(png_header.filter_method != 0)
   {
      printf("Undefined filter method specified\n");
      return -1;
   }
   if(png_header.interlace_method != 0 && png_header.interlace_method != 1)
   {
      printf("Undefined interlace method specified\n");
      return -1;
   }

   uint32_t bits_per_pixel = png_header.bit_depth; // To map to 0-255 : 1 -> x255, 2 -> x85, 4 -> x17
   switch(png_header.colour_type)
   {
      case Greyscale:
         if(!(png_header.bit_depth == 8 || png_header.bit_depth == 16 || png_header.bit_depth == 4 || png_header.bit_depth == 2 || png_header.bit_depth == 1))
         {
            printf("Illegal bit depth for colour type 0 (Greyscale)\n");
            return -1;
         }
         break;
      case Indexed_colour:
         if(!(png_header.bit_depth == 8 || png_header.bit_depth == 4 || png_header.bit_depth == 2 || png_header.bit_depth == 1))
         {
            printf("Illegal bit depth for colour type 3 (Indexed)\n");
            return -1;
         }
         break;
      case Truecolour:
         if (!(png_header.bit_depth == 8 || png_header.bit_depth == 16))
         {
            printf("Illegal bit depth for colour type 2 (Truecolour)\n");
            return -1;
         }
         bits_per_pixel *= 3;
         break;
      case TruecolourAlpha:
         if (!(png_header.bit_depth == 8 || png_header.bit_depth == 16))
         {
            printf("Illegal bit depth for colour type 6 (Truecolour + Alpha)\n");
            return -1;
         }
         bits_per_pixel *= 4;
         break;
      case GreyscaleAlpha:
         if (!(png_header.bit_depth == 8 || png_header.bit_depth == 16))
         {
            printf("Illegal bit depth for colour type 4 (Greyscale + Alpha)\n");
            return -1;
         }
         bits_per_pixel *= 2;
         break;
      default:
         printf("Illegal colour type\n");
         return -1;
   }
   uint8_t stride = (bits_per_pixel <8) ? 1 : bits_per_pixel >> 3;

   struct pixel_layout_t{
      uint32_t width;
      uint32_t height;
   } sub_images[7]; // 56 bytes   
   uint32_t buffer_size = 0;

   if(png_header.interlace_method == 1)
   {
      sub_images[0].width = (png_header.width + 7) >> 3;
      sub_images[0].height = (png_header.height + 7) >> 3;
      sub_images[1].width = (png_header.width + 3) >> 3;
      sub_images[1].height = sub_images[0].height;
      sub_images[2].width = (png_header.width + 3) >> 2;
      sub_images[2].height = (png_header.height + 3) >> 3;
      sub_images[3].width = (png_header.width + 1) >> 2;
      sub_images[3].height = (png_header.height + 3) >> 2;
      sub_images[4].width = (png_header.width + 1) >> 1;
      sub_images[4].height = (png_header.height + 1) >> 2;
      sub_images[5].width = png_header.width >> 1;
      sub_images[5].height = (png_header.height + 1) >> 1;
      sub_images[6].width = png_header.width;
      sub_images[6].height = png_header.height >> 1;

      for(int i=0; i<7; i++) { 
         uint32_t w = (bits_per_pixel >> 3) * sub_images[i].width + ((((bits_per_pixel % 8) * sub_images[i].width) + 0x07) >> 3); // Bytes per scanline
         printf("\tPass %d: %dpx x %dpx -> %d byte(s) per scanline\n", i+1, sub_images[i].height, sub_images[i].width, w);
         buffer_size += w * sub_images[i].height;
      }
      buffer_size += ((png_header.height + 7) >> 3) * 15;
   }
   else
   {
      sub_images[0].width = png_header.width;
      uint32_t w = (bits_per_pixel >> 3) * sub_images[0].width + ((((bits_per_pixel % 8) * sub_images[0].width) + 0x07) >> 3);
      sub_images[0].height = png_header.height;
      printf("\t%d x %d\n", sub_images[0].height, w);
      buffer_size = (w + 1) * sub_images[0].height;
   }

   temp_buffer = malloc(buffer_size);
   image = malloc(buffer_size); // TODO: Change to correct image size

   printf("Bits per pixel: %d\n", bits_per_pixel);

   enum critical_chunks { IHDR=0, PLTE, IDAT, IEND } chunk_state = IHDR;

   while(chunk_state != IEND && fread(&chunk_length, sizeof(chunk_length), 1, png_ptr) != 0 && feof(png_ptr) == 0)
   {
      uint32_t chunk_name;
      chunk_length = order_png32_t(chunk_length);

      uint8_t *chunk_buffer = malloc(sizeof(chunk_name) + chunk_length + sizeof(crc_check));
      
      fread(chunk_buffer, sizeof(chunk_name) + chunk_length + sizeof(crc_check), 1, png_ptr);
      crc_check = compute_crc(chunk_buffer, chunk_length + sizeof(chunk_name));
      if(crc_check != order_png32_t(*(uint32_t*)(chunk_buffer + sizeof(chunk_name) + chunk_length)))
      {
         printf("CRC check failed for %c%c%c%c chunk\n", *chunk_buffer, *(chunk_buffer+1), *(chunk_buffer+2), *(chunk_buffer+3));  
      }
      else
      {
         uint8_t CMF, FLG, CINFO, CM, FLEVEL, FDICT;
         uint16_t FCHECK_RESULT;

         switch(*(uint32_t*)chunk_buffer)
         {
            case PNG_PLTE:
               printf("PLTE - Not implemented\n");
               break;
            case PNG_IDAT:
               // Breaks between IDAT chunks may occur at any time, need to buffer across chunk boundaries.
               CMF = *(chunk_buffer + sizeof(chunk_name));
               FLG = *(chunk_buffer + sizeof(chunk_name) + 1);
               FCHECK_RESULT = ((CMF << 8) | FLG) % 31;
               if(FCHECK_RESULT)
               {
                  printf("zlib FCHECK failed\n");
                  return -1;
               }
               CM = CMF & 0x0f;
               if(CM != 8)
               {
                  printf("Invalid compression method specified in zlib header\n");
                  return -1;
               }
               CINFO = CMF >> 4;
               if(CINFO > 7)
               {
                  printf("Invalid window size specified in zlib header\n");
                  return -1;
               }
               FDICT = (FLG & 0x20) >> 5;
               if(FDICT)
               {
                  printf("Dictionary cannot be specified for PNG in zlib header\n");
                  return -1;
               }
               FLEVEL = FLG >> 6;

               printf("IDAT - %d bytes\n\tCINFO: %02X\n\tCM: %02X\n\tFLEVEL: %02X\n\tFDICT: %02X\n\tFCHECK: %02X (%d)\n", chunk_length, CINFO, CM, FLEVEL, FDICT, FLG & 0x1f, FCHECK_RESULT);
               
               int zlib_offset = sizeof(chunk_name) + sizeof(CMF) + sizeof(FLG);
               int zlib_data_length = chunk_length - sizeof(CMF) - sizeof(FLG) - ADLER32_SIZE;
               printf("Buffer size: %d Chunk length: %d\n", buffer_size, chunk_length);
               struct stream_ptr_t temp = { .data = (chunk_buffer + zlib_offset), .byte_index = 0, .bit_index = 0 };
               decompress_zlib(temp, zlib_data_length, png_header, temp_buffer); 

               // printf("Data:\n");
               // for(uint32_t i=0;i<buffer_size; i++)
               // {
               //    printf("%02X ", *(temp_buffer+i));
               // }
               // printf("\n%d\n", buffer_size);

               if(png_header.interlace_method == 1)
               {
                  int subindex = 0;
                  for(int image = 0; image < 7; image++)
                  {
                     printf("Sub-image %d:\n", image);
                     uint32_t w = 1 + sub_images[image].width * (bits_per_pixel >> 3) + (((sub_images[image].width * (bits_per_pixel % 8)) + 0x07) >> 3); // Bytes per scanline
                     png_filter(temp_buffer + subindex, w, sub_images[image].height, stride);
                     subindex += w * sub_images[image].height;
                  }
               }
               else
               {
                  uint32_t w = 1 + png_header.width * (bits_per_pixel >> 3) + (((png_header.width * (bits_per_pixel % 8)) + 0x07) >> 3); // Bytes per scanline
                  png_filter(temp_buffer, w, png_header.height, stride);
               }

               printf("Filtered:\n");
               // for(uint32_t i=0;i<buffer_size; i++)
               // {
               //    printf("%02X ", *(temp_buffer+i));
               // }
               // printf("\nBuffer size: %d\n", buffer_size);


               break;
            case PNG_IEND:
               printf("IEND\n");
               break;
            case PNG_cHRM:
            case PNG_gAMA:
            case PNG_iCCP:
            case PNG_sBIT:
            case PNG_sRGB:
            case PNG_bKGD:
            case PNG_hIST:
            case PNG_tRNS:
            case PNG_pHYs:
            case PNG_sPLT:
            case PNG_tIME:
            case PNG_iTXt:
            case PNG_tEXt:
            case PNG_zTXt:
               printf("Chunk not implemented\n");
               break;
            default:
               printf("Unknown chunk\n");
               break;
            case PNG_IHDR:
               printf("Error, invalid header chunk\n");
               break;
         }
         // printf("Length: %u\n", chunk_length);

      }

      free(chunk_buffer);
   }
   
   fclose(png_ptr);
   free(temp_buffer);
   return 0;
}

/*
   Store alphabets at root level for use between blocks.
   Calculate limits for each alphabet length
      e.g. defaults:    7  24
                        8  200 (split)
                        9  512
   
   Build lookup tables for alphabets, need 2 stages:
      1. Modify Lit/Len/Dist to index for lookup (convert from N bits to 0-HLEN/HDIST value)
      2. Lookup value

   HCLEN + uint8_t *codes
   HLIT + uint16_t *alphabet
   HDIST + uint16_t *alphabet

   Stream read
   ===========
   array + HCLEN/HLIT/HDIST
   Track by index/bit offset
   Track read state:    idle, block_header, code, alphabet, fixed read, dynamic read
   
   Check BFINAL and assess failed reads.
      Read header (BFINAL/BTYPE)
      Read uncompressed/dynamic header
      Read code lengths
      Read lit/len alphabet
      Read dist alphabet

   Return state and partial buffer at end of block
   Append to next block

   00000000 00000XXX XXXXXXXX XX
              a   b     c     d
   total = b+c+d;
   a = (8 - ((total-d) % 8)) & 0x07;
   increment = (total+a) >> 3;
*/

struct code_t 
{
   uint16_t offset;
   uint16_t limit;
   uint8_t len;
};

void huffbuild (uint8_t *codes, size_t code_len)
{
   #define CODE_SIZE 8
   
   uint16_t count[CODE_SIZE] = { 0 };
   for(size_t i=0; i<code_len; i++)
   { 
      count[*(codes+i)]++;
   }

   uint16_t total = 0;
   uint16_t start[CODE_SIZE] = { 0 };
   struct code_t lookup_map[CODE_SIZE];
   // printf("N\tcount\tstart\ttotal\tlimit\toffset\n");
   for(int i=2; i<CODE_SIZE; i++)
   {
      lookup_map[i].len = i;
      start[i] = (count[i-1] + start[i-1]) << 1;
      lookup_map[i].offset = start[i] - total;
      total += count[i];
      lookup_map[i].limit = lookup_map[i].offset + total;
      // printf("%d\t%d\t%d\t%d\t%d\t%d\n", lookup_map[i].code_len, count[i], start[i], total, lookup_map[i].limit, lookup_map[i].offset);
   }

   uint8_t lookup_table[code_len];
   // printf("\nindex\tcode\tvalue\toffset\tlookup\n");
   for(size_t i = 0; i < code_len; i++)
   {
      if(codes[i] != 0)
      {
         // printf("%lld\t%u\t%d\t%d\t%d\n", i, codes[i], start[codes[i]], lookup_map[codes[i]].offset, start[codes[i]] - lookup_map[codes[i]].offset);
         lookup_table[start[codes[i]] - lookup_map[codes[i]].offset] = i;
         start[codes[i]]++;
      }
   }

   uint16_t new_index = 0;
   for(int i = 1; i<CODE_SIZE; i++)
   {
      if(count[i])
      {
         lookup_map[new_index] = lookup_map[i];
         new_index++;
      }
   }

   printf("Len\tLimit\tOffset\n");
   for(size_t i = 0; i < new_index; i++) { printf("%d\t%d\t%d\n", lookup_map[i].len, lookup_map[i].limit, lookup_map[i].offset); }
   printf("\n");
   for(size_t i = 0; i < total; i++) { printf("%d, ", lookup_table[i]); }
}

void partial_read (struct stream_ptr_t *in) 
{
   // uint8_t code_map[] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
   struct stream_ptr_t chunk = *in; // Don't use indirect in loop
   union dbuf stream;
   stream.stream = *(uint32_t *) (chunk.data + chunk.byte_index);
   stream.stream >>= chunk.bit_index;
   
   uint8_t bits_read = 7; // Depends on coding scheme
   while(chunk.byte_index < chunk.len)
   {
      // Processing here...

      // Update stream pointer and read buffer
      chunk.bit_index += bits_read;
      chunk.byte_index = chunk.byte_index + (chunk.bit_index >> 3);
      chunk.bit_index = chunk.bit_index % 8;
      stream.stream = *(uint32_t *) (chunk.data + chunk.byte_index);
      stream.stream >>= chunk.bit_index;
   }

   chunk.bit_index = (chunk.byte_index - in->len) * 8 + chunk.bit_index;
   if(chunk.bit_index) // Undo stream pointer increment from loop
   {
      chunk.bit_index = (8 - ((bits_read - chunk.bit_index) % 8)) & 0x07;
      chunk.byte_index -= (bits_read + chunk.bit_index) >> 3;
   }

   printf("%llu : %d\n", chunk.byte_index, chunk.bit_index);
   in->bit_index = chunk.bit_index;
   in->byte_index = chunk.byte_index;
}

int main(int argc, char *argv[])
{
   (void)argv[argc-1];
   printf("OS: %s\n", OS_TARGET);

   // load_png(argv[1]);
   uint8_t test_data[] = { 0x55,0x33,0x77,0xee, 0xaa, 0xbb }; // 10111011 10101010 11101110 01110 111 00110011 01010101
   struct stream_ptr_t test = { .byte_index = 2, .bit_index = 3, .len = sizeof(test_data), .data = test_data };

   partial_read(&test);
   uint8_t data[] = { 3, 0, 7, 7, 7, 7, 6, 4, 2, 2, 3, 3, 0, 0, 0, 0, 7, 7, 0 }; // Sort during read
   huffbuild(data, sizeof(data));
   return 0;
}