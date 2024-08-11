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
#error, endianess not recognised
#endif
}

enum colour_type_t
{
   Greyscale = 0,
   Truecolour = 2,
   Indexed_colour = 3,
   GreyscaleAlpha = 4,
   TruecolourAlpha = 6
};

struct vector_1x3_t
{
   float data[3];
};

struct matrix_3x3_t
{
   float data[3][3];
};

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

struct data_buffer_t
{
   uint8_t *data;
   size_t index;
};

static __inline__ struct matrix_3x3_t inverse_3x3(const struct matrix_3x3_t *m)
{
   float determinant = m->data[0][0] * (m->data[1][1] * m->data[2][2] - m->data[1][2] * m->data[2][1]);
   determinant -= m->data[0][1] * (m->data[1][0] * m->data[2][2] - m->data[1][2] * m->data[2][0]);
   determinant += m->data[0][2] * (m->data[1][0] * m->data[2][1] - m->data[1][1] * m->data[2][0]);

   struct matrix_3x3_t result;
   // Divide cofactor transpose by determinant
   result.data[0][0] =  (m->data[1][1] * m->data[2][2] - m->data[1][2] * m->data[2][1]) / determinant;
   result.data[0][1] = -(m->data[0][1] * m->data[2][2] - m->data[2][1] * m->data[0][2]) / determinant;
   result.data[0][2] =  (m->data[0][1] * m->data[1][2] - m->data[1][1] * m->data[0][2]) / determinant;
   result.data[1][0] = -(m->data[1][0] * m->data[2][2] - m->data[1][2] * m->data[2][0]) / determinant;
   result.data[1][1] =  (m->data[0][0] * m->data[2][2] - m->data[0][2] * m->data[2][0]) / determinant;
   result.data[1][2] = -(m->data[0][0] * m->data[1][2] - m->data[0][2] * m->data[1][0]) / determinant;
   result.data[2][0] =  (m->data[1][0] * m->data[2][1] - m->data[1][1] * m->data[2][0]) / determinant;
   result.data[2][1] = -(m->data[0][0] * m->data[2][1] - m->data[0][1] * m->data[2][0]) / determinant;
   result.data[2][2] =  (m->data[0][0] * m->data[1][1] - m->data[0][1] * m->data[1][0]) / determinant;
   return result;
}

static __inline__ struct vector_1x3_t transform_1x3(const struct matrix_3x3_t *m, const struct vector_1x3_t *v)
{
   struct vector_1x3_t result;
   result.data[0] = m->data[0][0] * v->data[0] + m->data[0][1] * v->data[1] + m->data[0][2] * v->data[2];
   result.data[1] = m->data[1][0] * v->data[0] + m->data[1][1] * v->data[1] + m->data[1][2] * v->data[2];
   result.data[2] = m->data[2][0] * v->data[0] + m->data[2][1] * v->data[1] + m->data[2][2] * v->data[2];
   return result;
}

static __inline__ void increment_ring_buffer(struct ring_buffer_t *buf)
{
   buf->index = (buf->index + 1) & buf->mask;
}

static __inline__ void decrement_ring_buffer(struct ring_buffer_t *buf)
{
   buf->index = (buf->index - 1) & buf->mask;
}

static __inline__ void stream_ptr_add(struct stream_ptr_t *ptr, uint8_t bits)
{
   ptr->bit_index += bits;
   ptr->byte_index += ptr->bit_index >> 3;
   ptr->bit_index &= 0x07;
}

static __inline__ void stream_ptr_subtract(struct stream_ptr_t *ptr, uint8_t bits)
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