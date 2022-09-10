#ifndef _ADLER32_
#define _ADLER32_

#include <stdint.h>

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