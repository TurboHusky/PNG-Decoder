#ifndef _MD5_GENERATOR_
#define _MD5_GENERATOR_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

union md5_t
{
    uint32_t raw[4];
    uint8_t out[16];
};

#define BITS_PER_BLOCK 512
#define BITS_PER_FINAL_BLOCK 448
#define BYTES_PER_BLOCK 64
#define UINT32_PER_BLOCK 16

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MD5_A_INIT 0x67452301
#define MD5_B_INIT 0xefcdab89
#define MD5_C_INIT 0x98badcfe
#define MD5_D_INIT 0x10325476
#define SHIFT_MASK 0x00000003
#define INPUT_MASK 0x0000000f

const uint32_t T[64] = {0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
                        0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
                        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
                        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
                        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
                        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
                        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
                        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
                        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
                        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
                        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
                        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
                        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
                        0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
                        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
                        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#error Endianess not supported
#else
#error Endianess not defined
#endif

const int shift_A[4] = {7, 12, 17, 22};
const int shift_B[4] = {5, 9, 14, 20};
const int shift_C[4] = {4, 11, 16, 23};
const int shift_D[4] = {6, 10, 15, 21};


union md5_t md5_hash(const uint8_t *data, const size_t bitcount)
{
    size_t data_index = 0;
    uint64_t padding = bitcount % BITS_PER_BLOCK;
    padding = (padding < BITS_PER_FINAL_BLOCK) ? BITS_PER_FINAL_BLOCK - padding : (BITS_PER_BLOCK - padding) + BITS_PER_FINAL_BLOCK;

    uint32_t A = MD5_A_INIT;
    uint32_t B = MD5_B_INIT;
    uint32_t C = MD5_C_INIT;
    uint32_t D = MD5_D_INIT;

    size_t bytes = bitcount >> 3;
    uint32_t N = ((bitcount + padding) >> 3) + sizeof(uint64_t);
    for (int b = 0; b < N; b += BYTES_PER_BLOCK)
    {
        uint32_t word_buffer[UINT32_PER_BLOCK];
        int buffer_index = 0;
        uint8_t *buff_ptr = (uint8_t *)word_buffer;

        while (buffer_index < BYTES_PER_BLOCK && data_index < bytes)
        {
            buff_ptr[buffer_index] = data[data_index];
            ++buffer_index;
            ++data_index;
        }

        // padding
        if (buffer_index < BYTES_PER_BLOCK)
        {
            if (data_index == bytes)
            {
                uint8_t bits = bitcount & 0x07;
                if (bits != 0)
                {
                    --buffer_index;
                }
                buff_ptr[buffer_index] &= 0xff << 8 - bits;
                buff_ptr[buffer_index] |= 0x80 >> bits;
                ++buffer_index;
                ++data_index;
            }
            while (buffer_index < BYTES_PER_BLOCK && data_index < N - sizeof(uint64_t))
            {
                buff_ptr[buffer_index] = 0;
                ++buffer_index;
                ++data_index;
            }
            if (buffer_index < BYTES_PER_BLOCK)
            {
                *(uint64_t *)(buff_ptr + BYTES_PER_BLOCK - sizeof(uint64_t)) = (uint64_t)bitcount;
            }
        }

        // hash algorithm
        uint32_t a0 = A;
        uint32_t b0 = B;
        uint32_t c0 = C;
        uint32_t d0 = D;
        for (int i = 0; i < BYTES_PER_BLOCK; ++i)
        {
            uint32_t k;
            uint32_t s;
            uint32_t f;
            if (i < 16)
            {
                f = (B & C) | (~B & D);
                s = shift_A[i & SHIFT_MASK];
                k = i;
            }
            else if (i < 32)
            {
                f = (B & D) | (C & ~D);
                s = shift_B[i & SHIFT_MASK];
                k = ((5 * i) + 1) & INPUT_MASK;
            }
            else if (i < 48)
            {
                f = B ^ C ^ D;
                s = shift_C[i & SHIFT_MASK];
                k = ((3 * i) + 5) & INPUT_MASK;
            }
            else
            {
                f = C ^ (B | ~D);
                s = shift_D[i & SHIFT_MASK];
                k = (7 * i) & INPUT_MASK;
            }
            uint32_t temp = (A + f + word_buffer[k] + T[i]);
            A = D;
            D = C;
            C = B;
            B += (temp << s) | (temp >> ((sizeof(temp) * 8) - s));
        }
        A += a0;
        B += b0;
        C += c0;
        D += d0;
    }
    union md5_t out = {A, B, C, D};
    return out;
}

void md5_test_suite()
{
    // Test cases
    const char *a = "";
    printf("%s\n", a);
    union md5_t r = md5_hash(a, 0*8);
    printf("\t%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n", r.out[0], r.out[1], r.out[2], r.out[3], r.out[4], r.out[5], r.out[6], r.out[7], r.out[8], r.out[9], r.out[10], r.out[11], r.out[12], r.out[13], r.out[14], r.out[15]);
    printf("\td41d8cd9 8f00b204 e9800998 ecf8427e expected\n");
    const char *b = "a";
    printf("%s\n", b);
    r = md5_hash(b, 1*8);
    printf("\t%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n", r.out[0], r.out[1], r.out[2], r.out[3], r.out[4], r.out[5], r.out[6], r.out[7], r.out[8], r.out[9], r.out[10], r.out[11], r.out[12], r.out[13], r.out[14], r.out[15]);
    printf("\t0cc175b9 c0f1b6a8 31c399e2 69772661 expected\n");
    const char *c = "abc";
    printf("%s\n", c);
    r = md5_hash(c, 3*8);
    printf("\t%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n", r.out[0], r.out[1], r.out[2], r.out[3], r.out[4], r.out[5], r.out[6], r.out[7], r.out[8], r.out[9], r.out[10], r.out[11], r.out[12], r.out[13], r.out[14], r.out[15]);
    printf("\t90015098 3cd24fb0 d6963f7d 28e17f72 expected\n");
    const char *d = "message digest";
    printf("%s\n", d);
    r = md5_hash(d, 14*8);
    printf("\t%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n", r.out[0], r.out[1], r.out[2], r.out[3], r.out[4], r.out[5], r.out[6], r.out[7], r.out[8], r.out[9], r.out[10], r.out[11], r.out[12], r.out[13], r.out[14], r.out[15]);
    printf("\tf96b697d 7cb7938d 525a2f31 aaf161d0 expected\n");
    const char *e = "abcdefghijklmnopqrstuvwxyz";
    printf("%s\n", e);
    r = md5_hash(e, 26*8);
    printf("\t%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n", r.out[0], r.out[1], r.out[2], r.out[3], r.out[4], r.out[5], r.out[6], r.out[7], r.out[8], r.out[9], r.out[10], r.out[11], r.out[12], r.out[13], r.out[14], r.out[15]);
    printf("\tc3fcd3d7 6192e400 7dfb496c ca67e13b expected\n");
    const char *f = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    printf("%s\n", f);
    r = md5_hash(f, 62*8);
    printf("\t%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n", r.out[0], r.out[1], r.out[2], r.out[3], r.out[4], r.out[5], r.out[6], r.out[7], r.out[8], r.out[9], r.out[10], r.out[11], r.out[12], r.out[13], r.out[14], r.out[15]);
    printf("\td174ab98 d277d9f5 a5611c2c 9f419d9f expected\n");
    const char *g = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    printf("%s\n", g);
    r = md5_hash(g, 80*8);
    printf("\t%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n", r.out[0], r.out[1], r.out[2], r.out[3], r.out[4], r.out[5], r.out[6], r.out[7], r.out[8], r.out[9], r.out[10], r.out[11], r.out[12], r.out[13], r.out[14], r.out[15]);
    printf("\t57edf4a2 2be3c955 ac49da2e 2107b67a expected\n");
}

#endif