#include "png.h"
#include "png_utils.h"
#include "crc.h"
#include "zlib.h"
#include "filter.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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

const char *const greyscale_name = "Greyscale";
const char *const truecolour_name = "Truecolour";
const char *const indexed_name = "Indexed";
const char *const greyscale_alpha_name = "Greyscale Alpha";
const char *const truecolour_alpha_name = "Truecolour Alpha";
const char *const illegal_name = "Illegal colour type";

void debug_image(const struct image_t *image)
{
   for (uint32_t y = 0; y < image->height; y++)
   {
      for (uint32_t x = 0; x < image->width * image->mode; x++)
      {
         printf("%02x ", image->data[(y * image->width * image->mode) + x]);
      }
      printf("\n");
   }
}

int check_png_file_header(FILE *png_ptr)
{
   if (png_ptr == NULL)
   {
      log_error("Failed to open PNG file");
      fflush(stdout);
      return -1;
   }

   uint64_t file_header;
   if (fread(&file_header, sizeof(file_header), 1, png_ptr) != 1)
   {
      log_error("Failed to read png header");
      fflush(stdout);
      return -1;
   }

   if (file_header != PNG_HEADER)
   {
      log_error("File is not a PNG");
      fflush(stdout);
      return -1;
   }

   fseek(png_ptr, 0L, SEEK_END);
   long filesize = ftell(png_ptr); // Not POSIX compliant. stat for Linux, GetFileSize for Win.
   if (filesize < 0)
   {
      log_error("Error determining file size");
      fflush(stdout);
      return -1;
   }
   fseek(png_ptr, sizeof(file_header), SEEK_SET);

   log_debug("File size: %ld bytes", filesize);

   return 0;
}

int check_png_header(uint32_t header_length, struct png_header_t *new_header, uint32_t *crc)
{
   header_length = order_png32_t(header_length);

   if (header_length != sizeof(*new_header) - sizeof(new_header->crc) - sizeof(new_header->name))
   {
      log_error("Error, invalid header size");
      fflush(stdout);
      return -1;
   }

   if (new_header->name != PNG_IHDR)
   {
      log_error("Error, header chunk missing");
      fflush(stdout);
      return -1;
   }
   *crc = compute_crc((uint8_t *)new_header, 17);
   *crc = order_png32_t(*crc);
   if (*crc != new_header->crc)
   {
      log_error("CRC check failed - Header corrupt");
      fflush(stdout);
      return -1;
   }

   new_header->width = order_png32_t(new_header->width);
   new_header->height = order_png32_t(new_header->height);
   const char *const* colour_type;
   switch (new_header->colour_type)
   {
   case Greyscale:
      colour_type = &greyscale_name;
      break;
   case Truecolour:
      colour_type = &truecolour_name;
      break;
   case Indexed_colour:
      colour_type = &indexed_name;
      break;
   case GreyscaleAlpha:
      colour_type = &greyscale_alpha_name;
      break;
   case TruecolourAlpha:
      colour_type = &truecolour_alpha_name;
      break;
   default:
      colour_type = &illegal_name;
      break;
   }
   log_info("Width:              %u", new_header->width);
   log_info("Height:             %u", new_header->height);
   log_info("Bit Depth:          %u", new_header->bit_depth);
   log_info("Colour Type:        %u (%s)", new_header->colour_type, *colour_type);
   log_debug("Compression Method: %u", new_header->compression_method);
   log_debug("Filter Method:      %u", new_header->filter_method);
   log_debug("Interlace Method:   %u", new_header->interlace_method);

   if (new_header->width == 0)
   {
      log_error("Zero width detected");
      fflush(stdout);
      return -1;
   }
   if (new_header->height == 0)
   {
      log_error("Zero height detected");
      fflush(stdout);
      return -1;
   }
   if (new_header->compression_method != PNG_COMPRESSION_TYPE_DEFLATE)
   {
      log_error("Undefined compression method specified");
      fflush(stdout);
      return -1;
   }
   if (new_header->filter_method != PNG_ADAPTIVE_FILTERING)
   {
      log_error("Undefined filter method specified");
      fflush(stdout);
      return -1;
   }
   if (new_header->interlace_method != PNG_INTERLACE_NONE && new_header->interlace_method != PNG_INTERLACE_ADAM7)
   {
      log_error("Undefined interlace method specified");
      fflush(stdout);
      return -1;
   }

   if (new_header->colour_type == Truecolour)
   {
      if (!(new_header->bit_depth == 8 || new_header->bit_depth == 16))
      {
         log_error("Illegal bit depth for colour type 2 (Truecolour)");
         fflush(stdout);
         return -1;
      }
   }
   else if (new_header->colour_type == TruecolourAlpha)
   {
      if (!(new_header->bit_depth == 8 || new_header->bit_depth == 16))
      {
         log_error("Illegal bit depth for colour type 6 (Truecolour + Alpha)");
         fflush(stdout);
         return -1;
      }
   }
   else if (new_header->colour_type == Indexed_colour)
   {
      if (!(new_header->bit_depth == 8 || new_header->bit_depth == 4 || new_header->bit_depth == 2 || new_header->bit_depth == 1))
      {
         log_error("Illegal bit depth for colour type 3 (Indexed)");
         fflush(stdout);
         return -1;
      }
   }
   else if (new_header->colour_type == Greyscale)
   {
      if (!(new_header->bit_depth == 8 || new_header->bit_depth == 16 || new_header->bit_depth == 4 || new_header->bit_depth == 2 || new_header->bit_depth == 1))
      {
         log_error("Illegal bit depth for colour type 0 (Greyscale)");
         fflush(stdout);
         return -1;
      }
   }
   else if (new_header->colour_type == GreyscaleAlpha)
   {
      if (!(new_header->bit_depth == 8 || new_header->bit_depth == 16))
      {
         log_error("Illegal bit depth for colour type 4 (Greyscale + Alpha)");
         fflush(stdout);
         return -1;
      }
   }
   else
   {
      log_error("Illegal colour type");
      fflush(stdout);
      return -1;
   }

   return 0;
}

int load_png(const char *filename, struct image_t *output)
{
   output->mode = INVALID;

   FILE *png_ptr = fopen(filename, "rb");

   if (check_png_file_header(png_ptr) != 0)
   {
      fclose(png_ptr);
      return -1;
   }

   struct png_header_t png_header;
   uint32_t chunk_data_size;
   uint32_t crc_check;

   fread(&chunk_data_size, sizeof(chunk_data_size), 1, png_ptr);
   fread(&png_header, sizeof(png_header), 1, png_ptr);

   if (check_png_header(chunk_data_size, &png_header, &crc_check) < 0)
   {
      log_error("Check failed for png header");
      fflush(stdout);
      fclose(png_ptr);
      return -1;
   }

   output->width = png_header.width;
   output->height = png_header.height;
   output->bit_depth = png_header.bit_depth == 16 ? 16 : 8;
   output->mode = (png_header.colour_type == Greyscale) ? G : (png_header.colour_type == GreyscaleAlpha) ? GA
                                                          : (png_header.colour_type == TruecolourAlpha)  ? RGBA
                                                                                                         : RGB;

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
       .image_width = png_header.width};

   set_interlacing(&png_header, bits_per_pixel, output_settings.subimage.images);

   output_settings.scanline.stride = (bits_per_pixel + 0x07) >> 3;
   const uint32_t scanline_pixel_byte_count = (png_header.width * bits_per_pixel + 0x07) >> 3;
   const uint32_t scanline_buffer_size = (output_settings.scanline.stride + scanline_pixel_byte_count);
   log_debug("\tScanline buffer size: %u", scanline_buffer_size * 2);
   uint8_t *scanline_buffers = calloc(scanline_buffer_size * 2, sizeof(uint8_t));
   output_settings.scanline.buffer = scanline_buffers;
   output_settings.scanline.buffer_size = scanline_buffer_size * 2;
   output_settings.scanline.new = scanline_buffers + output_settings.scanline.stride;
   output_settings.scanline.last = output_settings.scanline.new + scanline_buffer_size;
   output_settings.scanline.index = 0;

   output->size = palette_scale * bytes_per_pixel[png_header.colour_type] * png_header.width * png_header.height;
   if (png_header.bit_depth == 16)
   {
      output->size <<= 1;
   }

   uint8_t *chunk_buffer = malloc(PNG_CHUNK_LENGTH_SIZE);
   struct zlib_t zlib_idat = {
       .state = READING_ZLIB_HEADER,
       .LZ77_buffer.data = malloc(ZLIB_BUFFER_MAX_SIZE),
       .bytes_read = 0};
       adler32_init(&zlib_idat.adler32);

   output->data = malloc(output->size);

   struct data_buffer_t image =
       {
           .data = output->data,
           .index = 0};

   int zlib_status = ZLIB_INCOMPLETE;

   log_debug("Output image size: %d bytes", output->size);
   log_debug("\tBits per pixel: %d", bits_per_pixel);
   log_debug("\tOutput pixel size: %d", output_settings.pixel.size);

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
         log_error("Error: CRC check failed for %c%c%c%c chunk", *(chunk_buffer + PNG_CHUNK_LENGTH_SIZE), *(chunk_buffer + PNG_CHUNK_LENGTH_SIZE + 1), *(chunk_buffer + PNG_CHUNK_LENGTH_SIZE + 2), *(chunk_buffer + PNG_CHUNK_LENGTH_SIZE + 3));
         fflush(stdout);
         break;
      }

      uint32_t chunk_name = *(uint32_t *)(chunk_buffer + PNG_CHUNK_LENGTH_SIZE);
      switch (chunk_name)
      {
      case PNG_PLTE:
         log_debug("PLTE - %d bytes", chunk_data_size);

         if (output_settings.palette.buffer != NULL)
         {
            log_error("Error: Critical chunk PLTE already defined");
            fflush(stdout);
            break;
         }
         if (chunk_state != IHDR_PROCESSED)
         {
            log_error("Error: Critical chunk PLTE out of order");
            fflush(stdout);
            chunk_state = EXIT_CHUNK_PROCESSING;
            break;
         }

         // 1 byte R, G, B values
         // Ignores bit depth
         // Required for colour type 3
         // Optional for colour types 2 and 6
         if (png_header.colour_type == Greyscale || png_header.colour_type == GreyscaleAlpha)
         {
            log_error("Error: Palette incompatible with specified colour type");
            fflush(stdout);
            chunk_state = EXIT_CHUNK_PROCESSING;
            break;
         }
         if (chunk_data_size % 3 != 0)
         {
            log_error("Error: Incorrect palette size");
            fflush(stdout);
            chunk_state = EXIT_CHUNK_PROCESSING;
            break;
         }
         output_settings.palette.buffer = malloc(chunk_data_size);
         output_settings.palette.size = chunk_data_size / 3;
         memcpy(output_settings.palette.buffer, chunk_data, chunk_data_size);
         chunk_state = PLTE_PROCESSED;
         break;
      case PNG_tRNS:
         log_debug("tRNS");
         if (png_header.colour_type == TruecolourAlpha || png_header.colour_type == GreyscaleAlpha)
         {
            log_error("Error: Colour mode already uses transparency.");
            fflush(stdout);
            break;
         }

         if (png_header.colour_type == Indexed_colour)
         {
            if (chunk_state != PLTE_PROCESSED)
            {
               log_error("Error: Transparency is missing palette in Indexed colour mode.");
               fflush(stdout);
               break;
            }
            if (chunk_data_size > output_settings.palette.size)
            {
               log_error("Error: Transparency values for Indexed colour mode exceed palette size.");
               fflush(stdout);
               break;
            }
            output->mode = RGBA;
         }
         else if (png_header.colour_type == Truecolour)
         {
            if (chunk_data_size != 6)
            {
               log_error("Error: Incorrect number of bytes read for Truecolour transparency. Read %u, expect 6.", chunk_data_size);
               fflush(stdout);
               break;
            }
            output->mode = RGBA;
         }
         else if (png_header.colour_type == Greyscale)
         {
            if (chunk_data_size != 2)
            {
               log_error("Error: Incorrect number of bytes read for Greyscale transparency. Read %u, expect 2.", chunk_data_size);
               fflush(stdout);
               break;
            }
            output->mode = GA;
         }

         uint8_t alpha_size = (png_header.bit_depth == 16) ? 2 : 1;
         output->size += (png_header.width * png_header.height * alpha_size);
         log_debug("\tResizing output image to %d bytes", output->size);
         output->data = realloc(output->data, output->size);
         image.data = output->data;

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
            log_error("Error: Non-consecutive IDAT chunk.");
            fflush(stdout);
            chunk_state = EXIT_CHUNK_PROCESSING;
         }
         else
         {
            chunk_state = READING_IDAT;

            static struct stream_ptr_t bitstream = {.bit_index = 0, .byte_index = 0};
            static uint8_t idat_buffer[PNG_CHUNK_LENGTH_SIZE + PNG_CHUNK_TYPE_SIZE] = {0};
            static size_t idat_buffer_index = 0;

            log_debug("IDAT - %d bytes", chunk_data_size);
            memcpy(chunk_buffer, idat_buffer, sizeof(idat_buffer));
            memcpy(idat_buffer, chunk_data + chunk_data_size - sizeof(idat_buffer), sizeof(idat_buffer));

            bitstream.data = chunk_data - idat_buffer_index;
            bitstream.size = chunk_data_size + idat_buffer_index;
            bitstream.byte_index = 0;

            zlib_status = decompress_zlib(&zlib_idat, &bitstream, &image, filter, (void *)&output_settings);

            idat_buffer_index = bitstream.size - bitstream.byte_index;
         }
         break;

      case PNG_gAMA: // if(chunk_state < PLTE_PROCESSED)    Overridden by sRGB or iCCP or cICP
                     // if (chunk_state < PLTE_PROCESSED)
                     // {
                     //    if (chunk_data_size != 4)
                     //    {
                     //       log_error("Error: Incorrect number of bytes for gamma.");
                     //       break;
                     //    }
                     //    gamma = order_png32_t(*(uint32_t*)chunk_data);
                     //    log_debug("Gamma %08x", gamma);
                     // }
                     // else
                     // {
                     //    log_error("Error: gAMA chunk found at incorrect position.");
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
         log_warning("Chunk %c%c%c%c not implemented", chunk_name & 0xff, (chunk_name >> 8) & 0xff, (chunk_name >> 16) & 0xff, (chunk_name >> 24) & 0xff);
         break;
      case PNG_IEND:
         log_debug("IEND");
         chunk_state = EXIT_CHUNK_PROCESSING;

         if (zlib_status != ZLIB_COMPLETE)
         {
            log_error("Error: Missing IDAT chunk.");
            fflush(stdout);
            break;
         }

         if (png_header.colour_type == Indexed_colour && output_settings.palette.buffer == NULL)
         {
            log_error("Error: No PLTE chunk present for Indexed colour type.");
            fflush(stdout);
            break;
         }
         break;
      case PNG_IHDR:
         log_error("Error, multiple header chunks detected");
         fflush(stdout);
         chunk_state = EXIT_CHUNK_PROCESSING;
         break;
      default:
         log_warning("Unrecognised chunk %c%c%c%c", (char)(*(uint32_t *)chunk_buffer & 0xFFFF), (char)((*(uint32_t *)chunk_buffer >> 8) & 0xFFFF), (char)((*(uint32_t *)chunk_buffer >> 16) & 0xFFFF), (char)(*(uint32_t *)chunk_buffer >> 24));
         fflush(stdout);
         break;
      }
   }

   fclose(png_ptr);

   free(chunk_buffer);
   free(zlib_idat.LZ77_buffer.data);
   free(scanline_buffers);
   free(output_settings.palette.buffer);
   free(output_settings.palette.alpha);

   return 0;
}

void close_png(struct image_t *image)
{
   free(image->data);
}