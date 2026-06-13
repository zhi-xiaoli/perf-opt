#ifndef AES_GCM_H
#define AES_GCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AES_GCM_KEY_SIZE 32
#define AES_GCM_IV_SIZE 12
#define AES_GCM_TAG_SIZE 16
#define AES_BLOCK_SIZE 16

typedef struct AesGcmContext_s {
    uint8_t *round_key;
} AesGcmContext_t;

int aes_gcm_context_init(AesGcmContext_t *ctx,
                         const uint8_t key[AES_GCM_KEY_SIZE]);

void aes_gcm_context_free(AesGcmContext_t *ctx);

int aes_gcm_encrypt(AesGcmContext_t *ctx,
                    const uint8_t iv[AES_GCM_IV_SIZE],
                    const uint8_t *aad,
                    size_t aad_len,
                    const uint8_t *plaintext,
                    size_t plaintext_len,
                    uint8_t *ciphertext,
                    uint8_t tag[AES_GCM_TAG_SIZE]);

int aes_gcm_decrypt(AesGcmContext_t *ctx,
                    const uint8_t iv[AES_GCM_IV_SIZE],
                    const uint8_t *aad,
                    size_t aad_len,
                    const uint8_t *ciphertext,
                    size_t ciphertext_len,
                    const uint8_t tag[AES_GCM_TAG_SIZE],
                    uint8_t *plaintext);

#ifdef __cplusplus
}
#endif

#endif
