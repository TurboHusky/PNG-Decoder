#ifndef _CRC_
#define _CRC_

#include <stdint.h>
#include <stdio.h>

#define CRC32_INITIAL 0xffffffffL
#define CRC32_POLYNOMIAL 0xedb88320L

uint32_t crc_table[256];
int crc_table_computed = 0; // TODO: Remove global.

void make_crc_table(void)
{
   uint32_t crc;
   int n, k;

   for (n = 0; n < 256; n++)
   {
      crc = (uint32_t)n;
      for (k = 0; k < 8; k++)
      {
         if (crc & 1)
         {
            crc = CRC32_POLYNOMIAL ^ (crc >> 1);
         }
         else
         {
            crc = crc >> 1;
         }
      }
      crc_table[n] = crc;
   }
   crc_table_computed = 1;
}

uint32_t update_crc(const uint32_t crc, const uint8_t *buf, const int len)
{
   uint32_t crc_out = crc;

   if (!crc_table_computed)
   {
      make_crc_table();
   }
   for (int n = 0; n < len; n++)
   {
      crc_out = crc_table[(crc_out ^ buf[n]) & 0xff] ^ (crc_out >> 8);
   }
   return crc_out;
}

uint32_t compute_crc(const uint8_t *buf, const int len)
{
   return update_crc(CRC32_INITIAL, buf, len) ^ CRC32_INITIAL;
}

#endif // _CRC_