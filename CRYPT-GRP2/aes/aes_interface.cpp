#include "aes_interface.h"
#include "aes_gcm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AES_SAMPLE_COUNT 30
#define AES_WARMUP_COUNT 3

static void fill_test_data(uint8_t *data, int len)
{
    int i;

    for (i = 0; i < len; ++i) {
        data[i] = (uint8_t)(i & 0xff);
    }
}

static void fill_test_key(uint8_t key[AES_GCM_KEY_SIZE])
{
    int i;

    for (i = 0; i < AES_GCM_KEY_SIZE; ++i) {
        key[i] = (uint8_t)(i & 0xff);
    }
}

static void fill_test_iv(uint8_t iv[AES_GCM_IV_SIZE])
{
    int i;

    for (i = 0; i < AES_GCM_IV_SIZE; ++i) {
        iv[i] = (uint8_t)((i * 7 + 11) & 0xff);
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
#ifdef AES_ENABLE_INPLACE
    return "inplace";
#else
    return "outofplace";
#endif
}

#ifdef AES_ENABLE_CTX_REUSE

static AesGcmContext_t *get_reused_ctx(void)
{
    static AesGcmContext_t ctx;
    static int initialized = 0;
    static uint8_t cached_key[AES_GCM_KEY_SIZE];

    uint8_t key[AES_GCM_KEY_SIZE];

    fill_test_key(key);

    if (!initialized) {
        memset(&ctx, 0, sizeof(ctx));

        if (aes_gcm_context_init(&ctx, key) != 0) {
            return NULL;
        }

        memcpy(cached_key, key, AES_GCM_KEY_SIZE);
        initialized = 1;
    }

    /*
     * 本实验 key 固定，因此可以复用扩展后的轮密钥。
     * 如果真实业务 key 会变化，需要检测 key 变化并重新扩展。
     */
    if (memcmp(cached_key, key, AES_GCM_KEY_SIZE) != 0) {
        aes_gcm_context_free(&ctx);

        if (aes_gcm_context_init(&ctx, key) != 0) {
            initialized = 0;
            return NULL;
        }

        memcpy(cached_key, key, AES_GCM_KEY_SIZE);
    }

    return &ctx;
}

#endif

static int make_context(AesGcmContext_t *ctx,
                        const uint8_t key[AES_GCM_KEY_SIZE])
{
#ifdef AES_ENABLE_CTX_REUSE
    (void)ctx;
    (void)key;
    return 0;
#else
    memset(ctx, 0, sizeof(*ctx));
    return aes_gcm_context_init(ctx, key);
#endif
}

static AesGcmContext_t *select_context(AesGcmContext_t *local_ctx)
{
#ifdef AES_ENABLE_CTX_REUSE
    return get_reused_ctx();
#else
    return local_ctx;
#endif
}

static void release_context(AesGcmContext_t *ctx)
{
#ifndef AES_ENABLE_CTX_REUSE
    aes_gcm_context_free(ctx);
#else
    (void)ctx;
#endif
}

/*
 * 保留原 iopointer 的基本语义：
 * input 由外部传入，output 保存密文；
 * 函数内部再解密，最后 input 被恢复为原始明文。
 */
int aes_freertos_iopointer(int scale, void *input, void *output)
{
    uint8_t key[AES_GCM_KEY_SIZE];
    uint8_t iv[AES_GCM_IV_SIZE];
    uint8_t tag[AES_GCM_TAG_SIZE];

    AesGcmContext_t local_ctx;
    AesGcmContext_t *ctx = NULL;

    uint8_t *input_data = (uint8_t *)input;
    uint8_t *output_data = (uint8_t *)output;

    int ret = -1;

    if (scale <= 0 || input == NULL || output == NULL) {
        return -1;
    }

    fill_test_key(key);
    fill_test_iv(iv);
    memset(tag, 0, sizeof(tag));

    if (make_context(&local_ctx, key) != 0) {
        return -1;
    }

    ctx = select_context(&local_ctx);
    if (ctx == NULL) {
        goto cleanup;
    }

#ifdef AES_ENABLE_INPLACE
    /*
     * in-place 模式下，先把 input 原地加密成密文。
     * 为了保留 iopointer 的原始行为，再把密文复制到 output。
     */
    if (aes_gcm_encrypt(ctx,
                        iv,
                        NULL,
                        0,
                        input_data,
                        (size_t)scale,
                        input_data,
                        tag) != 0) {
        goto cleanup;
    }

    if (output_data != input_data) {
        memcpy(output_data, input_data, (size_t)scale);
    }

    if (aes_gcm_decrypt(ctx,
                        iv,
                        NULL,
                        0,
                        input_data,
                        (size_t)scale,
                        tag,
                        input_data) != 0) {
        goto cleanup;
    }
#else
    if (aes_gcm_encrypt(ctx,
                        iv,
                        NULL,
                        0,
                        input_data,
                        (size_t)scale,
                        output_data,
                        tag) != 0) {
        goto cleanup;
    }

    if (aes_gcm_decrypt(ctx,
                        iv,
                        NULL,
                        0,
                        output_data,
                        (size_t)scale,
                        tag,
                        input_data) != 0) {
        goto cleanup;
    }
#endif

    ret = 0;

cleanup:
    release_context(&local_ctx);
    return ret;
}

int aes_freertos_ioself(int scale)
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

    if (aes_freertos_iopointer(scale, input_data, output_data) != 0) {
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

int aes_freertos_ioself_profiling(int scale)
{
    uint8_t key[AES_GCM_KEY_SIZE];
    uint8_t iv[AES_GCM_IV_SIZE];
    uint8_t tag[AES_GCM_TAG_SIZE];

    AesGcmContext_t local_ctx;
    AesGcmContext_t *ctx = NULL;

    uint8_t *original_data = NULL;

#ifdef AES_ENABLE_INPLACE
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
    fill_test_iv(iv);

    if (make_context(&local_ctx, key) != 0) {
        return -1;
    }

    ctx = select_context(&local_ctx);
    if (ctx == NULL) {
        goto cleanup;
    }

    original_data = (uint8_t *)malloc((size_t)scale);
    if (original_data == NULL) {
        goto cleanup;
    }

    fill_test_data(original_data, scale);

#ifdef AES_ENABLE_INPLACE
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

    for (i = 0; i < AES_WARMUP_COUNT; ++i) {
        memset(tag, 0, sizeof(tag));

#ifdef AES_ENABLE_INPLACE
        if (aes_gcm_encrypt(ctx, iv, NULL, 0,
                            working_data, (size_t)scale,
                            working_data, tag) != 0) {
            goto cleanup;
        }

        if (aes_gcm_decrypt(ctx, iv, NULL, 0,
                            working_data, (size_t)scale,
                            tag, working_data) != 0) {
            goto cleanup;
        }

        if (memcmp(original_data, working_data, (size_t)scale) != 0) {
            goto cleanup;
        }
#else
        if (aes_gcm_encrypt(ctx, iv, NULL, 0,
                            plain_data, (size_t)scale,
                            cipher_data, tag) != 0) {
            goto cleanup;
        }

        if (aes_gcm_decrypt(ctx, iv, NULL, 0,
                            cipher_data, (size_t)scale,
                            tag, decrypted_data) != 0) {
            goto cleanup;
        }

        if (memcmp(original_data, decrypted_data, (size_t)scale) != 0) {
            goto cleanup;
        }
#endif
    }

    for (i = 0; i < AES_SAMPLE_COUNT; ++i) {
        memset(tag, 0, sizeof(tag));

#ifdef AES_ENABLE_INPLACE
        start_ns = timestamp_ns();

        if (aes_gcm_encrypt(ctx, iv, NULL, 0,
                            working_data, (size_t)scale,
                            working_data, tag) != 0) {
            goto cleanup;
        }

        end_ns = timestamp_ns();
        encrypt_ns = end_ns - start_ns;

        start_ns = timestamp_ns();

        if (aes_gcm_decrypt(ctx, iv, NULL, 0,
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

        if (aes_gcm_encrypt(ctx, iv, NULL, 0,
                            plain_data, (size_t)scale,
                            cipher_data, tag) != 0) {
            goto cleanup;
        }

        end_ns = timestamp_ns();
        encrypt_ns = end_ns - start_ns;

        start_ns = timestamp_ns();

        if (aes_gcm_decrypt(ctx, iv, NULL, 0,
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

    printf("CRYPT,AES-256-GCM,encrypt,io_mode=%s,horizon=%d,samples=%d,"
           "avg_ns=%lld,min_ns=%lld,max_ns=%lld,"
           "avg_ms=%.6f,min_ms=%.6f,max_ms=%.6f\n",
           get_io_mode_name(),
           scale,
           AES_SAMPLE_COUNT,
           encrypt_sum_ns / AES_SAMPLE_COUNT,
           encrypt_min_ns,
           encrypt_max_ns,
           (double)encrypt_sum_ns / AES_SAMPLE_COUNT / 1000000.0,
           (double)encrypt_min_ns / 1000000.0,
           (double)encrypt_max_ns / 1000000.0);

    printf("CRYPT,AES-256-GCM,decrypt,io_mode=%s,horizon=%d,samples=%d,"
           "avg_ns=%lld,min_ns=%lld,max_ns=%lld,"
           "avg_ms=%.6f,min_ms=%.6f,max_ms=%.6f,verify=OK\n",
           get_io_mode_name(),
           scale,
           AES_SAMPLE_COUNT,
           decrypt_sum_ns / AES_SAMPLE_COUNT,
           decrypt_min_ns,
           decrypt_max_ns,
           (double)decrypt_sum_ns / AES_SAMPLE_COUNT / 1000000.0,
           (double)decrypt_min_ns / 1000000.0,
           (double)decrypt_max_ns / 1000000.0);

    ret = 0;

cleanup:
    free(original_data);

#ifdef AES_ENABLE_INPLACE
    free(working_data);
#else
    free(plain_data);
    free(cipher_data);
    free(decrypted_data);
#endif

    release_context(&local_ctx);

    if (ret != 0) {
        printf("CRYPT,AES-256-GCM,io_mode=%s,horizon=%d,verify=FAILED\n",
               get_io_mode_name(),
               scale);
    }

    return ret;
}
