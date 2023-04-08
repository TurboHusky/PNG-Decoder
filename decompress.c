 #include "decompress.h"

#include <stdio.h>

enum zliberr_t zlib_header_check(const uint8_t* data)
 {
    struct zlib_header_t zlib_header = *(struct zlib_header_t *)data;
    uint16_t FCHECK_RESULT = (((*data) << 8) | (*(data+1))) % 31;

    printf("\tCINFO: %02X\n\tCM: %02X\n\tFLEVEL: %02X\n\tFDICT: %02X\n\tFCHECK: %02X (%d)\n", zlib_header.CINFO, zlib_header.CM, zlib_header.FLEVEL, zlib_header.FDICT, *(data+1) & 0x1f, FCHECK_RESULT);

    if(zlib_header.CM != 8)
    {
        if(zlib_header.CM == 15)
        {
            printf("zlib error: Unsupported compression method specified in header\n");
            return ZLIB_UNSUPPORTED_CM;
        }
        else
        {
            printf("zlib error: Invalid compression method specified in header\n");
            return ZLIB_INVALID_CM;
        }
    }
    if(zlib_header.CINFO > 7)
    {
        printf("zlib error: Invalid window size specified in header\n");
        return ZLIB_INVALID_CINFO;
    }
    if(zlib_header.FDICT)
    {
        printf("zlib error: Dictionary cannot be specified for PNG in header\n");
        return ZLIB_UNSUPPORTED_FDICT;
    }
    if(FCHECK_RESULT)
    {
        printf("zlib error: FCHECK failed\n");
        return ZLIB_FCHECK_FAIL;
    }
    
    return ZLIB_NO_ERR;
}