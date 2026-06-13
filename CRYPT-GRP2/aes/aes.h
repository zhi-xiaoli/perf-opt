#ifndef CRYPTION_AES_H
#define CRYPTION_AES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t *aes_init(size_t key_size);

void aes_key_expansion(uint8_t *key, uint8_t *w);

void aes_cipher(uint8_t *in, uint8_t *out, uint8_t *w);

void aes_inv_cipher(uint8_t *in, uint8_t *out, uint8_t *w);

#ifdef __cplusplus
}
#endif

#endif
