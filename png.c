#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "png_utils.h"
#include "crc.h"
#include "decompress.h"
#include "filter.h"

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

#define PNG_COMPRESSION_TYPE_DEFLATE 0
#define PNG_ADAPTIVE_FILTERING 0
#define PNG_INTERLACE_NONE 0
#define PNG_INTERLACE_ADAM7 1

#define PNG_CHUNK_LENGTH_SIZE sizeof(uint32_t)
#define PNG_CHUNK_TYPE_SIZE sizeof(uint32_t)
#define PNG_CHUNK_CRC_SIZE sizeof(uint32_t)

#define FILTER_BYTE_SIZE 1

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

int check_png_file_header(FILE* png_ptr)
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

   if(file_header != PNG_HEADER)
   {
      printf("File is not a PNG\n");
      return -1;
   }
  
   fseek(png_ptr, 0L, SEEK_END);
   long filesize = ftell(png_ptr);
   if(filesize < 0)
   {
      printf("Error determining file size\n");
      return -1;
   }
   fseek(png_ptr, sizeof(file_header), SEEK_SET);

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
   printf("Width:              %u\n", new_header->width);
   printf("Height:             %u\n", new_header->height);
   printf("Bit Depth:          %u\n", new_header->bit_depth);
   printf("Colour Type:        %u (", new_header->colour_type);
   switch(new_header->colour_type)
   {
      case 0 : printf("Greyscale"); break;
      case 2 : printf("Truecolour"); break;
      case 3 : printf("Indexed"); break;
      case 4 : printf("Greyscale Alpha"); break;
      case 6 : printf("Truecolour Alpha"); break;
      default : printf("Illegal"); break;
   }
   printf(")\nCompression Method: %u\n", new_header->compression_method);
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
   if(new_header->compression_method != PNG_COMPRESSION_TYPE_DEFLATE)
   {
      printf("Undefined compression method specified\n");
      return -1;
   }
   if(new_header->filter_method != PNG_ADAPTIVE_FILTERING)
   {
      printf("Undefined filter method specified\n");
      return -1;
   }
   if(new_header->interlace_method != PNG_INTERLACE_NONE && new_header->interlace_method != PNG_INTERLACE_ADAM7)
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
   if(check_png_file_header(png_ptr) != 0)
   {
      return -1;
   }

   struct png_header_t png_header;
   uint32_t chunk_data_size;
   uint32_t crc_check;

   fread(&chunk_data_size, sizeof(chunk_data_size), 1, png_ptr);
   fread(&png_header, sizeof(png_header), 1, png_ptr);

   if(check_png_header(chunk_data_size, &png_header, &crc_check) < 0)
   {
      printf("Check failed for png header\n");
      return -1;
   }

   uint32_t bits_per_pixel = png_header.bit_depth; // To map to 0-255 : 1 -> x255, 2 -> x85, 4 -> x17
   uint8_t stride = (png_header.bit_depth + 0x07) >> 3;
   uint32_t image_buffer_size = png_header.width * png_header.height;
   switch(png_header.colour_type)
   {
      case Truecolour:
         bits_per_pixel *= 3;
         image_buffer_size *= 3;
         stride *=3;
         break;
      case TruecolourAlpha:
         bits_per_pixel *= 4;
         image_buffer_size *= 4;
         stride *=4;
         break;
      case Indexed_colour:
         image_buffer_size *= 3;
         break;
      case GreyscaleAlpha:
         bits_per_pixel *= 2;
         image_buffer_size *= 2;
         stride *=2;
         break;
   }
   
   struct interlacing_t sub_images[7]; // 56 bytes   
   uint32_t buffer_size = 0;

   if(png_header.interlace_method == PNG_INTERLACE_ADAM7)
   {
      set_interlacing(&sub_images[0], png_header.width, png_header.height);

      for(int i=0; i<7; i++) { 
         uint32_t scanline_size = (((sub_images[i].width * bits_per_pixel) + 0x07) >> 3); // Bytes per scanline
         printf("\tPass %d: %dpx x %dpx (%d byte(s) per scanline)\n", i+1, sub_images[i].width, sub_images[i].height, scanline_size + FILTER_BYTE_SIZE);
         buffer_size += (scanline_size + FILTER_BYTE_SIZE) * sub_images[i].height;
      }
   }
   else
   {
      sub_images[0].width = png_header.width;
      sub_images[0].height = png_header.height;
      uint32_t scanline_size = (((sub_images[0].width * bits_per_pixel) + 0x07) >> 3);
      printf("\t%d byte(s) per scanline\n", scanline_size + FILTER_BYTE_SIZE);
      buffer_size = (scanline_size + FILTER_BYTE_SIZE) * sub_images[0].height;
   }

   uint8_t *chunk_buffer = malloc(PNG_CHUNK_LENGTH_SIZE);
   uint8_t *temp_buffer = malloc(buffer_size);
   struct rgb_t *palette_buffer = NULL;
   uint8_t *palette_alpha = NULL;
   uint8_t *image = malloc(image_buffer_size);
   uint8_t palette_size = 0;
   
   printf("Output image size: %d\nTemp buffer size: %d\nBits per pixel: %d\n", image_buffer_size, buffer_size, bits_per_pixel);

   enum chunk_states_t { IHDR_PROCESSED=0, PLTE_PROCESSED, READING_IDAT, IDAT_PROCESSED, EXIT_CHUNK_PROCESSING } chunk_state = IHDR_PROCESSED;

   while(fread(chunk_buffer, PNG_CHUNK_LENGTH_SIZE, 1, png_ptr) != 0 && feof(png_ptr) == 0 && chunk_state != EXIT_CHUNK_PROCESSING)
   {
      chunk_data_size = order_png32_t(*(uint32_t*)chunk_buffer);
      chunk_buffer = realloc(chunk_buffer, PNG_CHUNK_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE + chunk_data_size + PNG_CHUNK_CRC_SIZE);
      fread(chunk_buffer + PNG_CHUNK_LENGTH_SIZE, PNG_CHUNK_TYPE_SIZE + chunk_data_size + PNG_CHUNK_CRC_SIZE, 1, png_ptr);

      uint8_t *chunk_data = chunk_buffer + PNG_CHUNK_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE;
      uint32_t chunk_crc = *(uint32_t*)(chunk_data + chunk_data_size);
      crc_check = compute_crc(chunk_buffer + PNG_CHUNK_LENGTH_SIZE, chunk_data_size + PNG_CHUNK_TYPE_SIZE);
      if(crc_check != order_png32_t(chunk_crc))
      {
         printf("Error: CRC check failed for %c%c%c%c chunk\n", *(chunk_buffer + PNG_CHUNK_LENGTH_SIZE), *(chunk_buffer+PNG_CHUNK_LENGTH_SIZE+1), *(chunk_buffer+PNG_CHUNK_LENGTH_SIZE+2), *(chunk_buffer+PNG_CHUNK_LENGTH_SIZE+3));
         break;
      }
      else
      {
         uint32_t chunk_name = *(uint32_t*)(chunk_buffer + PNG_CHUNK_LENGTH_SIZE);
         switch(chunk_name)
         {
            case PNG_PLTE:
               if(palette_buffer != NULL)
               {
                  printf("Error: Critical chunk PLTE already defined");
                  break;  
               }
               if(chunk_state != IHDR_PROCESSED)
               {
                  printf("Error: Critical chunk PLTE out of order");
                  chunk_state = EXIT_CHUNK_PROCESSING;
                  break;
               }
               else
               {
                  // 1 byte R, G, B values
                  // Ignores bit depth
                  // Required for colour type 3
                  // Optional for colour types 2 and 6
                  if(png_header.colour_type == Greyscale || png_header.colour_type == GreyscaleAlpha)
                  {
                     printf("Error: Palette incompatible with specified colour type");
                     chunk_state = EXIT_CHUNK_PROCESSING;
                     break;    
                  }
                  if(chunk_data_size % 3 != 0)
                  {
                     printf("Error: Incorrect palette size");
                     chunk_state = EXIT_CHUNK_PROCESSING;
                     break;
                  }
                  printf("PLTE - %d bytes\n", chunk_data_size);
                  palette_buffer = malloc(chunk_data_size);
                  palette_size = chunk_data_size / 3;
                  memcpy(palette_buffer, chunk_data, chunk_data_size);
                  for(uint32_t i = 0; i < palette_size; i++)
                  {
                     printf("\t%d: %02x %02x %02x\n", i, palette_buffer[i].r, palette_buffer[i].g, palette_buffer[i].b);
                  }
                  chunk_state = PLTE_PROCESSED;
               }
               break;
            case PNG_IDAT:
               if(chunk_state > READING_IDAT)
               {
                  printf("Error: Non-consecutive IDAT chunk.");
                  chunk_state = EXIT_CHUNK_PROCESSING;
               }
               else
               {
                  static struct stream_ptr_t bitstream = { .bit_index = 0, .byte_index = 0 };
                  static uint8_t idat_buffer[PNG_CHUNK_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE] = {0};
                  static size_t idat_buffer_index = 0;

                  printf("IDAT - %d bytes\n", chunk_data_size);
                  memcpy(chunk_buffer, idat_buffer, sizeof(idat_buffer));
                  memcpy(idat_buffer, chunk_data + chunk_data_size - sizeof(idat_buffer), sizeof(idat_buffer));

                  bitstream.data = chunk_data - idat_buffer_index;
                  bitstream.size = chunk_data_size + idat_buffer_index;
                  bitstream.byte_index = 0;
                  while((bitstream.size - bitstream.byte_index) > (PNG_CHUNK_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE))
                  {
                     int status = decompress_zlib(&bitstream, temp_buffer);

                     if (status > 1)
                     {
                        printf("Fatal decompression error.\n");
                        break;
                     }
                     else if (status == ZLIB_COMPLETE)
                     {
                        break;
                     }
                  }
                  
                  idat_buffer_index = bitstream.size - bitstream.byte_index;
                  chunk_state = READING_IDAT;
               }
               break;
            case PNG_tRNS:
               if(png_header.colour_type == Indexed_colour)
               {
                  if(chunk_state != PLTE_PROCESSED)
                  {
                     printf("Error: Transparency is missing palette in Indexed colour mode.");
                     break;
                  }
                  if(chunk_data_size > palette_size)
                  {
                     printf("Error: Transparency values for Indexed colour mode exceed palette size.");
                     break;
                  }
                  palette_alpha = malloc(palette_size);
                  memcpy(palette_alpha, chunk_data, chunk_data_size);
                  for(uint8_t i = chunk_data_size; i < palette_size; i++)
                  {
                     palette_alpha[i] = 255;
                  }
               }
               else if(png_header.colour_type == Truecolour)
               {
                  if(chunk_data_size < 6)
                  {
                     printf("Error: Too few byte for Truecolour transparency.");
                     break;
                  }
                  if(chunk_data_size > 6)
                  {
                     printf("Error: Too many bytes for Truecolour transparency.");
                  }
                  palette_alpha = malloc(6);
                  memcpy(palette_alpha, chunk_data, 6);
               }
               else if(png_header.colour_type == Greyscale)
               {
                  if(chunk_data_size < 2)
                  {
                     printf("Error: Too few byte for Greyscale transparency.");
                     break;
                  }
                  if(chunk_data_size > 2)
                  {
                     printf("Error: Too many bytes for Greyscale transparency.");
                  }
                  palette_alpha = malloc(2);
                  memcpy(palette_alpha, chunk_data, 2);
               }
               else
               {
                  printf("Error: Colour mode already uses transparency.");
                  break;
               }
               break;
            case PNG_sBIT: // if(chunk_state < PLTE_PROCESSED)
            case PNG_cICP: // if(chunk_state < PLTE_PROCESSED)
            case PNG_cHRM: // if(chunk_state < PLTE_PROCESSED)    Overridden by sRGB or iCCP or cICP
            case PNG_gAMA: // if(chunk_state < PLTE_PROCESSED)    Overridden by sRGB or iCCP or cICP
            case PNG_iCCP: // if(chunk_state < PLTE_PROCESSED)    Overridden by cICP
            case PNG_sRGB: // if(chunk_state < PLTE_PROCESSED)    Overridden by iCCP, cICP
            case PNG_bKGD: // if(chunk_state == PLTE_PROCESSED)
            case PNG_hIST: // if(chunk_state == PLTE_PROCESSED)
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
               printf("Chunk %c%c%c%c not implemented\n", chunk_name & 0xff, (chunk_name>>8) & 0xff, (chunk_name>>16) & 0xff, (chunk_name>>24) & 0xff);
               break;
            case PNG_IEND:
               printf("IEND\n");
               chunk_state = EXIT_CHUNK_PROCESSING;
               if(png_header.colour_type == Indexed_colour && palette_buffer == NULL)
               {
                  printf("Error: No PLTE chunk present for Indexed colour type.");
                  break;
               }

               if(png_header.interlace_method == PNG_INTERLACE_ADAM7)
               {
                  int subindex = 0;
                  for(int image = 0; image < 7; image++)
                  {
                     printf("Sub-image %d:\n", image);
                     uint32_t w = 1 + sub_images[image].width * (bits_per_pixel >> 3) + (((sub_images[image].width * (bits_per_pixel % 8)) + 0x07) >> 3); // Bytes per scanline
                     printf("\tScanline: %d bytes, stride: %d\n", w, stride);
                     png_filter(temp_buffer + subindex, w, sub_images[image].height, stride);
                     // for(uint32_t j = 0; j < sub_images[image].height; j++)
                     // {
                     //    for(uint32_t i = 0; i < sub_images[image].width; i++)
                     //    {
                     //       printf("%02x ", temp_buffer[j*w+1+i]);
                     //    }
                     //    printf("\n");
                     // }
                     subindex += w * sub_images[image].height;
                  }
                  // De-interlace
               }
               else
               {
                  uint32_t w = 1 + png_header.width * (bits_per_pixel >> 3) + (((png_header.width * (bits_per_pixel % 8)) + 0x07) >> 3); // Bytes per scanline
                  printf("Scanline: %d bytes, stride: %d\n", w, stride);
                  // for(uint32_t j = 0; j < png_header.height; j++)
                  // {
                  //    for(uint32_t i = 0; i < png_header.width; i++)
                  //    {
                  //       printf("%02x ", temp_buffer[j*w+1+i]);
                  //    }
                  //    printf("\n");
                  // }
                  png_filter(temp_buffer, w, png_header.height, stride);
               }

               // FILE *fp = fopen("test_png_read.ppm", "wb"); /* b - binary mode */
               // (void) fprintf(fp, "P6\n%d %d\n255\n", png_header.width, png_header.height);
               // for (uint32_t j = 0; j < png_header.height; ++j)
               // {
               //    for (uint32_t i = 1; i <= png_header.width*3; ++i)
               //    {
               //       (void) fwrite(temp_buffer + ((j * png_header.width) + i), 1, 3, fp);
               //    }
               // }
               // (void) fclose(fp);

               break;
            case PNG_IHDR:
               printf("Error, multiple header chunks detected\n");
               chunk_state = EXIT_CHUNK_PROCESSING;
               break;
            default:
               printf("Unrecognised chunk %c%c%c%c\n", (char)(*(uint32_t*)chunk_buffer&0xFFFF), (char)((*(uint32_t*)chunk_buffer>>8)&0xFFFF), (char)((*(uint32_t*)chunk_buffer>>16)&0xFFFF), (char)(*(uint32_t*)chunk_buffer>>24));
               break;
         }
      }
   }

   free(chunk_buffer);
   free(palette_buffer);
   free(palette_alpha);
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