#ifndef _PNG_UTILS_
#define _PNG_UTILS_

#include <stddef.h>
#include <stdint.h>

static __inline__ uint32_t order_png32_t(uint32_t value)
{
   #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      return (value & 0x000000FF) << 24 | (value & 0x0000FF00) << 8 | (value & 0x00FF0000) >> 8 | (value & 0xFF000000) >> 24;
   #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      #error Endianess not supported
   #else
      # error, endianess not recognised
   #endif
}

struct ring_buffer_t
{
   uint16_t mask;
   uint16_t index;
   uint8_t *data;
};

struct stream_ptr_t
{
   const uint8_t *data;
   size_t size;
   size_t byte_index;
   uint8_t bit_index;
};

struct data_buffer_t {
   uint8_t *data;
   size_t index;
};

static __inline__ void increment_ring_buffer(struct ring_buffer_t *buf)
{
   buf->index = (buf->index + 1) & buf->mask;
}

static __inline__ void decrement_ring_buffer(struct ring_buffer_t *buf)
{
   buf->index = (buf->index - 1) & buf->mask;
}

static __inline__ void stream_add_bits(struct stream_ptr_t *ptr, uint8_t bits)
{
   ptr->bit_index += bits;
   ptr->byte_index += ptr->bit_index >> 3;
   ptr->bit_index &= 0x07;
}

static __inline__ void stream_remove_bits(struct stream_ptr_t *ptr, uint8_t bits)
{
   ptr->byte_index -= (bits - ptr->bit_index + 0x07) >> 3;
   ptr->bit_index = (ptr->bit_index - bits) & 0x07;
}

struct rgb_t
{
   uint8_t r;
   uint8_t g;
   uint8_t b;
};

union dbuf 
{
   uint8_t byte;
   uint8_t u8[4];
   uint16_t u16[2];
   uint32_t u32;
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

#endif