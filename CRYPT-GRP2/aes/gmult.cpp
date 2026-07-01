#include "gmult.h"

uint8_t gmult(uint8_t a, uint8_t b)
{
    uint8_t p = 0;
    uint8_t high_bit_set;
    int i;

    /*
     * AES 使用的有限域 GF(2^8)，不可约多项式为：
     * x^8 + x^4 + x^3 + x + 1，对应常数 0x1b。
     */
    for (i = 0; i < 8; ++i) {
        if (b & 1) {
            p ^= a;
        }

        high_bit_set = (uint8_t)(a & 0x80);
        a <<= 1;

        if (high_bit_set) {
            a ^= 0x1b;
        }

        b >>= 1;
    }

    return p;
}
