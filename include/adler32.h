#ifndef _ADLER32_
#define _ADLER32_

#include <stdint.h>
#include <stddef.h>

#define ADLER32_CHECKSUM_INIT 0x0001

union adler32_t
{
   uint32_t checksum;
   uint16_t vars[2];
};

static __inline__ void adler32_init(union adler32_t *adler)
{
   adler->checksum = ADLER32_CHECKSUM_INIT;
}

static __inline__ void adler32_update(union adler32_t *adler, uint8_t byte)
{
   adler->vars[0] = (adler->vars[0] + (uint16_t)byte) % 0xfff1;
   adler->vars[1] = (adler->vars[1] + adler->vars[0]) % 0xfff1;
}

#endif // _ADLER32_