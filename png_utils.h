#ifndef _PNG_UTILS_
#define _PNG_UTILS_

#include <stdint.h>

static inline uint32_t order_png32_t(uint32_t value)
{
   #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      return (value & 0x000000FF) << 24 | (value & 0x0000FF00) << 8 | (value & 0x00FF0000) >> 8 | (value & 0xFF000000) >> 24;
   #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      #error Endianess not supported
   #else
      # error, endianess not defined
   #endif
}

union dbuf 
{
   uint8_t byte;
   uint8_t buffer[4];
   uint16_t split[2];
   uint32_t stream;
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