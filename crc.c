#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __MINGW32__
   #define OS_TARGET "windows"
#endif

#ifdef __linux__
   #define OS_TARGET "linux"
#endif

#define PNG_HEADER 0x0A1A0A0D474E5089  // Network (Big endian) format

/* Table of CRCs of all 8-bit messages. */
unsigned long crc_table[256];

/* Flag: has the table been computed? Initially false. */
int crc_table_computed = 0;

/* Make the table for a fast CRC. */
void make_crc_table(void)
{
   unsigned long c;
   int n, k;

   for (n = 0; n < 256; n++)
   {
      c = (unsigned long)n;
      for (k = 0; k < 8; k++)
      {
         if (c & 1)
            c = 0xedb88320L ^ (c >> 1);
         else
            c = c >> 1;
      }
      crc_table[n] = c;
//      printf("Table: %d, %02X %02X %02X %02X\n", n, (c>>24)&0xff, (c>>16)&0xff, (c>>8)&0xff, c&0xff);
   }
   crc_table_computed = 1;
}

/* Update a running CRC with the bytes buf[0..len-1]--the CRC
   should be initialized to all 1's, and the transmitted value
   is the 1's complement of the final running CRC (see the
   crc() routine below)). */

unsigned long update_crc(unsigned long crc, unsigned char *buf, int len)
{
   unsigned long c = crc;
   int n;

   if (!crc_table_computed)
      make_crc_table();
   for (n = 0; n < len; n++)
   {
      c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
   }
   return c;
}

/* Return the CRC of the bytes buf[0..len-1]. */
unsigned long compute_crc(unsigned char *buf, int len)
{
   return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

static inline uint32_t order_png32_t(uint32_t value)
{
   #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      return (value & 0x000000FF) << 24 | (value & 0x0000FF00) << 8 | (value & 0x00FF0000) >> 8 | (value & 0xFF000000) >> 24;
   #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      return value;
   #else
      # error, endianess not defined
   #endif
}

/* Naiive implementation to demonstrate algorithm */
uint32_t adler32(uint8_t *buf, int len)
{
   uint16_t a = 1;
   uint16_t b = 0;

   for(int i=0; i<len; i++)
   {
      a = (a + (uint16_t) *(buf+i)) % 65521;
      b = (b + a) % 65521;
   }

   return ((uint32_t) b) << 16 | a;
}

union dbuf 
{
   uint8_t byte;           // b
   uint8_t buffer[4];      // 0 1 2 3
   uint16_t split[2];      // 0   1
   uint32_t stream;        // s s s s
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

uint8_t reverse_byte(uint8_t n)
{
   return (reverse_nibble_lookup[n&0x0F] << 4) | reverse_nibble_lookup[n>>4];
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

void decompress(unsigned char *buf, int len, uint8_t *out)
{ 
   uint8_t BFINAL = *buf & 0x01;
   uint8_t BTYPE = (*buf & 0x06) >> 1;

   printf("\nDecompressing...\nBFINAL: %02X BTYPE: %02X\n", BFINAL, BTYPE);

   union dbuf input;

   input.buffer[3] = reverse_byte(*buf);
   input.buffer[2] = reverse_byte(*(buf+1));
   input.buffer[1] = reverse_byte(*(buf+2));
   input.buffer[0] = reverse_byte(*(buf+3));

   uint32_t shift = 3;
   input.stream <<= shift;

   int index = 0;
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
         printf("%d ", input.buffer[3]-48);
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
         printf("%d ", input.buffer[3]);
         input.stream <<= 8;
         shift += 9;
      }

      if(test_alphabet.value)
      {
         uint16_t extra = 0;
         if(test_alphabet.extra)
         {
            for(int i=0; i<test_alphabet.extra; i++)
            {
               extra |= (input.buffer[3] & 0x80)>>(7-i); // Read extra bits as machine integer (MSB first).
               input.stream <<= 1;
               shift++;
            }
         }
         int copy_size = test_alphabet.value + extra;
         printf("%d:", test_alphabet.value + extra);
         test_alphabet = distance_alphabet[input.buffer[3] >> 3];
         input.stream <<= 5;
         shift += 5;

         extra = 0;
         if(test_alphabet.extra)
         {
            for(int i=0; i<test_alphabet.extra; i++)
            {
               extra |= (input.split[1] & 0x8000)>>(15-i); // Read extra bits as machine integer (MSB first).
               input.stream <<= 1;
               shift++;
            }
         }

         int offset = test_alphabet.value + extra;
         printf("%d ", test_alphabet.value + extra);
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

void png_filter(uint8_t *buf, uint32_t width, uint32_t height, uint32_t offset)
{
   filter_t filters[5];
   filters[0] = filter_type_0;
   filters[1] = filter_type_1;
   filters[2] = filter_type_2;
   filters[3] = filter_type_3;
   filters[4] = filter_type_4;

   uint8_t a = 0;    // Pixel samples:    c  b
   uint8_t b = 0;    //                   a  x
   uint8_t c = 0;
   uint8_t x = 0;
   
   uint32_t index = 0;
   uint32_t col_index = 1;
   uint8_t filter_type = *buf;

   // Remember first row entry is filter type, for pixel data:
   // First column a & c = 0, first row b & c = 0
   while(col_index <= offset)
   {
      x = *(buf+col_index);
      *(buf+index) = filters[filter_type](x, 0, 0, 0);
      index++;
      col_index++;
   }
   while(col_index <= width)
   {
      x = *(buf+col_index);
      a = *(buf+col_index-offset-1); // Ok
      *(buf+index) = filters[filter_type](x, a, 0, 0);
      index++;
      col_index++;
   }

   for(uint32_t row_index=1; row_index<height; row_index++)
   {
      a=0;
      c=0;
      col_index = 1;
      filter_type = *(buf+(row_index*(width+1)));
      while(col_index <= offset)
      {
         x = *(buf+(row_index*(width+1))+col_index);
         b = *(buf+(row_index-1)*(width)+col_index-1);
         *(buf+index) = filters[filter_type](x, 0, b, 0);
         index++;
         col_index++;
      }
      while(col_index <= width)
      {
         x = *(buf+(row_index*(width+1))+col_index);
         a = *(buf+row_index*(width)+col_index-offset-1); // Ok
         b = *(buf+(row_index-1)*(width)+col_index-1);
         c = *(buf+(row_index-1)*(width)+col_index-offset-1);
         *(buf+index) = filters[filter_type](x, a, b, c);
         index++;
         col_index++;
      }
   }
}

int main()//int argc, char *argv[])
{
   printf("OS: %s\n", OS_TARGET);

   unsigned char test[17] = { 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x05, 0xa1, 0x00, 0x00, 0x03, 0x1f, 0x08, 0x02, 0x00, 0x00, 0x00 };
   unsigned long crc_result = compute_crc(test, sizeof(test));
   printf("Test:      %02lX %02lX %02lX %02lX\n", (crc_result>>24)&0xff, (crc_result>>16)&0xff, (crc_result>>8)&0xff, crc_result&0xff);
   printf("Should be: 40 4C 8C 3B (download_error.png)\n");

   unsigned char ref[2] = { 0x2d, 0x8d };
   crc_result = compute_crc(ref, sizeof(ref));
   printf("Ref:       %02lX %02lX %02lX %02lX\n", (crc_result>>24)&0xff, (crc_result>>16)&0xff, (crc_result>>8)&0xff, crc_result&0xff);

   size_t loaded;
   uint64_t header;

   FILE *png_ptr = fopen("E:\\Users\\Ben\\Pictures\\RGB8.png", "rb");
   if (png_ptr == NULL)
   {
      printf("Failed to load PNG\n");
      return -1;
   }
   loaded = fread(&header, sizeof(header), 1, png_ptr);
   if (loaded != 1)
   {
      printf("PNG read failed\n");
      return -1;
   }
   if(header != PNG_HEADER)
   {
      printf("Not a PNG file\n");
      return -1;
   }
   printf("Loaded: %llu\n", loaded);
   printf("File Header: %02llX %02llX %02llX %02llX %02llX %02llX %02llX %02llX\n", header&0xff, (header>>8)&0xff, (header>>16)&0xff, (header>>24)&0xff, (header>>32)&0xff, (header>>40)&0xff, (header>>48)&0xff, (header>>56)&0xff);
   


   uint32_t crc;
   long int filesize;

   fseek(png_ptr, 0L, SEEK_END);
   filesize = ftell(png_ptr);

   if(filesize < 0)
   {
      printf("Error determining file size\n");
      return -1;
   }
   printf("File size: %ld\n", filesize);
   fseek(png_ptr, sizeof(header), SEEK_SET);
   
   struct
   {
      uint32_t length;
      uint32_t name;
      uint32_t width;
      uint32_t height;
      uint8_t bit_depth;
      uint8_t colour_type;
      uint8_t compression_method;
      uint8_t filter_method;
      uint8_t interlace_method;
      uint32_t crc;
   } image_header;

   fread(&image_header, 25, 1, png_ptr);
   image_header.length = order_png32_t(image_header.length);
   image_header.width = order_png32_t(image_header.width);
   image_header.height = order_png32_t(image_header.height);
   printf("Header: %c%c%c%c, Width: %u, Height: %u, Length: %u, CRC: %u\n",
      (char)image_header.name, (char)(image_header.name>>8), (char)(image_header.name>>16), (char)(image_header.name>>24),
      image_header.width,
      image_header.height,
      image_header.length,
      crc
   );
   printf("\tWidth:              %u\n", image_header.width);
   printf("\tHeight:             %u\n", image_header.height);
   printf("\tBit Depth:          %u\n", image_header.bit_depth);
   printf("\tColour Type:        %u\n", image_header.colour_type);
   printf("\tCompression Method: %u\n", image_header.compression_method);
   printf("\tFilter Method:      %u\n", image_header.filter_method);
   printf("\tInterlace Method:   %u\n", image_header.interlace_method);

   struct
   {
      uint32_t length;
      uint32_t name;
   } chunk_header;

   while(fread(&chunk_header, sizeof(chunk_header), 1, png_ptr) != 0 && feof(png_ptr) == 0)
   {
      chunk_header.length = order_png32_t(chunk_header.length); 
      fseek(png_ptr, chunk_header.length, SEEK_CUR); // Skip data for now
      fread(&crc, sizeof(crc), 1, png_ptr);
      printf("Chunk: %c%c%c%c, Length: %u, CRC: %u\n", (char)chunk_header.name, (char)(chunk_header.name>>8), (char)(chunk_header.name>>16), (char)(chunk_header.name>>24), chunk_header.length, crc);
   }
   
   fclose(png_ptr);

   printf("\nRGB8.png\n--------\n");
   unsigned char png_ihdr[17] = {
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x08, 0x06, 0x00, 0x00, 0x00
   };
   crc_result = compute_crc(png_ihdr, sizeof(png_ihdr));
   printf("IHDR:      %02lX %02lX %02lX %02lX\n", (crc_result>>24)&0xff, (crc_result>>16)&0xff, (crc_result>>8)&0xff, crc_result&0xff);
   printf("Should be: 73 7A 7A F4\n");

   unsigned char png_phys[13] = {
      0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0e, 0xc4, 0x00, 0x00, 0x0e, 0xc4, 0x01
   };
   crc_result = compute_crc(png_phys, sizeof(png_phys));
   printf("pHYs:      %02lX %02lX %02lX %02lX\n", (crc_result>>24)&0xff, (crc_result>>16)&0xff, (crc_result>>8)&0xff, crc_result&0xff);
   printf("Should be: 95 2B 0E 1B\n");
   unsigned char png_bkgd[10] = {
      0x62, 0x4b, 0x47, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
   };
   crc_result = compute_crc(png_bkgd, sizeof(png_bkgd));
   printf("bkGD:      %02lX %02lX %02lX %02lX\n", (crc_result>>24)&0xff, (crc_result>>16)&0xff, (crc_result>>8)&0xff, crc_result&0xff);
   printf("Should be: F9 43 BB 7F\n");
   unsigned char png_time[11] = {
      0x74, 0x49, 0x4d, 0x45, 0x07, 0xe6, 0x07, 0x1f, 0x07, 0x0c, 0x38  
   };
   crc_result = compute_crc(png_time, sizeof(png_time));
   printf("tIME:      %02lX %02lX %02lX %02lX\n", (crc_result>>24)&0xff, (crc_result>>16)&0xff, (crc_result>>8)&0xff, crc_result&0xff);
   printf("Should be: 37 6B 28 F1\n");

   unsigned char png_idat[627] = {
      0x49, 0x44, 0x41, 0x54, // Name
      0x58, 0x47, 0x63, 0x64, 0xF8, 0x0F, 0x84, 0x14, 0x80, 0xB7, 0xB2, 0x2A, 0x50, 0x16, 0x76, 0x20, 0xFC, 0xE4, 0x36, 0x94, 0x85, 0x1D, 0x30, 0xFE, 0x07, 0x02, 0x28, 0x7B, 0x40, 0x00, 0xC1, 0x10, 0x20, 0xE4, 0x43, 0x42, 0x60, 0xF8, 0x87, 0x00, 0x21, 0x1F, 0x12, 0x02, 0x83, 0x20, 0x04, 0x18, 0x30, 0x43, 0x60, 0xE1, 0xE4, 0x6E, 0x06, 0x76, 0x36, 0x36, 0x86, 0x88, 0xF4, 0x7C, 0xA8, 0x08, 0x7E, 0x60, 0xA4, 0xAB, 0x05, 0xA6, 0x0D, 0x75, 0xB5, 0x19, 0xE6, 0x2E, 0x5B, 0x0D, 0x66, 0x13, 0x0B, 0xB0, 0x3A, 0x80, 0x54, 0x90, 0x16, 0x1B, 0xC1, 0x60, 0x6D, 0x66, 0xCC, 0x20, 0xC8, 0xCF, 0xC7, 0xE0, 0x17, 0x97, 0x0E, 0x15, 0x25, 0x1E, 0x80, 0x1C, 0x80, 0x13, 0x03, 0x7D, 0x07, 0xC6, 0xD8, 0xE4, 0x90, 0xB1, 0x87, 0xA3, 0x2D, 0x56, 0x71, 0x22, 0x30, 0x56, 0x41, 0x38, 0xAE, 0xC8, 0x4D, 0xFF, 0xBF, 0x69, 0xF1, 0xAC, 0xFF, 0x85, 0xE9, 0x89, 0x58, 0xE5, 0x29, 0xC5, 0x4C, 0x40, 0x02, 0x2F, 0xE8, 0x98, 0x3C, 0x93, 0xE1, 0xD1, 0x93, 0x67, 0x0C, 0xCA, 0x0A, 0xF2, 0x50, 0x11, 0xEA, 0x02, 0xAA, 0xA4, 0x01, 0x4A, 0x00, 0xC1, 0x10, 0xA0, 0x35, 0x18, 0x75, 0xC0, 0xC8, 0x70, 0x00, 0xB0, 0x8C, 0x80, 0xB2, 0x30, 0x01, 0x49, 0xB9, 0x60, 0x4E, 0x5F, 0x1B, 0xC3, 0xD5, 0x9B, 0xB7, 0x19, 0xFA, 0x67, 0xCE, 0x87, 0x8A, 0xE0, 0x07, 0xC0, 0xB2, 0x83, 0x41, 0x56, 0x5A, 0x92, 0x41, 0x4A, 0x42, 0x9C, 0x21, 0x22, 0x0D, 0x7B, 0xB1, 0x4E, 0x52, 0x08, 0x80, 0x2C, 0x77, 0xB1, 0xB7, 0xC6, 0xEB, 0x23, 0x64, 0x20, 0x2D, 0x29, 0xC1, 0xA0, 0xA7, 0xA5, 0xC1, 0xD0, 0x05, 0x2C, 0x4B, 0x70, 0x01, 0x66, 0x20, 0x6E, 0x80, 0x30, 0x09, 0x83, 0x13, 0x67, 0x2F, 0x30, 0x18, 0xEB, 0xE9, 0x30, 0xB8, 0x3B, 0xDA, 0x31, 0x7C, 0xF8, 0xF0, 0x91, 0xE1, 0xCE, 0x83, 0x47, 0x50, 0x19, 0x54, 0xD0, 0x50, 0x9A, 0xC7, 0xE0, 0xEB, 0xEE, 0xCC, 0x20, 0x29, 0x2E, 0xCA, 0x70, 0xE1, 0xCA, 0x35, 0x86, 0xE5, 0xEB, 0xB7, 0x40, 0x65, 0x30, 0x01, 0xC9, 0x69, 0x60, 0xF7, 0x81, 0xC3, 0x0C, 0x62, 0x22, 0xC2, 0x0C, 0xA1, 0x7E, 0x5E, 0x50, 0x11, 0x4C, 0x20, 0x22, 0x24, 0xC4, 0xF0, 0xE4, 0xD9, 0x73, 0x06, 0x50, 0x4D, 0xDF, 0xD0, 0x3D, 0x09, 0x2A, 0x8A, 0x1D, 0x90, 0xEC, 0x80, 0x1D, 0xFB, 0x0F, 0x33, 0xAC, 0xDA, 0xB8, 0x95, 0x41, 0x02, 0xE8, 0xBB, 0xE9, 0x5D, 0x4D, 0x18, 0xD1, 0x01, 0x4A, 0x27, 0x39, 0x95, 0x0D, 0x0C, 0xFC, 0x7C, 0xBC, 0x0C, 0x8C, 0x8C, 0xA0, 0x24, 0x86, 0x1F, 0x90, 0x95, 0x0B, 0x40, 0x89, 0xF0, 0xC6, 0xED, 0x7B, 0x0C, 0x0A, 0xB2, 0x32, 0x0C, 0xB1, 0x61, 0x81, 0x60, 0x47, 0x80, 0xF0, 0xD2, 0xE9, 0x7D, 0xE0, 0x74, 0x02, 0x02, 0xBC, 0x3C, 0x3C, 0x0C, 0xD1, 0x99, 0x45, 0x60, 0x36, 0x3E, 0x40, 0x51, 0x5D, 0x00, 0x0A, 0x81, 0xAF, 0xDF, 0xBE, 0x83, 0x7D, 0xCA, 0xC5, 0xC9, 0x01, 0x16, 0xDB, 0xB8, 0x7D, 0x37, 0x83, 0xBF, 0xA7, 0x2B, 0x43, 0x66, 0x59, 0x1D, 0x98, 0x4F, 0x08, 0x50, 0x54, 0x0E, 0x7C, 0xFB, 0xFE, 0x03, 0x18, 0x0A, 0xD2, 0x0C, 0xD7, 0x80, 0xBE, 0x06, 0x59, 0x08, 0xC2, 0x16, 0x26, 0x86, 0x0C, 0x7C, 0xBC, 0x3C, 0x50, 0x15, 0x84, 0x01, 0xC5, 0xB5, 0x21, 0x28, 0xAF, 0xCB, 0x48, 0x49, 0x82, 0xD9, 0xA0, 0x84, 0x07, 0x62, 0x17, 0xD7, 0xB7, 0x81, 0xF9, 0xC4, 0x82, 0xFF, 0x6B, 0xC5, 0xCC, 0xFF, 0xB7, 0xF3, 0x6B, 0x82, 0x1B, 0x08, 0xE4, 0x62, 0x60, 0x1A, 0x20, 0xAB, 0xD1, 0x02, 0x0E, 0x81, 0x79, 0x22, 0x86, 0x0C, 0x2A, 0xEC, 0x7C, 0x0C, 0x37, 0x7F, 0x7C, 0x64, 0x48, 0x7D, 0x7B, 0x01, 0x28, 0x44, 0x3F, 0x00, 0x4E, 0x03, 0xEB, 0x3E, 0x3E, 0x62, 0x10, 0x66, 0x66, 0x63, 0x30, 0xE7, 0x16, 0x65, 0x00, 0x86, 0x04, 0x58, 0x82, 0x5E, 0x00, 0xEC, 0x00, 0x0F, 0x5E, 0x69, 0x86, 0xAF, 0xFF, 0xFE, 0x30, 0xB0, 0x02, 0x53, 0xB3, 0x1B, 0x9F, 0x14, 0xC3, 0x6C, 0x61, 0x03, 0xB0, 0x24, 0x3D, 0x00, 0xD8, 0x01, 0xDA, 0x1C, 0x02, 0x0C, 0x6C, 0x8C, 0xCC, 0x0C, 0x0F, 0x7E, 0x7E, 0x65, 0x38, 0xF7, 0xED, 0x2D, 0xC3, 0x9D, 0x9F, 0x9F, 0xC1, 0x92, 0xF4, 0x00, 0xE0, 0x34, 0xE0, 0xC3, 0x2A, 0x0C, 0xE6, 0x6C, 0xF9, 0xFD, 0x16, 0x4C, 0xD3, 0x0F, 0x30, 0x30, 0x00, 0x00, 0x05, 0x89, 0x0F, 0xD9
   }; // CRC 0f 17 3c 66
   crc_result = compute_crc(png_idat, sizeof(png_idat));
   printf("IDAT:      %02lX %02lX %02lX %02lX\n", (crc_result>>24)&0xff, (crc_result>>16)&0xff, (crc_result>>8)&0xff, crc_result&0xff);
   printf("Should be: E8 09 2D 8A\n");
   
   // 0101 1000 CM=8, CINFO=5
   // 0100 0111 FCHECK=7, FDICT=0, FLEVEL=1 (fast)
   // CMF FLG 0x5847 is divisible by 31, (FCHECK)

   uint8_t compressed_data[49] = { 0x63, 0xfc, 0xcf, 0x80, 0x1d, 0xb0, 0x30, 0xfc, 0xff, 0xcf, 0xc0, 0xc0, 0xc0, 0xf0, 0xef, 0x1f, 0x54, 0x80, 0x91, 0x11, 0x4a, 0x33, 0xe0, 0xd0, 0xc2, 0xf2, 0x1f, 0x87, 0x0c, 0x23, 0x03, 0x0e, 0x09, 0x96, 0xff, 0xb8, 0x6c, 0x47, 0x03, 0x0b, 0x27, 0x77, 0xaf, 0x98, 0x39, 0x11, 0x9f, 0x0a, 0x00 };
   unsigned char test_data[200];
   decompress(compressed_data, sizeof(compressed_data), test_data);
   
   uint32_t a32_check = adler32(test_data, sizeof(test_data));
   printf("\nAdler32:   %02X %02X %02X %02X\n", (a32_check>>24)&0xff, (a32_check>>16)&0xff, (a32_check>>8)&0xff, a32_check&0xff);
   printf("Should be: 5E 2B 0E 96\n");
   
   printf("\n");
   png_filter(test_data, 24, 8, 3);
   for(int j=0;j<8;j++) { 
      for(int i=0;i<24;i++) { printf("%02X ", test_data[j*24+i]); }
      printf("\n");
   } printf(" test_data[191]: %02X\n", test_data[191]);
   return 0;
}