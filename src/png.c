#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "png_utils.h"
#include "crc.h"
#include "zlib.h"
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
#define PNG_CHUNK_LENGTH_SIZE sizeof(uint32_t)
#define PNG_CHUNK_TYPE_SIZE sizeof(uint32_t)
#define PNG_CHUNK_CRC_SIZE sizeof(uint32_t)

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
   case Greyscale:
      printf("Greyscale");
      break;
   case Truecolour:
      printf("Truecolour");
      break;
   case Indexed_colour:
      printf("Indexed");
      break;
   case GreyscaleAlpha:
      printf("Greyscale Alpha");
      break;
   case TruecolourAlpha:
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

   const uint8_t bytes_per_pixel[7] = {1, 0, 3, 1, 2, 0, 4};
   const uint32_t bits_per_pixel = png_header.bit_depth * bytes_per_pixel[png_header.colour_type];
   const uint8_t palette_scale = (png_header.colour_type == Indexed_colour) ? 3 : 1;

   struct output_settings_t output_settings = {
       .pixel.rgb_size = palette_scale * bytes_per_pixel[png_header.colour_type] * ((png_header.bit_depth + 0x07) >> 3),
       .pixel.size = palette_scale * bytes_per_pixel[png_header.colour_type] * ((png_header.bit_depth + 0x07) >> 3),
       .pixel.index = 0,
       .pixel.bit_depth = png_header.bit_depth,
       .pixel.color_type = png_header.colour_type,
       .subimage.image_index = 0,
       .subimage.row_index = 0,
       .palette.buffer = NULL,
       .palette.alpha = NULL,
       .palette.size = 0,
       .image_width = png_header.width,
       .scanline.stride = bits_per_pixel >> 3,
       .scanline.index = 0};

   set_interlacing(&png_header, output_settings.subimage.images, bits_per_pixel);

   const uint32_t scanline_buffer_size = (((png_header.width + FILTER_BYTE_SIZE) * bits_per_pixel) + 0x07) >> 3;
   uint8_t *scanline_buffer = calloc(scanline_buffer_size << 1, sizeof(uint8_t));
   output_settings.scanline.buffer = scanline_buffer;
   output_settings.scanline.buffer_size = scanline_buffer_size << 1;
   output_settings.scanline.new = scanline_buffer + ((bits_per_pixel + 0x07) >> 3) - 1;
   output_settings.scanline.last = output_settings.scanline.new + scanline_buffer_size;
   output_settings.scanline.stride = (bits_per_pixel + 0x07) >> 3;

   uint32_t image_size = palette_scale * bytes_per_pixel[png_header.colour_type] * png_header.width * png_header.height;
   if (png_header.bit_depth == 16)
   {
      image_size <<= 1;
   }

   uint8_t *chunk_buffer = malloc(PNG_CHUNK_LENGTH_SIZE);
   struct zlib_t zlib_idat = {
       .state = READING_ZLIB_HEADER,
       .LZ77_buffer.data = malloc(ZLIB_BUFFER_MAX_SIZE),
       .adler32.checksum = 1,
       .bytes_read = 0};
   struct data_buffer_t image = {
       .data = malloc(image_size),
       .index = 0};

   int zlib_status = ZLIB_INCOMPLETE;

   printf("Output image size: %d bytes\n\tBits per pixel: %d\n\tOutput pixel size: %d\n", image_size, bits_per_pixel, output_settings.pixel.size);

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
         printf("PLTE - %d bytes\n", chunk_data_size);

         if (output_settings.palette.buffer != NULL)
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
         output_settings.palette.buffer = malloc(chunk_data_size);
         output_settings.palette.size = chunk_data_size / 3;
         memcpy(output_settings.palette.buffer, chunk_data, chunk_data_size);
         // for (uint32_t i = 0; i < output_settings.palette.size; i++)
         // {
         //    printf("\t%d: %02x %02x %02x\n", i, output_settings.palette.buffer[i].r, output_settings.palette.buffer[i].g, output_settings.palette.buffer[i].b);
         // }
         chunk_state = PLTE_PROCESSED;
         break;
      case PNG_tRNS:
         printf("tRNS\n");
         if (png_header.colour_type == TruecolourAlpha || png_header.colour_type == GreyscaleAlpha)
         {
            printf("Error: Colour mode already uses transparency.");
            break;
         }

         if (png_header.colour_type == Indexed_colour)
         {
            if (chunk_state != PLTE_PROCESSED)
            {
               printf("Error: Transparency is missing palette in Indexed colour mode.");
               break;
            }
            if (chunk_data_size > output_settings.palette.size)
            {
               printf("Error: Transparency values for Indexed colour mode exceed palette size.");
               break;
            }
         }
         else if (png_header.colour_type == Truecolour)
         {
            if (chunk_data_size != 6)
            {
               printf("Error: Incorrect number of bytes read for Truecolour transparency. Read %u, expect 6.", chunk_data_size);
               break;
            }
         }
         else if (png_header.colour_type == Greyscale)
         {
            if (chunk_data_size != 2)
            {
               printf("Error: Incorrect number of bytes read for Greyscale transparency. Read %u, expect 2.", chunk_data_size);
               break;
            }
         }

         uint8_t alpha_size = (png_header.bit_depth == 16) ? 2 : 1;
         image_size += (png_header.width * png_header.height * alpha_size);
         printf("\tResizing output image to %d bytes\n", image_size);
         image.data = realloc(image.data, image_size);
         output_settings.pixel.size += alpha_size;

         output_settings.palette.alpha = malloc(output_settings.palette.size);
         memcpy(output_settings.palette.alpha, chunk_data, chunk_data_size);

         if (png_header.colour_type != Indexed_colour && alpha_size == 1)
         {
            for (uint32_t i = 0; i < chunk_data_size; i += 2)
            {
               output_settings.palette.alpha[i >> 1] = output_settings.palette.alpha[i + 1];
            }
            break;
         }

         for (uint8_t i = chunk_data_size; i < output_settings.palette.size; i++)
         {
            output_settings.palette.alpha[i] = 255;
         }
         break;
      case PNG_IDAT:
         if (chunk_state > READING_IDAT)
         {
            printf("Error: Non-consecutive IDAT chunk.");
            chunk_state = EXIT_CHUNK_PROCESSING;
         }
         else
         {
            chunk_state = READING_IDAT;

            static struct stream_ptr_t bitstream = {.bit_index = 0, .byte_index = 0};
            static uint8_t idat_buffer[PNG_CHUNK_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE] = {0};
            static size_t idat_buffer_index = 0;

            printf("IDAT - %d bytes\n", chunk_data_size);
            memcpy(chunk_buffer, idat_buffer, sizeof(idat_buffer));
            memcpy(idat_buffer, chunk_data + chunk_data_size - sizeof(idat_buffer), sizeof(idat_buffer));

            bitstream.data = chunk_data - idat_buffer_index;
            bitstream.size = chunk_data_size + idat_buffer_index;
            bitstream.byte_index = 0;

            zlib_status = decompress_zlib(&zlib_idat, &bitstream, &image, filter, (void *)&output_settings);

            idat_buffer_index = bitstream.size - bitstream.byte_index;

            // for(uint32_t y = 0; y < png_header.height; y++)
            // {
            //    for(uint32_t x = 0; x < png_header.width * output_settings.pixel.size; x++)
            //    {
            //       printf("%02x ", image.data[(y*png_header.width*output_settings.pixel.size)+x]);
            //    }
            //    printf("\n");
            // }
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

         if (zlib_status != ZLIB_COMPLETE)
         {
            printf("Error: Missing IDAT chunk.");
            break;
         }

         if (png_header.colour_type == Indexed_colour && output_settings.palette.buffer == NULL)
         {
            printf("Error: No PLTE chunk present for Indexed colour type.");
            break;
         }

         FILE *fp = fopen("png_decoder_test.ppm", "wb"); /* b - binary mode */
         (void)fprintf(fp, "P6\n%d %d\n255\n", png_header.width, png_header.height);

         FILE *fp_alpha = fopen("png_decoder_test_alpha.ppm", "wb");
         (void)fprintf(fp_alpha, "P6\n%d %d\n255\n", png_header.width, png_header.height);

         uint32_t inc = png_header.bit_depth == 16 ? 2 : 1;
         switch (png_header.colour_type)
         {
         case Greyscale:
            for (uint32_t i = 0; i < image_size; i += inc)
            {
               uint8_t px[3] = {image.data[i], image.data[i], image.data[i]};
               (void)fwrite(px, 3, 1, fp);
               if (output_settings.palette.alpha != NULL)
               {
                  uint8_t px[3] = {image.data[i + 3 * inc], image.data[i + 3 * inc], image.data[i + 3 * inc]};
                  (void)fwrite(px, 3, 1, fp_alpha);
                  i += inc;
               }
            }
            break;
         case Truecolour:
            for (uint32_t i = 0; i < image_size; i += 3 * inc)
            {
               (void)fwrite(image.data + i, 1, 1, fp);
               (void)fwrite(image.data + i + inc, 1, 1, fp);
               (void)fwrite(image.data + i + 2 * inc, 1, 1, fp);
               if (output_settings.palette.alpha != NULL)
               {
                  uint8_t px[3] = {image.data[i + 3 * inc], image.data[i + 3 * inc], image.data[i + 3 * inc]};
                  (void)fwrite(px, 3, 1, fp_alpha);
                  i += inc;
               }
            }
            break;
         case Indexed_colour:
            for (uint32_t i = 0; i < image_size; i += 3 * inc)
            {
               (void)fwrite(image.data + i, 1, 1, fp);
               (void)fwrite(image.data + i + inc, 1, 1, fp);
               (void)fwrite(image.data + i + 2 * inc, 1, 1, fp);
               if (output_settings.palette.alpha != NULL)
               {
                  uint8_t px[3] = {image.data[i + 3 * inc], image.data[i + 3 * inc], image.data[i + 3 * inc]};
                  (void)fwrite(px, 3, 1, fp_alpha);
                  i += inc;
               }
            }
            break;
         case GreyscaleAlpha:
            for (uint32_t i = 0; i < image_size; i += 2 * inc)
            {
               uint8_t px[3] = {image.data[i], image.data[i], image.data[i]};
               (void)fwrite(px, 3, 1, fp);
               uint8_t px_alpha[3] = {image.data[i + inc], image.data[i + inc], image.data[i + inc]};
               (void)fwrite(px_alpha, 3, 1, fp_alpha);
            }
            break;
         case TruecolourAlpha:
            for (uint32_t i = 0; i < image_size; i += 4 * inc)
            {
               (void)fwrite(image.data + i, 1, 1, fp);
               (void)fwrite(image.data + i + inc, 1, 1, fp);
               (void)fwrite(image.data + i + 2 * inc, 1, 1, fp);
               uint8_t px_alpha[3] = {image.data[i + 3 * inc], image.data[i + 3 * inc], image.data[i + 3 * inc]};
               (void)fwrite(px_alpha, 3, 1, fp_alpha);
            }
            break;
         }
         (void)fclose(fp);
         (void)fclose(fp_alpha);

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
   free(zlib_idat.LZ77_buffer.data);
   free(scanline_buffer);
   free(output_settings.palette.buffer);
   free(output_settings.palette.alpha);
   free(image.data);
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