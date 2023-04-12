#ifndef _ADLER32_
#define _ADLER32_

#include <stdint.h>

struct adler32_t {
   uint16_t a, b;
};

void adler32_update(struct adler32_t* checksum, uint8_t byte)
{
   checksum->a = (checksum->a + (uint16_t) byte) % 65521;
   checksum->b = (checksum->b + checksum->a) % 65521;
}

uint32_t adler32_result(struct adler32_t* checksum)
{
   return ((uint32_t) checksum->b) << 16 | checksum->a;
}

uint32_t adler32(uint8_t *buf, int len)
{
   uint16_t a = 1;
   uint16_t b = 0;

   for(int i=0; i<len; i++)
   {
      a = (a + (uint16_t) *(buf+i)) % 65521;
      b = (b + a) % 65521;
   }

   return ((uint32_t) b) << 16 | a;
}

#endif // _ADLER32_