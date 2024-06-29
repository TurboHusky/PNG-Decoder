#include "filter.h"
#include "png_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCANLINE_BUFFER_OFFSET -1

void d1(uint8_t input, uint8_t *output)
{
   for (int i = 7; i >= 0; i--)
   {
      output[i] = (input & 0x01) ? 0xff : 0x00;
      input >>= 1;
   }
}

void d2(uint8_t input, uint8_t *output)
{
   uint8_t depth_2[4] = {0x00, 0x55, 0xaa, 0xff};
   for (int i = 3; i >= 0; i--)
   {
      output[i] = depth_2[(input & 0x03)];
      input >>= 2;
   }
}

void d4(uint8_t input, uint8_t *output)
{
   uint8_t depth_4[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
   output[1] = depth_4[input & 0x0f];
   output[0] = depth_4[input >> 4];
}

void set_interlacing(const struct png_header_t *png_header, struct sub_image_t *sub_images, const uint32_t bits_per_pixel)
{
   if (png_header->interlace_method == PNG_INTERLACE_NONE)
   {
      sub_images[0].scanline_size = FILTER_BYTE_SIZE + (((png_header->width * bits_per_pixel) + 0x07) >> 3);
      sub_images[0].scanline_count = png_header->height;
      sub_images[0].px_offset = 0;
      sub_images[0].px_stride = 1;
      sub_images[0].row_offset = 0;
      sub_images[0].row_stride = 1;
      printf("\tNo interlacing, %d scanlines of size: %d\n", png_header->height, sub_images[0].scanline_size);
   }
   else
   {
      const uint8_t px_shift[7] = {3, 3, 2, 2, 1, 1, 0};
      const uint8_t px_spacing[7] = {7, 3, 3, 1, 1, 0, 0};
      const uint8_t row_shift[7] = {3, 3, 3, 2, 2, 1, 1};
      const uint8_t row_spacing[7] = {7, 7, 3, 3, 1, 1, 0};
      const uint8_t px_start[7] = {0, 4, 0, 2, 0, 1, 0};
      const uint8_t px_stride[7] = {8, 8, 4, 4, 2, 2, 1};
      const uint8_t row_start[7] = {0, 0, 4, 0, 2, 0, 1};
      const uint8_t row_stride[7] = {8, 8, 8, 4, 4, 2, 2};

      int index = 0;
      for (int i = 0; i < 7; i++)
      {
         uint32_t pixels_per_scanline = (png_header->width + px_spacing[i]) >> px_shift[i];
         uint32_t scanlines_per_subimage = (png_header->height + row_spacing[i]) >> row_shift[i];
         if ((pixels_per_scanline != 0) && (scanlines_per_subimage != 0))
         {
            sub_images[index].scanline_size = FILTER_BYTE_SIZE + (((pixels_per_scanline * bits_per_pixel) + 0x07) >> 3);
            sub_images[index].scanline_count = scanlines_per_subimage;
            sub_images[index].px_offset = px_start[i];
            sub_images[index].px_stride = px_stride[i];
            sub_images[index].row_offset = row_start[i];
            sub_images[index].row_stride = row_stride[i];
            printf("\tSubimage %d: %d scanlines of size: %d offset: %d\n", index + 1, sub_images[index].scanline_count, sub_images[index].scanline_size, sub_images[index].px_offset);
            index++;
         }
      }
   }
}

uint8_t reconstruction_filter_type_0(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   (void)a;
   (void)b;
   (void)c;
   return f;
}

uint8_t reconstruction_filter_type_1(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   (void)b;
   (void)c;
   return f + a;
}

uint8_t reconstruction_filter_type_2(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   (void)a;
   (void)c;
   return f + b;
}

uint8_t reconstruction_filter_type_3(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   (void)c;
   return f + ((a + b) >> 1);
}

uint8_t reconstruction_filter_type_4(uint8_t f, uint8_t a, uint8_t b, uint8_t c)
{
   int16_t p = a + b - c;
   int16_t pa = abs(p - a);
   int16_t pb = abs(p - b);
   int16_t pc = abs(p - c);
   return (pa <= pb) && (pa <= pc) ? f + a : (pb <= pc) ? f + b
                                                        : f + c;
}

typedef uint8_t (*filter_t)(uint8_t, uint8_t, uint8_t, uint8_t);

void filter(uint8_t byte, struct data_buffer_t *output_image, void *output_settings)
{
   struct output_settings_t *ptr = (struct output_settings_t *)output_settings;

   // Update scanline and output pointers
   if (ptr->scanline.index == ptr->subimage.images[ptr->subimage.image_index].scanline_size)
   {
      ptr->scanline.index = 0;

      uint8_t *temp = ptr->scanline.last;
      ptr->scanline.last = ptr->scanline.new;
      ptr->scanline.new = temp;

      ptr->subimage.row_index++;
      // printf("\n");
      if (ptr->subimage.row_index == ptr->subimage.images[ptr->subimage.image_index].scanline_count)
      {
         ptr->subimage.row_index = 0;
         ptr->subimage.image_index++;
         memset(ptr->scanline.buffer, 0, sizeof(uint8_t) * ptr->scanline.buffer_size);
         // printf("----\n");
      }

      ptr->pixel.index = 0;
      output_image->index = ((ptr->subimage.images[ptr->subimage.image_index].row_offset + ptr->subimage.row_index * ptr->subimage.images[ptr->subimage.image_index].row_stride) * ptr->image_width + ptr->subimage.images[ptr->subimage.image_index].px_offset) * ptr->pixel.size;
   }

   // Update filter type
   if (ptr->scanline.index == 0)
   {
      // printf("%d : ", byte);
      ptr->filter_type = byte;
      ptr->scanline.index++;
      return;
   }
   // printf("%02x ", byte);
   // Filter input and add to scanline buffer
   int idx1 = SCANLINE_BUFFER_OFFSET + ptr->scanline.index;
   int idx2 = idx1 - ptr->scanline.stride;
   uint8_t a = ptr->scanline.new[idx2];
   uint8_t b = ptr->scanline.last[idx1];
   uint8_t c = ptr->scanline.last[idx2];
   uint8_t f = byte;

   switch (ptr->filter_type)
   {
   case 0:
      ptr->scanline.new[idx1] = byte;
      break;
   case 1:
      ptr->scanline.new[idx1] = byte + a;
      break;
   case 2:
      ptr->scanline.new[idx1] = byte + b;
      break;
   case 3:
      ptr->scanline.new[idx1] = byte + ((a + b) >> 1);
      break;
   case 4:;
      int16_t p = a + b - c;
      int16_t pa = abs(p - (int16_t)a);
      int16_t pb = abs(p - (int16_t)b);
      int16_t pc = abs(p - (int16_t)c);
      p = (pa <= pb) && (pa <= pc) ? (f + a) & 0x00ff : (pb <= pc) ? (f + b) & 0x00ff
                                                                   : (f + c) & 0x00ff;
      ptr->scanline.new[idx1] = (uint8_t)p;
      break;
   }

   byte = ptr->scanline.new[idx1];
   // printf("%02x ", byte);

   // Handle bit depth
   uint8_t input_pixels = 1;
   uint8_t arr[8];
   uint8_t bit_depth_scale_factor = 1;
   switch (ptr->pixel.bit_depth)
   {
   case 1:
      arr[0] = (byte & 0x80) >> 7;
      arr[1] = (byte & 0x40) >> 6;
      arr[2] = (byte & 0x20) >> 5;
      arr[3] = (byte & 0x10) >> 4;
      arr[4] = (byte & 0x08) >> 3;
      arr[5] = (byte & 0x04) >> 2;
      arr[6] = (byte & 0x02) >> 1;
      arr[7] = byte & 0x01;
      input_pixels = 8;
      bit_depth_scale_factor = 0xff;
      break;
   case 2:
      arr[0] = (byte & 0xc0) >> 6;
      arr[1] = (byte & 0x30) >> 4;
      arr[2] = (byte & 0x0c) >> 2;
      arr[3] = (byte & 0x03);
      input_pixels = 4;
      bit_depth_scale_factor = 0x55;
      break;
   case 4:
      arr[0] = byte >> 4;
      arr[1] = byte & 0x0f;
      input_pixels = 2;
      bit_depth_scale_factor = 0x11;
      break;
   case 8:
   case 16:
      arr[0] = byte;
      break;
   }

   size_t max_output_index = (1 + ptr->subimage.images[ptr->subimage.image_index].row_offset + ptr->subimage.images[ptr->subimage.image_index].row_stride * ptr->subimage.row_index) * ptr->image_width * ptr->pixel.size;

   // Deinterlace filtered byte(s)
   int i = 0;
   while (i < input_pixels && output_image->index < max_output_index)
   {
      if (ptr->pixel.color_type == Indexed_colour)
      {
         output_image->data[output_image->index] = ptr->palette.buffer[arr[i]].r;
         output_image->data[output_image->index + 1] = ptr->palette.buffer[arr[i]].g;
         output_image->data[output_image->index + 2] = ptr->palette.buffer[arr[i]].b;
         if (ptr->palette.alpha != NULL) // Transparency
         {
            output_image->data[output_image->index + 3] = ptr->palette.alpha[arr[i]];
         }
         output_image->index += (ptr->subimage.images[ptr->subimage.image_index].px_stride) * ptr->pixel.size;
      }
      else
      {
         output_image->data[output_image->index] = arr[i];
         output_image->index++;
         ptr->pixel.index++;

         if (ptr->pixel.index == ptr->pixel.rgb_size && ptr->pixel.index < ptr->pixel.size) // Transparency
         {
            uint8_t alpha = (memcmp(output_image->data + output_image->index - ptr->pixel.rgb_size, ptr->palette.alpha, ptr->pixel.rgb_size)) ? 0xff : 0x00;
            if (ptr->pixel.bit_depth == 16)
            {
               output_image->data[output_image->index] = alpha;
               output_image->data[output_image->index + 1] = alpha;
               output_image->index += 2;
            }
            else
            {
               output_image->data[output_image->index] = alpha;
               output_image->index++;
            }
            ptr->pixel.index = ptr->pixel.size;
         }

         if (ptr->pixel.index == ptr->pixel.size)
         {
            for (int i = 1; i <= ptr->pixel.size; i++)
            {
               output_image->data[output_image->index - i] *= bit_depth_scale_factor;
            }
            output_image->index += (ptr->subimage.images[ptr->subimage.image_index].px_stride - 1) * ptr->pixel.size;
            ptr->pixel.index = 0;
         }
      }
      i++;
   }

   ptr->scanline.index++;
}