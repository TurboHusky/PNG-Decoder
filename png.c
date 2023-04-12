#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "png_utils.h"
#include "crc.h"
#include "image.h"
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
   #define PNG_cICP 0x50434963
   #define PNG_acTL 0x4C546361
   #define PNG_fcTL 0x4C546366
   #define PNG_fdAT 0x54416466
   #define PNG_eXIf 0x66495865
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
   #error Endianess not supported
   // #define PNG_HEADER 0x89504E470D0A1A0A                      
#else
   # error Endianess not defined
#endif

#define PNG_CHUNK_TYPE_SIZE sizeof(uint32_t)
#define PNG_CHUNK_CRC_SIZE sizeof(uint32_t)

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

int check_png_file_header(uint64_t file_header, long int filesize)
{
   if(file_header != PNG_HEADER)
   {
      printf("File is not a PNG\n");
      return -1;
   }

   if(filesize < 0)
   {
      printf("Error determining file size\n");
      return -1;
   }

   printf("File size: %ld\n", filesize);

   return 0;
}

int check_png_header(uint32_t header_length, struct png_header_t* new_header, uint32_t* crc)
{
   header_length = order_png32_t(header_length);

   if(header_length != sizeof(*new_header) - sizeof(new_header->crc) - sizeof(new_header->name))
   {
      printf("Error, invalid header size\n");
      return -1;
   }

   if(new_header->name != PNG_IHDR)
   {
      printf("Error, header chunk missing\n");
      return -1;
   }
   *crc = compute_crc((uint8_t*)new_header, 17);
   *crc = order_png32_t(*crc);
   if(*crc != new_header->crc)
   {
      printf("CRC check failed - Header corrupt\n");
      return -1;
   }

   new_header->width = order_png32_t(new_header->width);
   new_header->height = order_png32_t(new_header->height);
   // printf("Chunk: %c%c%c%c, Length: %u\n", (char)(new_header->name>>24), (char)(new_header->name>>16), (char)(new_header->name>>8), (char)new_header->name, new_header->length);
   printf("Width:              %u\n", new_header->width);
   printf("Height:             %u\n", new_header->height);
   printf("Bit Depth:          %u\n", new_header->bit_depth);
   printf("Colour Type:        %u\n", new_header->colour_type);
   printf("Compression Method: %u\n", new_header->compression_method);
   printf("Filter Method:      %u\n", new_header->filter_method);
   printf("Interlace Method:   %u\n", new_header->interlace_method);

   if(new_header->width == 0)
   {
      printf("Zero width detected\n");
      return -1;
   }
   if(new_header->height == 0)
   {
      printf("Zero height detected\n");
      return -1;
   }
   if(new_header->compression_method != 0)
   {
      printf("Undefined compression method specified\n");
      return -1;
   }
   if(new_header->filter_method != 0)
   {
      printf("Undefined filter method specified\n");
      return -1;
   }
   if(new_header->interlace_method != 0 && new_header->interlace_method != 1)
   {
      printf("Undefined interlace method specified\n");
      return -1;
   }

   if (new_header->colour_type == Truecolour)
   {
      if(!(new_header->bit_depth == 8 || new_header->bit_depth == 16))
      {
         printf("Illegal bit depth for colour type 2 (Truecolour)\n");
         return -1;
      }
   }
   else if (new_header->colour_type == TruecolourAlpha)
   {
      if  (!(new_header->bit_depth == 8 || new_header->bit_depth == 16))
      {
         printf("Illegal bit depth for colour type 6 (Truecolour + Alpha)\n");
         return -1;
      }
   }
   else if (new_header->colour_type == Indexed_colour)
   {
      if (!(new_header->bit_depth == 8 || new_header->bit_depth == 4 || new_header->bit_depth == 2 || new_header->bit_depth == 1))
      {
         printf("Illegal bit depth for colour type 3 (Indexed)\n");
         return -1;
      }
   }
   else if (new_header->colour_type == Greyscale)
   {
      if(!(new_header->bit_depth == 8 || new_header->bit_depth == 16 || new_header->bit_depth == 4 || new_header->bit_depth == 2 || new_header->bit_depth == 1)) 
      {
         printf("Illegal bit depth for colour type 0 (Greyscale)\n");
         return -1;
      }
   }
   else if (new_header->colour_type == GreyscaleAlpha)
   {
      if(!(new_header->bit_depth == 8 || new_header->bit_depth == 16))
      {
         printf("Illegal bit depth for colour type 4 (Greyscale + Alpha)\n");
         return -1;
      }
   }
   else 
   {         
      printf("Illegal colour type\n");
      return -1;
   }

   return 0;
}

int load_png(FILE* png_ptr)
{
   if (png_ptr == NULL)
   {
      printf("Failed to load file\n");
      return -1;
   }

   uint64_t file_header;   
   if (fread(&file_header, sizeof(file_header), 1, png_ptr) != 1)
   {
      printf("Failed to read png header\n");
      return -1;
   }
  
   fseek(png_ptr, 0L, SEEK_END);
   if(check_png_file_header(file_header, ftell(png_ptr)) < 0)
   {
      return -1;
   }
   fseek(png_ptr, sizeof(file_header), SEEK_SET);

   struct png_header_t png_header;
   uint32_t chunk_data_size;
   uint32_t crc_check;

   fread(&chunk_data_size, sizeof(chunk_data_size), 1, png_ptr);
   fread(&png_header, sizeof(png_header), 1, png_ptr);

   if(check_png_header(chunk_data_size, &png_header, &crc_check) < 0)
   {
      return -1;
   }

   uint32_t bits_per_pixel = png_header.bit_depth; // To map to 0-255 : 1 -> x255, 2 -> x85, 4 -> x17
   switch(png_header.colour_type)
   {
      case Truecolour:
         bits_per_pixel *= 3;
         break;
      case TruecolourAlpha:
         bits_per_pixel *= 4;
         break;
      case GreyscaleAlpha:
         bits_per_pixel *= 2;
         break;
   }
   
   // uint8_t stride = (bits_per_pixel < 8) ? 1 : bits_per_pixel >> 3;

   struct interlacing_t sub_images[7]; // 56 bytes   
   uint32_t buffer_size = 0;

   if(png_header.interlace_method == 1)
   {
      set_interlacing(&sub_images[0], png_header.width, png_header.height);

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

   uint8_t *chunk_buffer = NULL;
   uint8_t *temp_buffer = malloc(buffer_size);
   uint8_t *image = malloc(buffer_size); // TODO: Change to correct image size

   // struct stream_ptr_t deflate_ptr = {
   //    .byte_index = 0,
   //    .bit_index = 0,
   //    .status = STREAM_IDLE
   // };

   printf("Output image buffer size: %d\nBits per pixel: %d\n", buffer_size, bits_per_pixel);

   enum chunk_states_t { IHDR_PROCESSED=0, PLTE_PROCESSED, READING_IDAT, IDAT_PROCESSED, EXIT_CHUNK_PROCESSING } chunk_state = IHDR_PROCESSED;

   while(fread(&chunk_data_size, sizeof(chunk_data_size), 1, png_ptr) != 0 && feof(png_ptr) == 0 && chunk_state != EXIT_CHUNK_PROCESSING)
   {
      chunk_data_size = order_png32_t(chunk_data_size);
      chunk_buffer = realloc(chunk_buffer, PNG_CHUNK_TYPE_SIZE + chunk_data_size + PNG_CHUNK_CRC_SIZE);

      fread(chunk_buffer, PNG_CHUNK_TYPE_SIZE + chunk_data_size + PNG_CHUNK_CRC_SIZE, 1, png_ptr);

      crc_check = compute_crc(chunk_buffer, chunk_data_size + PNG_CHUNK_TYPE_SIZE);
      if(crc_check != order_png32_t(*(uint32_t*)(chunk_buffer + PNG_CHUNK_TYPE_SIZE + chunk_data_size)))
      {
         printf("Error: CRC check failed for %c%c%c%c chunk\n", *chunk_buffer, *(chunk_buffer+1), *(chunk_buffer+2), *(chunk_buffer+3));  
      }
      else
      {
         uint32_t chunk_name = *(uint32_t*)chunk_buffer;
         switch(chunk_name)
         {
            case PNG_IDAT:
               if(chunk_state > READING_IDAT)
               {
                  printf("Error: Non-consecutive IDAT chunk.");
                  chunk_state = EXIT_CHUNK_PROCESSING;
               }
               else
               {
                  printf("IDAT - %d bytes\n", chunk_data_size);
                  decompress(chunk_buffer + PNG_CHUNK_TYPE_SIZE, chunk_data_size, temp_buffer);
                  
                  // printf("Data:\n");
                  // for(uint32_t i=0;i<buffer_size; i++)
                  // {
                  //    printf("%02X ", *(temp_buffer+i));
                  // }
                  // printf("\n%d\n", buffer_size);

                  // if(png_header.interlace_method == 1)
                  // {
                  //    int subindex = 0;
                  //    for(int image = 0; image < 7; image++)
                  //    {
                  //       printf("Sub-image %d:\n", image);
                  //       uint32_t w = 1 + sub_images[image].width * (bits_per_pixel >> 3) + (((sub_images[image].width * (bits_per_pixel % 8)) + 0x07) >> 3); // Bytes per scanline
                  //       png_filter(temp_buffer + subindex, w, sub_images[image].height, stride);
                  //       subindex += w * sub_images[image].height;
                  //    }
                  // }
                  // else
                  // {
                  //    uint32_t w = 1 + png_header.width * (bits_per_pixel >> 3) + (((png_header.width * (bits_per_pixel % 8)) + 0x07) >> 3); // Bytes per scanline
                  //    png_filter(temp_buffer, w, png_header.height, stride);
                  // }

                  // printf("Filtered:\n");
                  // for(uint32_t i=0;i<buffer_size; i++)
                  // {
                  //    printf("%02X ", *(temp_buffer+i));
                  // }
                  // printf("\nBuffer size: %d\n", buffer_size);
                  chunk_state = READING_IDAT;
               }
               break;
            case PNG_PLTE:
               if(chunk_state > IHDR_PROCESSED)
               {
                  printf("Error: Critical chunk PLTE out of order.");
                  chunk_state = EXIT_CHUNK_PROCESSING;
                  break;
               }
               else
               {
                  // Required for colour type 3
                  // Optional for colour types 2 and 6
                  // Not allowed for colour types 0 and 4
                  chunk_state = PLTE_PROCESSED;
               }
               printf("PLTE - Not implemented\n");
               break;
            case PNG_sBIT: // if(chunk_state < PLTE_PROCESSED)
            case PNG_cICP: // if(chunk_state < PLTE_PROCESSED)
            case PNG_cHRM: // if(chunk_state < PLTE_PROCESSED)    Overridden by sRGB or iCCP or cICP
            case PNG_gAMA: // if(chunk_state < PLTE_PROCESSED)    Overridden by sRGB or iCCP or cICP
            case PNG_iCCP: // if(chunk_state < PLTE_PROCESSED)    Overridden by cICP
            case PNG_sRGB: // if(chunk_state < PLTE_PROCESSED)    Overridden by iCCP, cICP
            case PNG_bKGD: // if(chunk_state == PLTE_PROCESSED)
            case PNG_hIST: // if(chunk_state == PLTE_PROCESSED)
            case PNG_tRNS: // if(chunk_state == PLTE_PROCESSED)
            case PNG_eXIf: // if(chunk_state < READING_IDAT)
            case PNG_pHYs: // if(chunk_state < READING_IDAT)
            case PNG_sPLT: // if(chunk_state < READING_IDAT)      multiple allowed
            case PNG_tIME: // no restrictions
            case PNG_iTXt: // no restrictions                     multiple allowed
            case PNG_tEXt: // no restrictions                     multiple allowed
            case PNG_zTXt: // no restrictions                     multiple allowed
            // acTL, fcTL and fdAT for animated PNG
            case PNG_acTL: // if(chunk_state < READING_IDAT)
            case PNG_fcTL: // one before IDAT, all others after IDAT
            case PNG_fdAT: // if(chunk_state == IDAT_PROCESSED)   multiple allowed
               printf("Chunk %c%c%c%c not implemented\n", (char)(*(uint32_t*)chunk_buffer&0xFFFF), (char)((*(uint32_t*)chunk_buffer>>8)&0xFFFF), (char)((*(uint32_t*)chunk_buffer>>16)&0xFFFF), (char)(*(uint32_t*)chunk_buffer>>24));
               break;
            case PNG_IEND:
               printf("IEND\n");
               chunk_state = EXIT_CHUNK_PROCESSING;
               break;
            case PNG_IHDR:
               printf("Error, multiple header chunks detected\n");
               chunk_state = EXIT_CHUNK_PROCESSING;
               break;
            default:
               printf("Unrecognised chunk %c%c%c%c\n", (char)(*(uint32_t*)chunk_buffer&0xFFFF), (char)((*(uint32_t*)chunk_buffer>>8)&0xFFFF), (char)((*(uint32_t*)chunk_buffer>>16)&0xFFFF), (char)(*(uint32_t*)chunk_buffer>>24));
               break;
         }
         // printf("Length: %u\n", chunk_data_size);
      }
   }

   free(chunk_buffer);
   free(image);
   free(temp_buffer);
   return 0;
}

int main(int argc, char *argv[])
{
   (void)argv[argc-1];
   printf("OS: %s\n", OS_TARGET);

   FILE *png_file = fopen(argv[1], "rb");
   load_png(png_file);
   fclose(png_file);

   return 0;
}