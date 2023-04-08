#include <stdint.h>

#define ZLIB_HEADER_SIZE 2
#define ADLER32_SIZE 4

enum zliberr_t { ZLIB_NO_ERR=0, ZLIB_FCHECK_FAIL, ZLIB_UNSUPPORTED_CM, ZLIB_INVALID_CM, ZLIB_INVALID_CINFO, ZLIB_UNSUPPORTED_FDICT };

struct zlib_header_t
{
   uint8_t CM : 4;
   uint8_t CINFO : 4;
   uint8_t FCHECK : 5;
   uint8_t FDICT : 1;
   uint8_t FLEVEL : 2;
};

enum zliberr_t zlib_header_check(const uint8_t* data);