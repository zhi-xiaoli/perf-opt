#ifndef CHACHA20_POLY1305_H
#define CHACHA20_POLY1305_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHACHA20_KEY_SIZE 32
#define CHACHA20_NONCE_SIZE 12
#define CHACHA20_TAG_SIZE 16

void chacha20_xor(uint8_t *output,
                  const uint8_t *input,
                  size_t length,
                  const uint8_t key[CHACHA20_KEY_SIZE],
                  const uint8_t nonce[CHACHA20_NONCE_SIZE],
                  uint32_t counter);

void poly1305_mac(uint8_t tag[CHACHA20_TAG_SIZE],
                  const uint8_t *message,
                  size_t message_len,
                  const uint8_t key[32]);

int chacha20_poly1305_encrypt(const uint8_t key[CHACHA20_KEY_SIZE],
                              const uint8_t nonce[CHACHA20_NONCE_SIZE],
                              const uint8_t *aad,
                              size_t aad_len,
                              const uint8_t *plaintext,
                              size_t plaintext_len,
                              uint8_t *ciphertext,
                              uint8_t tag[CHACHA20_TAG_SIZE]);

int chacha20_poly1305_decrypt(const uint8_t key[CHACHA20_KEY_SIZE],
                              const uint8_t nonce[CHACHA20_NONCE_SIZE],
                              const uint8_t *aad,
                              size_t aad_len,
                              const uint8_t *ciphertext,
                              size_t ciphertext_len,
                              const uint8_t tag[CHACHA20_TAG_SIZE],
                              uint8_t *plaintext);

#ifdef __cplusplus
}
#endif

#endif
