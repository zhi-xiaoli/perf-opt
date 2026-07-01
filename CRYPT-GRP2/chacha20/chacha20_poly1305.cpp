#include "chacha20_poly1305.h"

#include <stdlib.h>
#include <string.h>

static uint32_t load32_le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void store32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void store64_le(uint8_t *p, uint64_t v)
{
    int i;

    for (i = 0; i < 8; ++i) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint32_t rotl32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static void quarter_round(uint32_t state[16],
                          int a,
                          int b,
                          int c,
                          int d)
{
    /*
     * ChaCha20 的核心混合操作：
     * 加法、异或、循环左移。这里的加法是 32 位模加。
     */
    state[a] += state[b];
    state[d] ^= state[a];
    state[d] = rotl32(state[d], 16);

    state[c] += state[d];
    state[b] ^= state[c];
    state[b] = rotl32(state[b], 12);

    state[a] += state[b];
    state[d] ^= state[a];
    state[d] = rotl32(state[d], 8);

    state[c] += state[d];
    state[b] ^= state[c];
    state[b] = rotl32(state[b], 7);
}

static void chacha20_block(const uint8_t key[32],
                           const uint8_t nonce[12],
                           uint32_t counter,
                           uint8_t output[64])
{
    static const uint8_t sigma[16] = {
    'e', 'x', 'p', 'a',
    'n', 'd', ' ', '3',
    '2', '-', 'b', 'y',
    't', 'e', ' ', 'k'
    };

    uint32_t state[16];
    uint32_t working_state[16];
    int i;

    state[0] = load32_le((const uint8_t *)(sigma + 0));
    state[1] = load32_le((const uint8_t *)(sigma + 4));
    state[2] = load32_le((const uint8_t *)(sigma + 8));
    state[3] = load32_le((const uint8_t *)(sigma + 12));

    for (i = 0; i < 8; ++i) {
        state[4 + i] = load32_le(key + i * 4);
    }

    state[12] = counter;
    state[13] = load32_le(nonce + 0);
    state[14] = load32_le(nonce + 4);
    state[15] = load32_le(nonce + 8);

    memcpy(working_state, state, sizeof(state));

    /*
     * ChaCha20 = 10 个 double round。
     * 每个 double round 包含 1 次列混合和 1 次对角线混合。
     */
    for (i = 0; i < 10; ++i) {
        quarter_round(working_state, 0, 4, 8, 12);
        quarter_round(working_state, 1, 5, 9, 13);
        quarter_round(working_state, 2, 6, 10, 14);
        quarter_round(working_state, 3, 7, 11, 15);

        quarter_round(working_state, 0, 5, 10, 15);
        quarter_round(working_state, 1, 6, 11, 12);
        quarter_round(working_state, 2, 7, 8, 13);
        quarter_round(working_state, 3, 4, 9, 14);
    }

    for (i = 0; i < 16; ++i) {
        working_state[i] += state[i];
        store32_le(output + i * 4, working_state[i]);
    }
}

void chacha20_xor(uint8_t *output,
                  const uint8_t *input,
                  size_t length,
                  const uint8_t key[CHACHA20_KEY_SIZE],
                  const uint8_t nonce[CHACHA20_NONCE_SIZE],
                  uint32_t counter)
{
    uint8_t block[64];
    size_t offset = 0;
    size_t todo;
    size_t i;

    while (length > 0) {
        chacha20_block(key, nonce, counter, block);
        counter++;

        todo = length < 64 ? length : 64;

        for (i = 0; i < todo; ++i) {
            output[offset + i] = input[offset + i] ^ block[i];
        }

        offset += todo;
        length -= todo;
    }

    memset(block, 0, sizeof(block));
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

/*
 * Poly1305 实现使用 5 个 26-bit limb。
 * 这样可以避免大整数库，同时保证中间乘法用 64 位保存。
 */
void poly1305_mac(uint8_t tag[CHACHA20_TAG_SIZE],
                  const uint8_t *message,
                  size_t message_len,
                  const uint8_t key[32])
{
    const uint64_t mask26 = 0x3ffffff;

    uint64_t r0, r1, r2, r3, r4;
    uint64_t s1, s2, s3, s4;

    uint64_t h0 = 0;
    uint64_t h1 = 0;
    uint64_t h2 = 0;
    uint64_t h3 = 0;
    uint64_t h4 = 0;

    uint64_t d0, d1, d2, d3, d4;
    uint64_t c;

    uint8_t block[16];
    size_t block_len;
    uint64_t hibit;

    uint64_t g0, g1, g2, g3, g4;
    uint64_t mask;

    uint64_t f0, f1, f2, f3;
    uint64_t pad0, pad1, pad2, pad3;

    r0 = load32_le(key + 0) & 0x3ffffff;
    r1 = (load32_le(key + 3) >> 2) & 0x3ffff03;
    r2 = (load32_le(key + 6) >> 4) & 0x3ffc0ff;
    r3 = (load32_le(key + 9) >> 6) & 0x3f03fff;
    r4 = (load32_le(key + 12) >> 8) & 0x00fffff;

    s1 = r1 * 5;
    s2 = r2 * 5;
    s3 = r3 * 5;
    s4 = r4 * 5;

    while (message_len > 0) {
        if (message_len >= 16) {
            block_len = 16;
            hibit = ((uint64_t)1 << 24);
            memcpy(block, message, 16);
        } else {
            block_len = message_len;
            hibit = 0;
            memset(block, 0, sizeof(block));
            memcpy(block, message, block_len);
            block[block_len] = 1;
        }

        h0 += load32_le(block + 0) & 0x3ffffff;
        h1 += (load32_le(block + 3) >> 2) & 0x3ffffff;
        h2 += (load32_le(block + 6) >> 4) & 0x3ffffff;
        h3 += (load32_le(block + 9) >> 6) & 0x3ffffff;
        h4 += ((load32_le(block + 12) >> 8) & 0x00ffffff) | hibit;

        d0 = h0 * r0 + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;
        d1 = h0 * r1 + h1 * r0 + h2 * s4 + h3 * s3 + h4 * s2;
        d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * s4 + h4 * s3;
        d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * s4;
        d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;

        c = d0 >> 26;
        h0 = d0 & mask26;
        d1 += c;

        c = d1 >> 26;
        h1 = d1 & mask26;
        d2 += c;

        c = d2 >> 26;
        h2 = d2 & mask26;
        d3 += c;

        c = d3 >> 26;
        h3 = d3 & mask26;
        d4 += c;

        c = d4 >> 26;
        h4 = d4 & mask26;
        h0 += c * 5;

        c = h0 >> 26;
        h0 &= mask26;
        h1 += c;

        message += block_len;
        message_len -= block_len;
    }

    c = h1 >> 26;
    h1 &= mask26;
    h2 += c;

    c = h2 >> 26;
    h2 &= mask26;
    h3 += c;

    c = h3 >> 26;
    h3 &= mask26;
    h4 += c;

    c = h4 >> 26;
    h4 &= mask26;
    h0 += c * 5;

    c = h0 >> 26;
    h0 &= mask26;
    h1 += c;

    /*
     * 如果 h >= p，则减去 p。
     * 这里用 mask 避免分支，减少时序差异。
     */
    g0 = h0 + 5;
    c = g0 >> 26;
    g0 &= mask26;

    g1 = h1 + c;
    c = g1 >> 26;
    g1 &= mask26;

    g2 = h2 + c;
    c = g2 >> 26;
    g2 &= mask26;

    g3 = h3 + c;
    c = g3 >> 26;
    g3 &= mask26;

    g4 = h4 + c - ((uint64_t)1 << 26);

    mask = (g4 >> 63) - 1;

    h0 = (h0 & ~mask) | (g0 & mask);
    h1 = (h1 & ~mask) | (g1 & mask);
    h2 = (h2 & ~mask) | (g2 & mask);
    h3 = (h3 & ~mask) | (g3 & mask);
    h4 = (h4 & ~mask) | (g4 & mask);

    f0 = (h0 | (h1 << 26)) & 0xffffffff;
    f1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffff;
    f2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffff;
    f3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffff;

    pad0 = load32_le(key + 16);
    pad1 = load32_le(key + 20);
    pad2 = load32_le(key + 24);
    pad3 = load32_le(key + 28);

    f0 += pad0;
    c = f0 >> 32;
    f0 &= 0xffffffff;

    f1 += pad1 + c;
    c = f1 >> 32;
    f1 &= 0xffffffff;

    f2 += pad2 + c;
    c = f2 >> 32;
    f2 &= 0xffffffff;

    f3 += pad3 + c;
    f3 &= 0xffffffff;

    store32_le(tag + 0, (uint32_t)f0);
    store32_le(tag + 4, (uint32_t)f1);
    store32_le(tag + 8, (uint32_t)f2);
    store32_le(tag + 12, (uint32_t)f3);

    memset(block, 0, sizeof(block));
}

static void poly1305_key_gen(uint8_t poly_key[32],
                             const uint8_t key[32],
                             const uint8_t nonce[12])
{
    uint8_t block[64];

    /*
     * AEAD 中 counter=0 的 ChaCha20 block 用于生成 Poly1305 一次性 key。
     * 正文加密从 counter=1 开始。
     */
    chacha20_block(key, nonce, 0, block);
    memcpy(poly_key, block, 32);
    memset(block, 0, sizeof(block));
}

static size_t pad16_len(size_t len)
{
    size_t r = len % 16;

    if (r == 0) {
        return 0;
    }

    return 16 - r;
}

static int build_mac_data(uint8_t **out,
                          size_t *out_len,
                          const uint8_t *aad,
                          size_t aad_len,
                          const uint8_t *ciphertext,
                          size_t ciphertext_len)
{
    size_t aad_pad = pad16_len(aad_len);
    size_t cipher_pad = pad16_len(ciphertext_len);
    size_t total = aad_len + aad_pad + ciphertext_len + cipher_pad + 16;
    uint8_t *buf = (uint8_t *)malloc(total);
    size_t pos = 0;

    if (buf == NULL) {
        return -1;
    }

    memset(buf, 0, total);

    if (aad_len > 0 && aad != NULL) {
        memcpy(buf + pos, aad, aad_len);
    }
    pos += aad_len + aad_pad;

    if (ciphertext_len > 0 && ciphertext != NULL) {
        memcpy(buf + pos, ciphertext, ciphertext_len);
    }
    pos += ciphertext_len + cipher_pad;

    /*
     * 最后 16 字节存放 AAD 和密文长度，单位是 bit。
     */
    store64_le(buf + pos, (uint64_t)aad_len * 8);
    store64_le(buf + pos + 8, (uint64_t)ciphertext_len * 8);

    *out = buf;
    *out_len = total;

    return 0;
}

int chacha20_poly1305_encrypt(const uint8_t key[CHACHA20_KEY_SIZE],
                              const uint8_t nonce[CHACHA20_NONCE_SIZE],
                              const uint8_t *aad,
                              size_t aad_len,
                              const uint8_t *plaintext,
                              size_t plaintext_len,
                              uint8_t *ciphertext,
                              uint8_t tag[CHACHA20_TAG_SIZE])
{
    uint8_t poly_key[32];
    uint8_t *mac_data = NULL;
    size_t mac_data_len = 0;

    if (key == NULL || nonce == NULL || plaintext == NULL ||
        ciphertext == NULL || tag == NULL) {
        return -1;
    }

    poly1305_key_gen(poly_key, key, nonce);

    chacha20_xor(ciphertext,
                 plaintext,
                 plaintext_len,
                 key,
                 nonce,
                 1);

    if (build_mac_data(&mac_data,
                       &mac_data_len,
                       aad,
                       aad_len,
                       ciphertext,
                       plaintext_len) != 0) {
        memset(poly_key, 0, sizeof(poly_key));
        return -1;
    }

    poly1305_mac(tag, mac_data, mac_data_len, poly_key);

    memset(poly_key, 0, sizeof(poly_key));
    memset(mac_data, 0, mac_data_len);
    free(mac_data);

    return 0;
}

int chacha20_poly1305_decrypt(const uint8_t key[CHACHA20_KEY_SIZE],
                              const uint8_t nonce[CHACHA20_NONCE_SIZE],
                              const uint8_t *aad,
                              size_t aad_len,
                              const uint8_t *ciphertext,
                              size_t ciphertext_len,
                              const uint8_t tag[CHACHA20_TAG_SIZE],
                              uint8_t *plaintext)
{
    uint8_t poly_key[32];
    uint8_t expected_tag[CHACHA20_TAG_SIZE];
    uint8_t *mac_data = NULL;
    size_t mac_data_len = 0;

    if (key == NULL || nonce == NULL || ciphertext == NULL ||
        tag == NULL || plaintext == NULL) {
        return -1;
    }

    poly1305_key_gen(poly_key, key, nonce);

    if (build_mac_data(&mac_data,
                       &mac_data_len,
                       aad,
                       aad_len,
                       ciphertext,
                       ciphertext_len) != 0) {
        memset(poly_key, 0, sizeof(poly_key));
        return -1;
    }

    poly1305_mac(expected_tag, mac_data, mac_data_len, poly_key);

    memset(poly_key, 0, sizeof(poly_key));
    memset(mac_data, 0, mac_data_len);
    free(mac_data);

    if (!constant_time_equal(expected_tag, tag, CHACHA20_TAG_SIZE)) {
        memset(expected_tag, 0, sizeof(expected_tag));
        return -1;
    }

    memset(expected_tag, 0, sizeof(expected_tag));

    chacha20_xor(plaintext,
                 ciphertext,
                 ciphertext_len,
                 key,
                 nonce,
                 1);

    return 0;
}
