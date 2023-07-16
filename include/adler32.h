#ifndef _ADLER32_
#define _ADLER32_

#include <stdint.h>
#include <stddef.h>

union adler32_t {
   uint32_t checksum;
   uint16_t vars[2];
};

static __inline__ void adler32_update(union adler32_t* adler, uint8_t byte)
{
   adler->vars[0] = (adler->vars[0] + (uint16_t) byte) % 0xfff1;
   adler->vars[1] = (adler->vars[1] + adler->vars[0]) % 0xfff1;
}

static __inline__ uint32_t compute_adler32(uint8_t *buf, size_t len)
{
   union adler32_t temp = { .checksum = 1 };

   for(size_t i=0; i<len; i++)
   {
      adler32_update(&temp, buf[i]);
   }

   return temp.checksum;
}

#endif // _ADLER32_