#ifndef CRYPTION_GMULT_H
#define CRYPTION_GMULT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GF(2^8) 乘法。
 * AES 的 MixColumns 和 Rcon 生成都会用到。
 */
uint8_t gmult(uint8_t a, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif
