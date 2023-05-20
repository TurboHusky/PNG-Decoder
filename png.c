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
   #error Endianess not defined
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
   for (int i = 31; i >= 0; i--)
   {
      if (i % 8 == 7)
      {
         printf(" ");
      }
      printf("%1d", (n & mask) >> i);
      mask >>= 1;
   }
   printf("\n");
}

void print_img_bytes(uint8_t *buf, uint32_t width, uint32_t height)
{
   for (uint32_t j = 0; j < height; j++)
   {
      for (uint32_t i = 0; i < width; i++)
      {
         printf("%02X ", *(buf + j * width + i));
      }
      printf("\n");
   }
}

enum colour_type_t
{
   Greyscale = 0,
   Truecolour = 2,
   Indexed_colour = 3,
   GreyscaleAlpha = 4,
   TruecolourAlpha = 6
};

int check_png_file_header(FILE *png_ptr)
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

   if (file_header != PNG_HEADER)
   {
      printf("File is not a PNG\n");
      return -1;
   }

   fseek(png_ptr, 0L, SEEK_END);
   long filesize = ftell(png_ptr);
   if (filesize < 0)
   {
      printf("Error determining file size\n");
      return -1;
   }
   fseek(png_ptr, sizeof(file_header), SEEK_SET);

   printf("File size: %ld\n", filesize);

   return 0;
}

int check_png_header(uint32_t header_length, struct png_header_t *new_header, uint32_t *crc)
{
   header_length = order_png32_t(header_length);

   if (header_length != sizeof(*new_header) - sizeof(new_header->crc) - sizeof(new_header->name))
   {
      printf("Error, invalid header size\n");
      return -1;
   }

   if (new_header->name != PNG_IHDR)
   {
      printf("Error, header chunk missing\n");
      return -1;
   }
   *crc = compute_crc((uint8_t *)new_header, 17);
   *crc = order_png32_t(*crc);
   if (*crc != new_header->crc)
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
   switch (new_header->colour_type)
   {
   case 0:
      printf("Greyscale");
      break;
   case 2:
      printf("Truecolour");
      break;
   case 3:
      printf("Indexed");
      break;
   case 4:
      printf("Greyscale Alpha");
      break;
   case 6:
      printf("Truecolour Alpha");
      break;
   default:
      printf("Illegal");
      break;
   }
   printf(")\nCompression Method: %u\n", new_header->compression_method);
   printf("Filter Method:      %u\n", new_header->filter_method);
   printf("Interlace Method:   %u\n", new_header->interlace_method);
   printf("\n");
   if (new_header->width == 0)
   {
      printf("Zero width detected\n");
      return -1;
   }
   if (new_header->height == 0)
   {
      printf("Zero height detected\n");
      return -1;
   }
   if (new_header->compression_method != PNG_COMPRESSION_TYPE_DEFLATE)
   {
      printf("Undefined compression method specified\n");
      return -1;
   }
   if (new_header->filter_method != PNG_ADAPTIVE_FILTERING)
   {
      printf("Undefined filter method specified\n");
      return -1;
   }
   if (new_header->interlace_method != PNG_INTERLACE_NONE && new_header->interlace_method != PNG_INTERLACE_ADAM7)
   {
      printf("Undefined interlace method specified\n");
      return -1;
   }

   if (new_header->colour_type == Truecolour)
   {
      if (!(new_header->bit_depth == 8 || new_header->bit_depth == 16))
      {
         printf("Illegal bit depth for colour type 2 (Truecolour)\n");
         return -1;
      }
   }
   else if (new_header->colour_type == TruecolourAlpha)
   {
      if (!(new_header->bit_depth == 8 || new_header->bit_depth == 16))
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
      if (!(new_header->bit_depth == 8 || new_header->bit_depth == 16 || new_header->bit_depth == 4 || new_header->bit_depth == 2 || new_header->bit_depth == 1))
      {
         printf("Illegal bit depth for colour type 0 (Greyscale)\n");
         return -1;
      }
   }
   else if (new_header->colour_type == GreyscaleAlpha)
   {
      if (!(new_header->bit_depth == 8 || new_header->bit_depth == 16))
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

int load_png(FILE *png_ptr)
{
   if (check_png_file_header(png_ptr) != 0)
   {
      return -1;
   }

   struct png_header_t png_header;
   uint32_t chunk_data_size;
   uint32_t crc_check;

   fread(&chunk_data_size, sizeof(chunk_data_size), 1, png_ptr);
   fread(&png_header, sizeof(png_header), 1, png_ptr);

   if (check_png_header(chunk_data_size, &png_header, &crc_check) < 0)
   {
      printf("Check failed for png header\n");
      return -1;
   }

   const uint8_t input_stride[7] = {1, 0, 3, 1, 2, 0, 4};
   const uint8_t output_stride[7] = {1, 0, 3, 3, 2, 0, 4};
   uint32_t bits_per_pixel = png_header.bit_depth * input_stride[png_header.colour_type];                    // Used to determine size of temp buffer

   struct sub_image_t sub_images[8]; // 56 bytes
   uint32_t decompressed_buffer_size = 0;

   if (png_header.interlace_method == PNG_INTERLACE_ADAM7)
   {
      set_interlacing(&png_header, &sub_images[0]);

      for (int i = 0; i < 7; i++)
      {
         sub_images[i].scanline_size = FILTER_BYTE_SIZE + (((sub_images[i].scanline_width * bits_per_pixel) + 0x07) >> 3); // Bytes per scanline, safe for ~0x03ffffff pixel width
         decompressed_buffer_size += sub_images[i].scanline_size * sub_images[i].scanline_count;
      }
   }
   else
   {
      sub_images[7].scanline_width = png_header.width;
      sub_images[7].scanline_count = png_header.height;
      sub_images[7].scanline_size = FILTER_BYTE_SIZE + (((sub_images[7].scanline_width * bits_per_pixel) + 0x07) >> 3);
      decompressed_buffer_size = sub_images[7].scanline_size * sub_images[7].scanline_count;
   }

   uint8_t *chunk_buffer = malloc(PNG_CHUNK_LENGTH_SIZE);
   uint8_t *decompressed_data = malloc(decompressed_buffer_size);
   struct rgb_t *palette_buffer = NULL;
   uint8_t *palette_alpha = NULL;
   uint32_t image_buffer_size = png_header.width * png_header.height * output_stride[png_header.colour_type]; // Used for output, need to check for separate alpha chunk (tRNS).
   uint8_t *image = malloc(image_buffer_size);
   uint8_t palette_size = 0;

   printf("Output image size: %d bytes\nTemp buffer size: %d bytes\nBits per pixel: %d\n\n", image_buffer_size, decompressed_buffer_size, bits_per_pixel);

   enum chunk_states_t
   {
      IHDR_PROCESSED = 0,
      PLTE_PROCESSED,
      READING_IDAT,
      IDAT_PROCESSED,
      EXIT_CHUNK_PROCESSING
   } chunk_state = IHDR_PROCESSED;

   while (fread(chunk_buffer, PNG_CHUNK_LENGTH_SIZE, 1, png_ptr) != 0 && feof(png_ptr) == 0 && chunk_state != EXIT_CHUNK_PROCESSING)
   {
      chunk_data_size = order_png32_t(*(uint32_t *)chunk_buffer);
      chunk_buffer = realloc(chunk_buffer, PNG_CHUNK_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE + chunk_data_size + PNG_CHUNK_CRC_SIZE);
      fread(chunk_buffer + PNG_CHUNK_LENGTH_SIZE, PNG_CHUNK_TYPE_SIZE + chunk_data_size + PNG_CHUNK_CRC_SIZE, 1, png_ptr);

      uint8_t *chunk_data = chunk_buffer + PNG_CHUNK_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE;
      uint32_t chunk_crc = *(uint32_t *)(chunk_data + chunk_data_size);
      crc_check = compute_crc(chunk_buffer + PNG_CHUNK_LENGTH_SIZE, chunk_data_size + PNG_CHUNK_TYPE_SIZE);
      if (crc_check != order_png32_t(chunk_crc))
      {
         printf("Error: CRC check failed for %c%c%c%c chunk\n", *(chunk_buffer + PNG_CHUNK_LENGTH_SIZE), *(chunk_buffer + PNG_CHUNK_LENGTH_SIZE + 1), *(chunk_buffer + PNG_CHUNK_LENGTH_SIZE + 2), *(chunk_buffer + PNG_CHUNK_LENGTH_SIZE + 3));
         break;
      }

      uint32_t chunk_name = *(uint32_t *)(chunk_buffer + PNG_CHUNK_LENGTH_SIZE);
      switch (chunk_name)
      {
      case PNG_PLTE:
         if (palette_buffer != NULL)
         {
            printf("Error: Critical chunk PLTE already defined");
            break;
         }
         if (chunk_state != IHDR_PROCESSED)
         {
            printf("Error: Critical chunk PLTE out of order");
            chunk_state = EXIT_CHUNK_PROCESSING;
            break;
         }

         // 1 byte R, G, B values
         // Ignores bit depth
         // Required for colour type 3
         // Optional for colour types 2 and 6
         if (png_header.colour_type == Greyscale || png_header.colour_type == GreyscaleAlpha)
         {
            printf("Error: Palette incompatible with specified colour type");
            chunk_state = EXIT_CHUNK_PROCESSING;
            break;
         }
         if (chunk_data_size % 3 != 0)
         {
            printf("Error: Incorrect palette size");
            chunk_state = EXIT_CHUNK_PROCESSING;
            break;
         }
         printf("PLTE - %d bytes\n", chunk_data_size);
         palette_buffer = malloc(chunk_data_size);
         palette_size = chunk_data_size / 3;
         memcpy(palette_buffer, chunk_data, chunk_data_size);
         for (uint32_t i = 0; i < palette_size; i++)
         {
            printf("\t%d: %02x %02x %02x\n", i, palette_buffer[i].r, palette_buffer[i].g, palette_buffer[i].b);
         }
         chunk_state = PLTE_PROCESSED;
         break;
      case PNG_IDAT:
         if (chunk_state > READING_IDAT)
         {
            printf("Error: Non-consecutive IDAT chunk.");
            chunk_state = EXIT_CHUNK_PROCESSING;
         }
         else
         {
            static struct stream_ptr_t bitstream = {.bit_index = 0, .byte_index = 0};
            static uint8_t idat_buffer[PNG_CHUNK_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE] = {0};
            static size_t idat_buffer_index = 0;

            printf("IDAT - %d bytes\n", chunk_data_size);
            memcpy(chunk_buffer, idat_buffer, sizeof(idat_buffer));
            memcpy(idat_buffer, chunk_data + chunk_data_size - sizeof(idat_buffer), sizeof(idat_buffer));

            bitstream.data = chunk_data - idat_buffer_index;
            bitstream.size = chunk_data_size + idat_buffer_index;
            bitstream.byte_index = 0;
            while ((bitstream.size - bitstream.byte_index) > (PNG_CHUNK_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE))
            {
               int status = decompress_zlib(&bitstream, decompressed_data);

               if (status == ZLIB_COMPLETE)
               {
                  break;
               }
               else if (status != ZLIB_BUSY)
               {
                  printf("Fatal decompression error.\n");
                  break;
               }
            }

            idat_buffer_index = bitstream.size - bitstream.byte_index;
            chunk_state = READING_IDAT;
         }
         break;
      case PNG_tRNS:
         if (png_header.colour_type == Indexed_colour)
         {
            if (chunk_state != PLTE_PROCESSED)
            {
               printf("Error: Transparency is missing palette in Indexed colour mode.");
               break;
            }
            if (chunk_data_size > palette_size)
            {
               printf("Error: Transparency values for Indexed colour mode exceed palette size.");
               break;
            }
            palette_alpha = malloc(palette_size);
            memcpy(palette_alpha, chunk_data, chunk_data_size);
            for (uint8_t i = chunk_data_size; i < palette_size; i++)
            {
               palette_alpha[i] = 255;
            }
         }
         else if (png_header.colour_type == Truecolour)
         {
            if (chunk_data_size < 6)
            {
               printf("Error: Too few bytes for Truecolour transparency.");
               break;
            }
            if (chunk_data_size > 6)
            {
               printf("Error: Too many bytes for Truecolour transparency.");
            }
            palette_alpha = malloc(6);
            memcpy(palette_alpha, chunk_data, 6);
         }
         else if (png_header.colour_type == Greyscale)
         {
            if (chunk_data_size < 2)
            {
               printf("Error: Too few bytes for Greyscale transparency.");
               break;
            }
            if (chunk_data_size > 2)
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
      case PNG_gAMA: // if(chunk_state < PLTE_PROCESSED)    Overridden by sRGB or iCCP or cICP
                     // if (chunk_state < PLTE_PROCESSED)
                     // {
                     //    if (chunk_data_size != 4)
                     //    {
                     //       printf("Error: Incorrect number of bytes for gamma.");
                     //       break;
                     //    }
                     //    gamma = order_png32_t(*(uint32_t*)chunk_data);
                     //    printf("Gamma %08x\n", gamma);
                     // }
                     // else
                     // {
                     //    printf("Error: gAMA chunk found at incorrect position.");
                     // }
                     // break;
      case PNG_sBIT: // if(chunk_state < PLTE_PROCESSED)
      case PNG_cICP: // if(chunk_state < PLTE_PROCESSED)
      case PNG_cHRM: // if(chunk_state < PLTE_PROCESSED)    Overridden by sRGB or iCCP or cICP
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
         printf("Chunk %c%c%c%c not implemented\n", chunk_name & 0xff, (chunk_name >> 8) & 0xff, (chunk_name >> 16) & 0xff, (chunk_name >> 24) & 0xff);
         break;
      case PNG_IEND:
         printf("IEND\n");
         chunk_state = EXIT_CHUNK_PROCESSING;

         if (png_header.colour_type == Indexed_colour && palette_buffer == NULL)
         {
            printf("Error: No PLTE chunk present for Indexed colour type.");
            break;
         }

         // Adam7 image mapping:
         const uint8_t width_offset[8] = {0, 4, 0, 2, 0, 1, 0, 0};
         const uint8_t width_spacing[8] = {8, 8, 4, 4, 2, 2, 1, 1};
         const uint8_t height_offset[8] = {0, 0, 4, 0, 2, 0, 1, 0};
         const uint8_t height_spacing[8] = {8, 8, 8, 4, 4, 2, 2, 1};
         const uint8_t filter_stride = ((png_header.bit_depth + 0x07) >> 3) * input_stride[png_header.colour_type]; // Used for deinterlacing, not required for decompression

         uint8_t sub_image_index = 7;

         if (png_header.interlace_method == PNG_INTERLACE_ADAM7)
         {
            sub_image_index = 0;
         }
         int decompressed_data_index = 0;
         do // Each subimage
         {
            if (sub_images[sub_image_index].scanline_width == 0 || sub_images[sub_image_index].scanline_count == 0)
            {
               break;
            }
            size_t output_x = width_offset[sub_image_index] * output_stride[png_header.colour_type];
            size_t output_y = height_offset[sub_image_index] * png_header.width * output_stride[png_header.colour_type];
            // TODO: Account for tRNS chunk

            // printf("\tPass %d: %dpx x %dpx (%d byte(s) per scanline)\n", sub_image_index + 1, sub_images[sub_image_index].scanline_width, sub_images[sub_image_index].scanline_count, sub_images[sub_image_index].scanline_size);
            png_filter(decompressed_data + decompressed_data_index, sub_images[sub_image_index].scanline_size, sub_images[sub_image_index].scanline_count, filter_stride);

            for (uint32_t j = 0; j < sub_images[sub_image_index].scanline_count; j++) // Each scanline
            {
               // printf("\t\t");
               size_t output_index = output_x + output_y;
               decompressed_data_index += FILTER_BYTE_SIZE;
               uint8_t bit_index = 0;
               uint8_t val;

               for (uint32_t i = 0; i < sub_images[sub_image_index].scanline_width; i++) // Each pixel
               {
                  switch (png_header.bit_depth)
                  {
                  case 1:
                     val = (decompressed_data[decompressed_data_index] >> (7 - bit_index)) & 0x01;
                     bit_index++;
                     switch (png_header.colour_type)
                     {
                     case Greyscale:
                        image[output_index] = val * 0xff;
                        // printf("%02x ", image[output_index]);
                        output_index += width_spacing[sub_image_index];
                        break;
                     case Indexed_colour:
                        image[output_index] = palette_buffer[val].r;
                        image[output_index + 1] = palette_buffer[val].g;
                        image[output_index + 2] = palette_buffer[val].b;
                        // printf("%02x%02x%02x ", image[output_index], image[output_index + 1], image[output_index + 2]);
                        output_index += (width_spacing[sub_image_index] * 3);
                        break;
                     }
                     decompressed_data_index += bit_index >> 3;
                     bit_index &= 0x07;
                     break;
                  case 2:
                     val = (decompressed_data[decompressed_data_index] >> (6 - bit_index)) & 0x03;
                     bit_index += 2;
                     switch (png_header.colour_type)
                     {
                     case Greyscale:
                        image[output_index] = val * 0x55;
                        // printf("%02x ", image[output_index]);
                        output_index += width_spacing[sub_image_index];
                        break;
                     case Indexed_colour:
                        image[output_index] = palette_buffer[val].r;
                        image[output_index + 1] = palette_buffer[val].g;
                        image[output_index + 2] = palette_buffer[val].b;
                        // printf("%02x%02x%02x ", image[output_index], image[output_index + 1], image[output_index + 2]);
                        output_index += width_spacing[sub_image_index] * 3;
                        break;
                     }
                     decompressed_data_index += bit_index >> 3;
                     bit_index &= 0x07;
                     break;
                  case 4:
                     val = (decompressed_data[decompressed_data_index] >> (4 - bit_index)) & 0x0f;
                     bit_index += 4;
                     switch (png_header.colour_type)
                     {
                     case Greyscale:
                        image[output_index] = val * 0x11;
                        // printf("%02x ", image[output_index]);
                        output_index += width_spacing[sub_image_index];
                        break;
                     case Indexed_colour:
                        image[output_index] = palette_buffer[val].r;
                        image[output_index + 1] = palette_buffer[val].g;
                        image[output_index + 2] = palette_buffer[val].b;
                        // printf("%02x%02x%02x ", image[output_index], image[output_index + 1], image[output_index + 2]);
                        output_index += width_spacing[sub_image_index] * 3;
                        break;
                     }
                     decompressed_data_index += bit_index >> 3;
                     bit_index &= 0x07;
                     break;
                  case 8:
                     switch (png_header.colour_type)
                     {
                     case Greyscale:
                        image[output_index] = decompressed_data[decompressed_data_index];
                        // printf("%02x ", image[output_index]);
                        decompressed_data_index++;
                        output_index += width_spacing[sub_image_index];
                        break;
                     case Truecolour:
                        image[output_index] = decompressed_data[decompressed_data_index];
                        image[output_index + 1] = decompressed_data[decompressed_data_index + 1];
                        image[output_index + 2] = decompressed_data[decompressed_data_index + 2];
                        // printf("%02x%02x%02x ", image[output_index], image[output_index + 1], image[output_index + 2]);
                        decompressed_data_index += 3;
                        output_index += 3 * width_spacing[sub_image_index];
                        break;
                     case Indexed_colour:
                        image[output_index] = palette_buffer[decompressed_data[decompressed_data_index]].r;
                        image[output_index + 1] = palette_buffer[decompressed_data[decompressed_data_index]].g;
                        image[output_index + 2] = palette_buffer[decompressed_data[decompressed_data_index]].b;
                        // printf("%02x%02x%02x ", image[output_index], image[output_index + 1], image[output_index + 2]);
                        decompressed_data_index++;
                        output_index += 3 * width_spacing[sub_image_index];
                        break;
                     case GreyscaleAlpha:
                        image[output_index] = decompressed_data[decompressed_data_index];
                        image[output_index + 1] = decompressed_data[decompressed_data_index + 1];
                        // printf("%02x:%02x ", image[output_index], image[output_index + 1]);
                        decompressed_data_index += 2;
                        output_index += 2 * width_spacing[sub_image_index];
                        break;
                     case TruecolourAlpha:
                        image[output_index] = decompressed_data[decompressed_data_index];
                        image[output_index + 1] = decompressed_data[decompressed_data_index + 1];
                        image[output_index + 2] = decompressed_data[decompressed_data_index + 2];
                        image[output_index + 3] = decompressed_data[decompressed_data_index + 3];
                        // printf("%02x%02x%02x:%02x ", image[output_index], image[output_index + 1], image[output_index + 2], image[output_index + 3]);
                        decompressed_data_index += 4;
                        output_index += 4 * width_spacing[sub_image_index];
                        break;
                     }
                     break;
                  case 16:
                     switch (png_header.colour_type)
                     {
                     case Greyscale:
                        image[output_index] = decompressed_data[decompressed_data_index];
                        image[output_index + 1] = decompressed_data[decompressed_data_index + 1];
                        decompressed_data_index += 2;
                        output_index += 2 * width_spacing[sub_image_index];
                        // printf("%02x%02x ", image[output_index], image[output_index + 1]);
                        break;
                     case Truecolour:
                        image[output_index] = decompressed_data[decompressed_data_index];
                        image[output_index + 1] = decompressed_data[decompressed_data_index + 1];
                        image[output_index + 2] = decompressed_data[decompressed_data_index + 2];
                        image[output_index + 3] = decompressed_data[decompressed_data_index + 3];
                        image[output_index + 4] = decompressed_data[decompressed_data_index + 4];
                        image[output_index + 5] = decompressed_data[decompressed_data_index + 5];
                        decompressed_data_index += 6;
                        output_index += 6 * width_spacing[sub_image_index];
                        // printf("%02x%02x%02x%02x%02x%02x ", image[output_index], image[output_index + 1], image[output_index + 2], image[output_index + 3], image[output_index + 4], image[output_index + 5]);
                        break;
                     case GreyscaleAlpha:
                        image[output_index] = decompressed_data[decompressed_data_index];
                        image[output_index + 1] = decompressed_data[decompressed_data_index + 1];
                        image[output_index + 2] = decompressed_data[decompressed_data_index + 2];
                        image[output_index + 3] = decompressed_data[decompressed_data_index + 3];
                        decompressed_data_index += 4;
                        output_index += 4 * width_spacing[sub_image_index];
                        // printf("%02x%02x:%02x%02x ", image[output_index], image[output_index + 1], image[output_index + 2], image[output_index + 3]);
                        break;
                     case TruecolourAlpha:
                        image[output_index] = decompressed_data[decompressed_data_index];
                        image[output_index + 1] = decompressed_data[decompressed_data_index + 1];
                        image[output_index + 2] = decompressed_data[decompressed_data_index + 2];
                        image[output_index + 3] = decompressed_data[decompressed_data_index + 3];
                        image[output_index + 4] = decompressed_data[decompressed_data_index + 4];
                        image[output_index + 5] = decompressed_data[decompressed_data_index + 5];
                        image[output_index + 6] = decompressed_data[decompressed_data_index + 6];
                        image[output_index + 7] = decompressed_data[decompressed_data_index + 7];
                        decompressed_data_index += 8;
                        output_index += 8 * width_spacing[sub_image_index];
                        // printf("%02x%02x%02x%02x%02x%02x:%02x%02x ", image[output_index], image[output_index + 1], image[output_index + 2], image[output_index + 3], image[output_index + 4], image[output_index + 5], image[output_index + 6], image[output_index + 7]);
                        break;
                     }
                     break;
                  }
               } 
               // printf("%llu, %llu -> %llu\n", output_x, output_y, output_index);
               decompressed_data_index += (bit_index + 0x07) >> 3;
               output_y += height_spacing[sub_image_index] * png_header.width * output_stride[png_header.colour_type];
            } // End of scanline
            sub_image_index++;
         } while (sub_image_index < 7);

         // for(uint32_t z = 0; z < image_buffer_size; z++) {printf("%02x ", image[z]);} printf("\n");

         // for(uint32_t y = 0; y < png_header.height; y++)
         // {
         //    for(uint32_t x = 0; x < png_header.width * output_stride[png_header.colour_type]; x++)
         //    {
         //       printf("%02x ", image[x + y * png_header.width]);
         //    }
         //    printf("\n");
         // }

         FILE *fp = fopen("png_decoder_test.ppm", "wb"); /* b - binary mode */
         (void) fprintf(fp, "P6\n%d %d\n255\n", png_header.width, png_header.height);
         switch(png_header.colour_type)
         {
            case Greyscale:
               for(uint32_t i = 0; i < image_buffer_size; i++)
               {
                  uint8_t px[3] = { image[i], image[i], image[i] };
                  (void) fwrite(px, 3, 1, fp);
               }
               break;
            case Truecolour:
            case Indexed_colour:
               (void) fwrite(image, 1, image_buffer_size, fp); // ppm requires rgb for each pixel
               break;
            case GreyscaleAlpha:
               for(uint32_t i = 0; i < image_buffer_size; i += 2)
               {
                  uint8_t px[3] = { image[i], image[i], image[i] };
                  (void) fwrite(px, 3, 1, fp);
               }            
               break;
            case TruecolourAlpha:
               for(uint32_t i = 0; i < image_buffer_size; i += 4)
               {
                  (void) fwrite(image + i, 1, 1, fp);
                  (void) fwrite(image + i + 1, 1, 1, fp);
                  (void) fwrite(image + i + 2, 1, 1, fp);
               }
               break;
         }
         (void) fclose(fp);

         break;
      case PNG_IHDR:
         printf("Error, multiple header chunks detected\n");
         chunk_state = EXIT_CHUNK_PROCESSING;
         break;
      default:
         printf("Unrecognised chunk %c%c%c%c\n", (char)(*(uint32_t *)chunk_buffer & 0xFFFF), (char)((*(uint32_t *)chunk_buffer >> 8) & 0xFFFF), (char)((*(uint32_t *)chunk_buffer >> 16) & 0xFFFF), (char)(*(uint32_t *)chunk_buffer >> 24));
         break;
      }
   }

   free(chunk_buffer);
   free(palette_buffer);
   free(palette_alpha);
   free(image);
   free(decompressed_data);
   return 0;
}

int main(int argc, char *argv[])
{
   (void)argv[argc - 1];
   printf("OS: %s\n", OS_TARGET);

   FILE *png_file = fopen(argv[1], "rb");
   load_png(png_file);
   fclose(png_file);

   return 0;
}