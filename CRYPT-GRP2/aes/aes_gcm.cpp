#include "aes_gcm.h"
#include "aes.h"

#include <stdlib.h>
#include <string.h>

static void xor_block(uint8_t out[16], const uint8_t in[16])
{
    int i;

    for (i = 0; i < 16; ++i) {
        out[i] ^= in[i];
    }
}

static void store64_be(uint8_t out[8], uint64_t v)
{
    int i;

    for (i = 0; i < 8; ++i) {
        out[7 - i] = (uint8_t)(v >> (8 * i));
    }
}

static int constant_time_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    size_t i;

    for (i = 0; i < len; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }

    return diff == 0;
}

static void shift_right_one(uint8_t block[16])
{
    int i;
    uint8_t carry = 0;

    for (i = 0; i < 16; ++i) {
        uint8_t new_carry = (uint8_t)(block[i] & 1);
        block[i] = (uint8_t)((block[i] >> 1) | (carry << 7));
        carry = new_carry;
    }
}

static int get_bit_be(const uint8_t block[16], int bit_index)
{
    int byte_index = bit_index / 8;
    int bit_in_byte = 7 - (bit_index % 8);

    return (block[byte_index] >> bit_in_byte) & 1;
}

static void gf128_mul(uint8_t x[16], const uint8_t h[16])
{
    uint8_t z[16];
    uint8_t v[16];
    int i;
    int lsb;

    memset(z, 0, sizeof(z));
    memcpy(v, h, 16);

    /*
     * GHASH 的乘法在 GF(2^128) 中进行。
     * 这里采用逐 bit 的通用实现，便于理解，不依赖硬件指令。
     */
    for (i = 0; i < 128; ++i) {
        if (get_bit_be(x, i)) {
            xor_block(z, v);
        }

        lsb = v[15] & 1;
        shift_right_one(v);

        if (lsb) {
            v[0] ^= 0xe1;
        }
    }

    memcpy(x, z, 16);
}

static void ghash_update_block(uint8_t y[16],
                               const uint8_t h[16],
                               const uint8_t block[16])
{
    xor_block(y, block);
    gf128_mul(y, h);
}

static void ghash_update_data(uint8_t y[16],
                              const uint8_t h[16],
                              const uint8_t *data,
                              size_t data_len)
{
    uint8_t block[16];
    size_t offset = 0;
    size_t todo;

    while (data_len > 0) {
        memset(block, 0, sizeof(block));
        todo = data_len < 16 ? data_len : 16;

        if (data != NULL && todo > 0) {
            memcpy(block, data + offset, todo);
        }

        ghash_update_block(y, h, block);

        offset += todo;
        data_len -= todo;
    }
}

static void ghash(uint8_t out[16],
                  const uint8_t h[16],
                  const uint8_t *aad,
                  size_t aad_len,
                  const uint8_t *ciphertext,
                  size_t ciphertext_len)
{
    uint8_t y[16];
    uint8_t len_block[16];

    memset(y, 0, sizeof(y));

    if (aad_len > 0) {
        ghash_update_data(y, h, aad, aad_len);
    }

    if (ciphertext_len > 0) {
        ghash_update_data(y, h, ciphertext, ciphertext_len);
    }

    memset(len_block, 0, sizeof(len_block));

    /*
     * GCM 最后一个 GHASH block 保存 AAD 和密文长度，单位是 bit。
     * 这里使用大端序，符合 GCM 的长度编码方式。
     */
    store64_be(len_block, (uint64_t)aad_len * 8);
    store64_be(len_block + 8, (uint64_t)ciphertext_len * 8);

    ghash_update_block(y, h, len_block);

    memcpy(out, y, 16);
}

static void inc32(uint8_t counter[16])
{
    int i;

    /*
     * GCM 的 inc32 只递增最后 32 bit。
     * 前 12 字节 IV 部分保持不变。
     */
    for (i = 15; i >= 12; --i) {
        counter[i]++;
        if (counter[i] != 0) {
            break;
        }
    }
}

static void aes_encrypt_block(AesGcmContext_t *ctx,
                              const uint8_t in[16],
                              uint8_t out[16])
{
    aes_cipher((uint8_t *)in, out, ctx->round_key);
}

static void aes_ctr_xor(AesGcmContext_t *ctx,
                        const uint8_t j0[16],
                        const uint8_t *input,
                        uint8_t *output,
                        size_t len)
{
    uint8_t counter[16];
    uint8_t stream[16];
    size_t offset = 0;
    size_t todo;
    size_t i;

    memcpy(counter, j0, 16);
    inc32(counter);

    while (len > 0) {
        aes_encrypt_block(ctx, counter, stream);

        todo = len < 16 ? len : 16;

        for (i = 0; i < todo; ++i) {
            output[offset + i] = input[offset + i] ^ stream[i];
        }

        offset += todo;
        len -= todo;

        inc32(counter);
    }

    memset(stream, 0, sizeof(stream));
}

static void make_j0(uint8_t j0[16], const uint8_t iv[AES_GCM_IV_SIZE])
{
    /*
     * 本实验固定使用 12 字节 IV。
     * GCM 中 96-bit IV 的 J0 构造方式是 IV || 0x00000001。
     */
    memcpy(j0, iv, AES_GCM_IV_SIZE);
    j0[12] = 0x00;
    j0[13] = 0x00;
    j0[14] = 0x00;
    j0[15] = 0x01;
}

int aes_gcm_context_init(AesGcmContext_t *ctx,
                         const uint8_t key[AES_GCM_KEY_SIZE])
{
    if (ctx == NULL || key == NULL) {
        return -1;
    }

    ctx->round_key = aes_init(AES_GCM_KEY_SIZE);
    if (ctx->round_key == NULL) {
        return -1;
    }

    /*
     * 原始 aes.cpp 中的 aes_key_expansion 用于生成 AES-256 的轮密钥。
     * 后续每个 counter block 都复用这份轮密钥。
     */
    aes_key_expansion((uint8_t *)key, ctx->round_key);

    return 0;
}

void aes_gcm_context_free(AesGcmContext_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->round_key != NULL) {
        memset(ctx->round_key, 0, AES_BLOCK_SIZE * 15);
        free(ctx->round_key);
        ctx->round_key = NULL;
    }
}

static int aes_gcm_compute_tag(AesGcmContext_t *ctx,
                               const uint8_t iv[AES_GCM_IV_SIZE],
                               const uint8_t *aad,
                               size_t aad_len,
                               const uint8_t *ciphertext,
                               size_t ciphertext_len,
                               uint8_t tag[AES_GCM_TAG_SIZE])
{
    uint8_t zero_block[16];
    uint8_t h[16];
    uint8_t j0[16];
    uint8_t s[16];
    uint8_t tag_mask[16];
    int i;

    if (ctx == NULL || ctx->round_key == NULL || iv == NULL ||
        tag == NULL) {
        return -1;
    }

    memset(zero_block, 0, sizeof(zero_block));

    /*
     * H = AES_K(0^128)，用于 GHASH。
     */
    aes_encrypt_block(ctx, zero_block, h);

    make_j0(j0, iv);

    ghash(s, h, aad, aad_len, ciphertext, ciphertext_len);

    /*
     * tag = AES_K(J0) XOR GHASH(...)
     */
    aes_encrypt_block(ctx, j0, tag_mask);

    for (i = 0; i < 16; ++i) {
        tag[i] = tag_mask[i] ^ s[i];
    }

    memset(h, 0, sizeof(h));
    memset(s, 0, sizeof(s));
    memset(tag_mask, 0, sizeof(tag_mask));

    return 0;
}

int aes_gcm_encrypt(AesGcmContext_t *ctx,
                    const uint8_t iv[AES_GCM_IV_SIZE],
                    const uint8_t *aad,
                    size_t aad_len,
                    const uint8_t *plaintext,
                    size_t plaintext_len,
                    uint8_t *ciphertext,
                    uint8_t tag[AES_GCM_TAG_SIZE])
{
    uint8_t j0[16];

    if (ctx == NULL || ctx->round_key == NULL || iv == NULL ||
        plaintext == NULL || ciphertext == NULL || tag == NULL) {
        return -1;
    }

    make_j0(j0, iv);

    /*
     * GCM 加密部分是 CTR：AES_K(counter) 生成密钥流，再和明文异或。
     * 正文从 inc32(J0) 开始。
     */
    aes_ctr_xor(ctx, j0, plaintext, ciphertext, plaintext_len);

    if (aes_gcm_compute_tag(ctx,
                            iv,
                            aad,
                            aad_len,
                            ciphertext,
                            plaintext_len,
                            tag) != 0) {
        return -1;
    }

    return 0;
}

int aes_gcm_decrypt(AesGcmContext_t *ctx,
                    const uint8_t iv[AES_GCM_IV_SIZE],
                    const uint8_t *aad,
                    size_t aad_len,
                    const uint8_t *ciphertext,
                    size_t ciphertext_len,
                    const uint8_t tag[AES_GCM_TAG_SIZE],
                    uint8_t *plaintext)
{
    uint8_t expected_tag[AES_GCM_TAG_SIZE];
    uint8_t j0[16];

    if (ctx == NULL || ctx->round_key == NULL || iv == NULL ||
        ciphertext == NULL || tag == NULL || plaintext == NULL) {
        return -1;
    }

    /*
     * 先验证 tag，再输出可信明文。
     * 这样可以避免认证失败时使用被篡改的数据。
     */
    if (aes_gcm_compute_tag(ctx,
                            iv,
                            aad,
                            aad_len,
                            ciphertext,
                            ciphertext_len,
                            expected_tag) != 0) {
        return -1;
    }

    if (!constant_time_equal(expected_tag, tag, AES_GCM_TAG_SIZE)) {
        memset(expected_tag, 0, sizeof(expected_tag));
        return -1;
    }

    memset(expected_tag, 0, sizeof(expected_tag));

    make_j0(j0, iv);
    aes_ctr_xor(ctx, j0, ciphertext, plaintext, ciphertext_len);

    return 0;
}
