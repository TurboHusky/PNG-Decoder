#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "png_utils.h"
#include "crc.h"
#include "decompress.h"

#ifdef __MINGW32__
   #define OS_TARGET "windows"
#endif

#ifdef __linux__
   #define OS_TARGET "linux"
#endif

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
   // #define PNG_cICP
   // #define PNG_acTL
   // #define PNG_fcTL
   // #define PNG_fdAT
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
   #error Endianess not supported
   // #define PNG_HEADER 0x89504E470D0A1A0A                      
#else
   # error Endianess not defined
#endif


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

int load_png(const char *filepath)
{
   size_t loaded;
   uint64_t file_header;
   uint8_t *temp_buffer;
   uint8_t *image; (void) image;

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

   uint8_t *chunk_buffer = NULL;

   while(chunk_state != IEND && fread(&chunk_length, sizeof(chunk_length), 1, png_ptr) != 0 && feof(png_ptr) == 0)
   {
      uint32_t chunk_name;
      chunk_length = order_png32_t(chunk_length);

      chunk_buffer = realloc(chunk_buffer, sizeof(chunk_name) + chunk_length + sizeof(crc_check));
      
      fread(chunk_buffer, sizeof(chunk_name) + chunk_length + sizeof(crc_check), 1, png_ptr);
      crc_check = compute_crc(chunk_buffer, chunk_length + sizeof(chunk_name));
      if(crc_check != order_png32_t(*(uint32_t*)(chunk_buffer + sizeof(chunk_name) + chunk_length)))
      {
         printf("CRC check failed for %c%c%c%c chunk\n", *chunk_buffer, *(chunk_buffer+1), *(chunk_buffer+2), *(chunk_buffer+3));  
      }
      else
      {
         switch(*(uint32_t*)chunk_buffer)
         {
            case PNG_PLTE:
               // Required for colour type 3
               // Optional for colour types 2 and 6
               // Not allowed for colour types 0 and 4
               printf("PLTE - Not implemented\n");
               break;
            case PNG_IDAT:
               // Multiple chunks must be consecutive
               printf("IDAT - %d bytes\n", chunk_length);
               if (zlib_header_check((chunk_buffer + sizeof(chunk_name))) != ZLIB_NO_ERR)
               {
                  printf("IDAT - zlib header check failed\n");
                  break; 
               }

               int deflate_offset = sizeof(chunk_name) + ZLIB_HEADER_SIZE;
               int deflate_data_length = chunk_length - ZLIB_HEADER_SIZE - ZLIB_ADLER32_SIZE;
               printf("Buffer size: %d Chunk length: %d\n", buffer_size, chunk_length);
               struct stream_ptr_t temp = { .data = (chunk_buffer + deflate_offset), .byte_index = 0, .bit_index = 0 };
               deflate(temp, deflate_data_length, png_header, temp_buffer);

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
            case PNG_sBIT: //                                                 Before PLTE
            // case PNG_cICP: //                                                 Before PLTE
            case PNG_cHRM: // Overridden by sRGB or iCCP or cICP              Before PLTE
            case PNG_gAMA: // Overridden by sRGB or iCCP or cICP              Before PLTE
            case PNG_iCCP: // Overridden by cICP                              Before PLTE
            case PNG_sRGB: // Overridden by cICP                              Before PLTE
            case PNG_bKGD: //                                                 After PLTE, before IDAT
            case PNG_hIST: //                                                 After PLTE, before IDAT
            case PNG_tRNS: //                                                 After PLTE, before IDAT
            // case PNG_eXIf: //                                                  Before IDAT
            case PNG_pHYs: //                                                 Before IDAT
            case PNG_sPLT: //                                                 Before IDAT
            case PNG_tIME:
            case PNG_iTXt:
            case PNG_tEXt:
            case PNG_zTXt:
            // case PNG_acTL:// acTL, fcTL and fdAT for animated PNG
            // case PNG_fcTL:
            // case PNG_fdAT:
               printf("Chunk %c%c%c%c not implemented\n", (char)(*(uint32_t*)chunk_buffer&0xFFFF), (char)((*(uint32_t*)chunk_buffer>>8)&0xFFFF), (char)((*(uint32_t*)chunk_buffer>>16)&0xFFFF), (char)(*(uint32_t*)chunk_buffer>>24));
               break;
            default:
               printf("Unrecognised chunk %c%c%c%c\n", (char)(*(uint32_t*)chunk_buffer&0xFFFF), (char)((*(uint32_t*)chunk_buffer>>8)&0xFFFF), (char)((*(uint32_t*)chunk_buffer>>16)&0xFFFF), (char)(*(uint32_t*)chunk_buffer>>24));
               break;
            case PNG_IHDR:
               printf("Error, invalid header chunk\n");
               break;
         }
         // printf("Length: %u\n", chunk_length);
      }
   }

   fclose(png_ptr);
   free(chunk_buffer);
   free(image);
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

   load_png(argv[1]);

   return 0;
}