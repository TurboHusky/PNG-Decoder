#ifndef _ADLER32_
#define _ADLER32_

#include <stdint.h>
#include <stddef.h>

struct adler32_t {
   uint16_t a, b;
};

void adler32_update(struct adler32_t* checksum, uint8_t byte)
{
   checksum->a = (checksum->a + (uint16_t) byte) % 0xFFF1;
   checksum->b = (checksum->b + checksum->a) % 0xFFF1;
}

uint32_t adler32_result(struct adler32_t* checksum)
{
   return ((uint32_t) checksum->b) << 16 | checksum->a;
}

uint32_t adler32(uint8_t *buf, size_t len)
{
   uint16_t a = 1;
   uint16_t b = 0;

   for(size_t i=0; i<len; i++)
   {
      a = (a + (uint16_t) *(buf+i)) % 0xFFF1;
      b = (b + a) % 0xFFF1;
   }

   return ((uint32_t) b) << 16 | a;
}

#endif // _ADLER32_