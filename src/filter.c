#include "filter.h"
#include "png_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void d1 (uint8_t input, uint8_t *output)
{
   for(int i=7; i>=0; i--)
   {
      output[i] = (input & 0x01) ? 0xff : 0x00;
      input >>= 1;
   }
}

void d2 (uint8_t input, uint8_t *output)
{
   uint8_t depth_2[4] = { 0x00, 0x55, 0xaa, 0xff };
   for(int i=3; i>=0; i--)
   {
      output[i] = depth_2[(input & 0x03)];
      input >>= 2;
   }
}

void d4 (uint8_t input, uint8_t *output)
{
   uint8_t depth_4[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
   output[1] = depth_4[input&0x0f];
   output[0] = depth_4[input >> 4];
}

void set_interlacing_mode_2(const struct png_header_t *png_header, struct sub_image_t *sub_images, const uint32_t bits_per_pixel)
{
   if(png_header->interlace_method == PNG_INTERLACE_NONE)
   {
      sub_images[0].scanline_width = png_header->width;
      sub_images[0].scanline_count = png_header->height;
      sub_images[0].scanline_size = FILTER_BYTE_SIZE + (((sub_images[0].scanline_width * bits_per_pixel) + 0x07) >> 3);
   }
   else
   {
      sub_images[0].scanline_width = (png_header->width + 7) >> 3;
      sub_images[0].scanline_count = (png_header->height + 7) >> 3;
      sub_images[1].scanline_width = (png_header->width + 3) >> 3;
      sub_images[1].scanline_count = sub_images->scanline_count;
      sub_images[2].scanline_width = (png_header->width + 3) >> 2;
      sub_images[2].scanline_count = (png_header->height + 3) >> 3;
      sub_images[3].scanline_width = (png_header->width + 1) >> 2;
      sub_images[3].scanline_count = (png_header->height + 3) >> 2;
      sub_images[4].scanline_width = (png_header->width + 1) >> 1;
      sub_images[4].scanline_count = (png_header->height + 1) >> 2;
      sub_images[5].scanline_width = png_header->width >> 1;
      sub_images[5].scanline_count = (png_header->height + 1) >> 1;
      sub_images[6].scanline_width = png_header->width;
      sub_images[6].scanline_count = png_header->height >> 1;
uint32_t debug[7];
      for(int i = 0; i < 7; i++)
      {
         debug[i] = sub_images[i].scanline_width;
         sub_images[i].scanline_size = FILTER_BYTE_SIZE + (((debug[i] * bits_per_pixel) + 0x07) >> 3);
         debug[i] = sub_images[i].scanline_size;
      }
   }
}

void set_interlacing(const struct png_header_t *png_header, struct sub_image_t *sub_images)
{
   sub_images[0].scanline_width = (png_header->width + 7) >> 3;
   sub_images[0].scanline_count = (png_header->height + 7) >> 3;
   sub_images[1].scanline_width = (png_header->width + 3) >> 3;
   sub_images[1].scanline_count = sub_images->scanline_count;
   sub_images[2].scanline_width = (png_header->width + 3) >> 2;
   sub_images[2].scanline_count = (png_header->height + 3) >> 3;
   sub_images[3].scanline_width = (png_header->width + 1) >> 2;
   sub_images[3].scanline_count = (png_header->height + 3) >> 2;
   sub_images[4].scanline_width = (png_header->width + 1) >> 1;
   sub_images[4].scanline_count = (png_header->height + 1) >> 2;
   sub_images[5].scanline_width = png_header->width >> 1;
   sub_images[5].scanline_count = (png_header->height + 1) >> 1;
   sub_images[6].scanline_width = png_header->width;
   sub_images[6].scanline_count = png_header->height >> 1;
}

uint8_t reconstruction_filter_type_0(uint8_t f, uint8_t a, uint8_t b, uint8_t c) {
   (void)a;
   (void)b;
   (void)c;
   return f;
}

uint8_t reconstruction_filter_type_1(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   (void)b;
   (void)c;
   return f+a;
}

uint8_t reconstruction_filter_type_2(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   (void)a;
   (void)c;
   return f+b;
}

uint8_t reconstruction_filter_type_3(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   (void)c;
   return f+((a+b)>>1);
}

uint8_t reconstruction_filter_type_4(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   int16_t p = a+b-c;
   int16_t pa = abs(p-a);
   int16_t pb = abs(p-b);
   int16_t pc = abs(p-c);
   return (pa<=pb) && (pa<=pc) ? f+a : (pb<=pc) ? f+b : f+c;
}

typedef uint8_t (*filter_t) (uint8_t, uint8_t, uint8_t, uint8_t);

static const filter_t reconstruct[5] = {
   reconstruction_filter_type_0,
   reconstruction_filter_type_1,
   reconstruction_filter_type_2,
   reconstruction_filter_type_3,
   reconstruction_filter_type_4
};

void png_filter(uint8_t *scanline, uint32_t scanline_width, uint32_t scanline_count, uint8_t stride)
{
   uint8_t a = 0;    // c b
   uint8_t b = 0;    // a x
   uint8_t c = 0;
   uint8_t filter_type = *scanline;
   uint32_t scanline_index = 1; // skip filter type

   if(filter_type > 4)
   {
      printf("Invalid filter type\n");
      scanline_index += scanline_width;
   }
   while(scanline_index <= stride)
   {
      *(scanline + scanline_index) = reconstruct[filter_type](*(scanline + scanline_index), a, b, c);
      scanline_index++;
   }
   while(scanline_index < scanline_width)
   {
      a = *(scanline + scanline_index - stride);
      *(scanline + scanline_index) = reconstruct[filter_type](*(scanline + scanline_index), a, b, c);
      scanline_index++;
   }
   for(uint32_t j = 1; j < scanline_count; j++)
   {
      a = 0;
      c = 0;
      scanline_index = j * scanline_width;
      filter_type = *(scanline + scanline_index);
      scanline_index++;
      if(filter_type > 4)
      {
         printf("Invalid filter type\n");
         scanline_index += scanline_width;
      }
      while (scanline_index <= j * scanline_width + stride)
      {
         b = *(scanline + scanline_index - scanline_width);
         *(scanline + scanline_index) = reconstruct[filter_type](*(scanline + scanline_index), a, b, c);
         scanline_index++;
      }
      while (scanline_index < (j + 1) * scanline_width)
      {
         a = *(scanline + scanline_index - stride);
         b = *(scanline + scanline_index - scanline_width);
         c = *(scanline + scanline_index - scanline_width - stride);
         *(scanline + scanline_index) = reconstruct[filter_type](*(scanline + scanline_index), a, b, c);
         scanline_index++;
      }
   }
}

void filter(uint8_t byte, void* payload)
{
   (void) byte;
   (void) payload;
   struct filter_settings_t *settings = (struct filter_settings_t *) payload;
   uint8_t offset = settings->scanline.stride - FILTER_BYTE_SIZE;

   settings->scanline.current[settings->scanline.index + offset] = byte;
   ++settings->scanline.index;

   if (settings->scanline.index == settings->images[settings->image_index].scanline_size)
   {
      settings->scanline.index = 0;

      uint8_t filter_type = settings->scanline.current[offset];
      memset(settings->scanline.current, 0, settings->scanline.stride);
      for(uint32_t i = 0; i < settings->images[settings->image_index].scanline_size - FILTER_BYTE_SIZE; i++)
      {
         settings->scanline.current[i + settings->scanline.stride] = reconstruct[filter_type](settings->scanline.current[i + settings->scanline.stride], settings->scanline.current[i], settings->scanline.last[i + settings->scanline.stride], settings->scanline.last[i]);
         printf("%02x ", settings->scanline.current[i + settings->scanline.stride]);
      } printf("\n");

      // Process bit depth, deinterlace and output

      uint8_t *temp = settings->scanline.current;
      settings->scanline.current = settings->scanline.last;
      settings->scanline.last = temp;
      
      ++settings->row_index;

      if (settings->row_index == settings->images[settings->image_index].scanline_count)
      {
         settings->row_index = 0;
         ++settings->image_index;
         memset(settings->scanline.last, 0, settings->images[settings->image_index].scanline_size + offset);
      }
   }
}