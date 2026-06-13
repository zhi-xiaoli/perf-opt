#include "chacha20_interface.h"
#include "chacha20_poly1305.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SAMPLE_COUNT 30
#define WARMUP_COUNT 3

static void fill_test_data(uint8_t *data, int len)
{
    int i;

    for (i = 0; i < len; ++i) {
        data[i] = (uint8_t)(i & 0xff);
    }
}

static void fill_test_key(uint8_t key[CHACHA20_KEY_SIZE])
{
    int i;

    for (i = 0; i < CHACHA20_KEY_SIZE; ++i) {
        key[i] = (uint8_t)(i & 0xff);
    }
}

static void fill_test_nonce(uint8_t nonce[CHACHA20_NONCE_SIZE])
{
    int i;

    for (i = 0; i < CHACHA20_NONCE_SIZE; ++i) {
        nonce[i] = (uint8_t)((i * 7 + 11) & 0xff);
    }
}

static long long timestamp_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }

    return (long long)now.tv_sec * 1000000000LL + (long long)now.tv_nsec;
}

static const char *get_io_mode_name(void)
{
#ifdef CHACHA20_ENABLE_INPLACE
    return "inplace";
#else
    return "outofplace";
#endif
}

/*
 * iopointer 保留“外部传入 input/output”的基本形式。
 * input 最后会被恢复成原始明文，output 保存密文。
 */
int chacha20_freertos_iopointer(int scale, void *input, void *output)
{
    uint8_t key[CHACHA20_KEY_SIZE];
    uint8_t nonce[CHACHA20_NONCE_SIZE];
    uint8_t tag[CHACHA20_TAG_SIZE];

    uint8_t *input_data = (uint8_t *)input;
    uint8_t *output_data = (uint8_t *)output;

    if (scale <= 0 || input == NULL || output == NULL) {
        return -1;
    }

    fill_test_key(key);
    fill_test_nonce(nonce);
    memset(tag, 0, sizeof(tag));

#ifdef CHACHA20_ENABLE_INPLACE
    if (chacha20_poly1305_encrypt(key,
                                  nonce,
                                  NULL,
                                  0,
                                  input_data,
                                  (size_t)scale,
                                  input_data,
                                  tag) != 0) {
        return -1;
    }

    if (output_data != input_data) {
        memcpy(output_data, input_data, (size_t)scale);
    }

    if (chacha20_poly1305_decrypt(key,
                                  nonce,
                                  NULL,
                                  0,
                                  input_data,
                                  (size_t)scale,
                                  tag,
                                  input_data) != 0) {
        return -1;
    }
#else
    if (chacha20_poly1305_encrypt(key,
                                  nonce,
                                  NULL,
                                  0,
                                  input_data,
                                  (size_t)scale,
                                  output_data,
                                  tag) != 0) {
        return -1;
    }

    if (chacha20_poly1305_decrypt(key,
                                  nonce,
                                  NULL,
                                  0,
                                  output_data,
                                  (size_t)scale,
                                  tag,
                                  input_data) != 0) {
        return -1;
    }
#endif

    return 0;
}

int chacha20_freertos_ioself(int scale)
{
    uint8_t *input_data = NULL;
    uint8_t *output_data = NULL;
    int i;
    int ret = -1;

    if (scale <= 0) {
        return -1;
    }

    input_data = (uint8_t *)malloc((size_t)scale);
    output_data = (uint8_t *)malloc((size_t)scale);

    if (input_data == NULL || output_data == NULL) {
        goto cleanup;
    }

    fill_test_data(input_data, scale);

    if (chacha20_freertos_iopointer(scale, input_data, output_data) != 0) {
        goto cleanup;
    }

    for (i = 0; i < scale; ++i) {
        if (input_data[i] != (uint8_t)(i & 0xff)) {
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    free(input_data);
    free(output_data);

    return ret;
}

int chacha20_freertos_ioself_profiling(int scale)
{
    uint8_t key[CHACHA20_KEY_SIZE];
    uint8_t nonce[CHACHA20_NONCE_SIZE];
    uint8_t tag[CHACHA20_TAG_SIZE];

    uint8_t *original_data = NULL;

#ifdef CHACHA20_ENABLE_INPLACE
    uint8_t *working_data = NULL;
#else
    uint8_t *plain_data = NULL;
    uint8_t *cipher_data = NULL;
    uint8_t *decrypted_data = NULL;
#endif

    int i;
    int ret = -1;

    long long start_ns;
    long long end_ns;
    long long encrypt_ns;
    long long decrypt_ns;

    long long encrypt_sum_ns = 0;
    long long decrypt_sum_ns = 0;

    long long encrypt_min_ns = 0x7fffffffffffffffLL;
    long long decrypt_min_ns = 0x7fffffffffffffffLL;

    long long encrypt_max_ns = 0;
    long long decrypt_max_ns = 0;

    if (scale <= 0) {
        return -1;
    }

    fill_test_key(key);
    fill_test_nonce(nonce);

    original_data = (uint8_t *)malloc((size_t)scale);
    if (original_data == NULL) {
        goto cleanup;
    }

    fill_test_data(original_data, scale);

#ifdef CHACHA20_ENABLE_INPLACE
    working_data = (uint8_t *)malloc((size_t)scale);
    if (working_data == NULL) {
        goto cleanup;
    }

    memcpy(working_data, original_data, (size_t)scale);
#else
    plain_data = (uint8_t *)malloc((size_t)scale);
    cipher_data = (uint8_t *)malloc((size_t)scale);
    decrypted_data = (uint8_t *)malloc((size_t)scale);

    if (plain_data == NULL || cipher_data == NULL || decrypted_data == NULL) {
        goto cleanup;
    }

    memcpy(plain_data, original_data, (size_t)scale);
#endif

    for (i = 0; i < WARMUP_COUNT; ++i) {
        memset(tag, 0, sizeof(tag));

#ifdef CHACHA20_ENABLE_INPLACE
        if (chacha20_poly1305_encrypt(key, nonce, NULL, 0,
                                      working_data, (size_t)scale,
                                      working_data, tag) != 0) {
            goto cleanup;
        }

        if (chacha20_poly1305_decrypt(key, nonce, NULL, 0,
                                      working_data, (size_t)scale,
                                      tag, working_data) != 0) {
            goto cleanup;
        }

        if (memcmp(original_data, working_data, (size_t)scale) != 0) {
            goto cleanup;
        }
#else
        if (chacha20_poly1305_encrypt(key, nonce, NULL, 0,
                                      plain_data, (size_t)scale,
                                      cipher_data, tag) != 0) {
            goto cleanup;
        }

        if (chacha20_poly1305_decrypt(key, nonce, NULL, 0,
                                      cipher_data, (size_t)scale,
                                      tag, decrypted_data) != 0) {
            goto cleanup;
        }

        if (memcmp(original_data, decrypted_data, (size_t)scale) != 0) {
            goto cleanup;
        }
#endif
    }

    for (i = 0; i < SAMPLE_COUNT; ++i) {
        memset(tag, 0, sizeof(tag));

#ifdef CHACHA20_ENABLE_INPLACE
        start_ns = timestamp_ns();

        if (chacha20_poly1305_encrypt(key, nonce, NULL, 0,
                                      working_data, (size_t)scale,
                                      working_data, tag) != 0) {
            goto cleanup;
        }

        end_ns = timestamp_ns();
        encrypt_ns = end_ns - start_ns;

        start_ns = timestamp_ns();

        if (chacha20_poly1305_decrypt(key, nonce, NULL, 0,
                                      working_data, (size_t)scale,
                                      tag, working_data) != 0) {
            goto cleanup;
        }

        end_ns = timestamp_ns();
        decrypt_ns = end_ns - start_ns;

        if (memcmp(original_data, working_data, (size_t)scale) != 0) {
            goto cleanup;
        }
#else
        start_ns = timestamp_ns();

        if (chacha20_poly1305_encrypt(key, nonce, NULL, 0,
                                      plain_data, (size_t)scale,
                                      cipher_data, tag) != 0) {
            goto cleanup;
        }

        end_ns = timestamp_ns();
        encrypt_ns = end_ns - start_ns;

        start_ns = timestamp_ns();

        if (chacha20_poly1305_decrypt(key, nonce, NULL, 0,
                                      cipher_data, (size_t)scale,
                                      tag, decrypted_data) != 0) {
            goto cleanup;
        }

        end_ns = timestamp_ns();
        decrypt_ns = end_ns - start_ns;

        if (memcmp(original_data, decrypted_data, (size_t)scale) != 0) {
            goto cleanup;
        }
#endif

        encrypt_sum_ns += encrypt_ns;
        decrypt_sum_ns += decrypt_ns;

        if (encrypt_ns < encrypt_min_ns) {
            encrypt_min_ns = encrypt_ns;
        }

        if (encrypt_ns > encrypt_max_ns) {
            encrypt_max_ns = encrypt_ns;
        }

        if (decrypt_ns < decrypt_min_ns) {
            decrypt_min_ns = decrypt_ns;
        }

        if (decrypt_ns > decrypt_max_ns) {
            decrypt_max_ns = decrypt_ns;
        }
    }

    printf("CRYPT,ChaCha20-Poly1305,encrypt,io_mode=%s,horizon=%d,samples=%d,"
           "avg_ns=%lld,min_ns=%lld,max_ns=%lld,"
           "avg_ms=%.6f,min_ms=%.6f,max_ms=%.6f\n",
           get_io_mode_name(),
           scale,
           SAMPLE_COUNT,
           encrypt_sum_ns / SAMPLE_COUNT,
           encrypt_min_ns,
           encrypt_max_ns,
           (double)encrypt_sum_ns / SAMPLE_COUNT / 1000000.0,
           (double)encrypt_min_ns / 1000000.0,
           (double)encrypt_max_ns / 1000000.0);

    printf("CRYPT,ChaCha20-Poly1305,decrypt,io_mode=%s,horizon=%d,samples=%d,"
           "avg_ns=%lld,min_ns=%lld,max_ns=%lld,"
           "avg_ms=%.6f,min_ms=%.6f,max_ms=%.6f,verify=OK\n",
           get_io_mode_name(),
           scale,
           SAMPLE_COUNT,
           decrypt_sum_ns / SAMPLE_COUNT,
           decrypt_min_ns,
           decrypt_max_ns,
           (double)decrypt_sum_ns / SAMPLE_COUNT / 1000000.0,
           (double)decrypt_min_ns / 1000000.0,
           (double)decrypt_max_ns / 1000000.0);

    ret = 0;

cleanup:
    free(original_data);

#ifdef CHACHA20_ENABLE_INPLACE
    free(working_data);
#else
    free(plain_data);
    free(cipher_data);
    free(decrypted_data);
#endif

    if (ret != 0) {
        printf("CRYPT,ChaCha20-Poly1305,io_mode=%s,horizon=%d,verify=FAILED\n",
               get_io_mode_name(),
               scale);
    }

    return ret;
}
