#include "aes.h"
#include "gmult.h"

#include <stdlib.h>
#include <string.h>

/*
 * AES 参数。
 * Nb 固定为 4，表示 state 有 4 列，每列 4 字节。
 * Nk 和 Nr 根据 key_size 在 aes_init() 中确定。
 */
static int Nb = 4;
static int Nk = 8;
static int Nr = 14;

static uint8_t s_box[256];
static uint8_t inv_s_box[256];
static int s_box_ready = 0;

static uint8_t gf_pow(uint8_t a, int power)
{
    uint8_t result = 1;

    while (power > 0) {
        if (power & 1) {
            result = gmult(result, a);
        }

        a = gmult(a, a);
        power >>= 1;
    }

    return result;
}

static uint8_t gf_inverse(uint8_t a)
{
    if (a == 0) {
        return 0;
    }

    /*
     * GF(2^8) 中非零元素 a 的逆元为 a^254。
     */
    return gf_pow(a, 254);
}

static uint8_t rotl8(uint8_t x, int n)
{
    return (uint8_t)((x << n) | (x >> (8 - n)));
}

static void aes_build_sbox(void)
{
    int i;
    uint8_t x;
    uint8_t y;

    if (s_box_ready) {
        return;
    }

    /*
     * 动态生成 S-box，避免在源码中放置 256 项大表。
     * AES S-box = 有限域逆元 + 仿射变换。
     */
    for (i = 0; i < 256; ++i) {
        x = gf_inverse((uint8_t)i);

        y = (uint8_t)(0x63 ^
                      x ^
                      rotl8(x, 1) ^
                      rotl8(x, 2) ^
                      rotl8(x, 3) ^
                      rotl8(x, 4));

        s_box[i] = y;
        inv_s_box[y] = (uint8_t)i;
    }

    s_box_ready = 1;
}

static uint8_t rcon_value(uint8_t i)
{
    uint8_t c = 1;

    if (i == 0) {
        return 0;
    }

    while (i > 1) {
        c = gmult(c, 0x02);
        --i;
    }

    return c;
}

static void coef_add(const uint8_t a[4],
                     const uint8_t b[4],
                     uint8_t d[4])
{
    d[0] = a[0] ^ b[0];
    d[1] = a[1] ^ b[1];
    d[2] = a[2] ^ b[2];
    d[3] = a[3] ^ b[3];
}

static void coef_mult(const uint8_t a[4],
                      const uint8_t b[4],
                      uint8_t d[4])
{
    d[0] = gmult(a[0], b[0]) ^
           gmult(a[3], b[1]) ^
           gmult(a[2], b[2]) ^
           gmult(a[1], b[3]);

    d[1] = gmult(a[1], b[0]) ^
           gmult(a[0], b[1]) ^
           gmult(a[3], b[2]) ^
           gmult(a[2], b[3]);

    d[2] = gmult(a[2], b[0]) ^
           gmult(a[1], b[1]) ^
           gmult(a[0], b[2]) ^
           gmult(a[3], b[3]);

    d[3] = gmult(a[3], b[0]) ^
           gmult(a[2], b[1]) ^
           gmult(a[1], b[2]) ^
           gmult(a[0], b[3]);
}

static void add_round_key(uint8_t *state,
                          const uint8_t *w,
                          uint8_t round)
{
    uint8_t row;
    uint8_t col;

    for (col = 0; col < Nb; ++col) {
        for (row = 0; row < 4; ++row) {
            state[Nb * row + col] ^= w[4 * Nb * round + 4 * col + row];
        }
    }
}

static void sub_bytes(uint8_t *state)
{
    uint8_t row;
    uint8_t col;

    for (row = 0; row < 4; ++row) {
        for (col = 0; col < Nb; ++col) {
            state[Nb * row + col] = s_box[state[Nb * row + col]];
        }
    }
}

static void inv_sub_bytes(uint8_t *state)
{
    uint8_t row;
    uint8_t col;

    for (row = 0; row < 4; ++row) {
        for (col = 0; col < Nb; ++col) {
            state[Nb * row + col] = inv_s_box[state[Nb * row + col]];
        }
    }
}

static void shift_rows(uint8_t *state)
{
    uint8_t row;
    uint8_t shift;
    uint8_t col;
    uint8_t tmp;

    /*
     * 第 0 行不动，第 1/2/3 行分别循环左移 1/2/3 字节。
     */
    for (row = 1; row < 4; ++row) {
        for (shift = 0; shift < row; ++shift) {
            tmp = state[Nb * row + 0];

            for (col = 1; col < Nb; ++col) {
                state[Nb * row + col - 1] = state[Nb * row + col];
            }

            state[Nb * row + Nb - 1] = tmp;
        }
    }
}

static void inv_shift_rows(uint8_t *state)
{
    uint8_t row;
    uint8_t shift;
    int col;
    uint8_t tmp;

    for (row = 1; row < 4; ++row) {
        for (shift = 0; shift < row; ++shift) {
            tmp = state[Nb * row + Nb - 1];

            for (col = Nb - 1; col > 0; --col) {
                state[Nb * row + col] = state[Nb * row + col - 1];
            }

            state[Nb * row + 0] = tmp;
        }
    }
}

static void mix_columns(uint8_t *state)
{
    uint8_t a[4] = {0x02, 0x01, 0x01, 0x03};
    uint8_t col_data[4];
    uint8_t result[4];
    uint8_t col;
    uint8_t row;

    /*
     * MixColumns 对 state 的每一列分别做有限域矩阵乘法，
     * 让一列中的 4 个字节互相扩散。
     */
    for (col = 0; col < Nb; ++col) {
        for (row = 0; row < 4; ++row) {
            col_data[row] = state[Nb * row + col];
        }

        coef_mult(a, col_data, result);

        for (row = 0; row < 4; ++row) {
            state[Nb * row + col] = result[row];
        }
    }
}

static void inv_mix_columns(uint8_t *state)
{
    uint8_t a[4] = {0x0e, 0x09, 0x0d, 0x0b};
    uint8_t col_data[4];
    uint8_t result[4];
    uint8_t col;
    uint8_t row;

    for (col = 0; col < Nb; ++col) {
        for (row = 0; row < 4; ++row) {
            col_data[row] = state[Nb * row + col];
        }

        coef_mult(a, col_data, result);

        for (row = 0; row < 4; ++row) {
            state[Nb * row + col] = result[row];
        }
    }
}

static void sub_word(uint8_t *word)
{
    int i;

    for (i = 0; i < 4; ++i) {
        word[i] = s_box[word[i]];
    }
}

static void rot_word(uint8_t *word)
{
    uint8_t tmp = word[0];

    word[0] = word[1];
    word[1] = word[2];
    word[2] = word[3];
    word[3] = tmp;
}

void aes_key_expansion(uint8_t *key, uint8_t *w)
{
    uint8_t tmp[4];
    uint8_t rcon[4];
    int i;
    int len;

    aes_build_sbox();

    len = Nb * (Nr + 1);

    for (i = 0; i < Nk; ++i) {
        w[4 * i + 0] = key[4 * i + 0];
        w[4 * i + 1] = key[4 * i + 1];
        w[4 * i + 2] = key[4 * i + 2];
        w[4 * i + 3] = key[4 * i + 3];
    }

    for (i = Nk; i < len; ++i) {
        tmp[0] = w[4 * (i - 1) + 0];
        tmp[1] = w[4 * (i - 1) + 1];
        tmp[2] = w[4 * (i - 1) + 2];
        tmp[3] = w[4 * (i - 1) + 3];

        if (i % Nk == 0) {
            rot_word(tmp);
            sub_word(tmp);

            rcon[0] = rcon_value((uint8_t)(i / Nk));
            rcon[1] = 0x00;
            rcon[2] = 0x00;
            rcon[3] = 0x00;

            coef_add(tmp, rcon, tmp);
        } else if (Nk > 6 && i % Nk == 4) {
            /*
             * AES-256 的 key schedule 特有步骤。
             */
            sub_word(tmp);
        }

        w[4 * i + 0] = w[4 * (i - Nk) + 0] ^ tmp[0];
        w[4 * i + 1] = w[4 * (i - Nk) + 1] ^ tmp[1];
        w[4 * i + 2] = w[4 * (i - Nk) + 2] ^ tmp[2];
        w[4 * i + 3] = w[4 * (i - Nk) + 3] ^ tmp[3];
    }
}

uint8_t *aes_init(size_t key_size)
{
    aes_build_sbox();

    switch (key_size) {
        case 16:
            Nk = 4;
            Nr = 10;
            break;

        case 24:
            Nk = 6;
            Nr = 12;
            break;

        case 32:
            Nk = 8;
            Nr = 14;
            break;

        default:
            Nk = 8;
            Nr = 14;
            key_size = 32;
            break;
    }

    (void)key_size;

    /*
     * 扩展轮密钥大小：
     * Nb * (Nr + 1) 个 word，每个 word 4 字节。
     * AES-256 时为 4 * 15 * 4 = 240 字节。
     */
    return (uint8_t *)malloc((size_t)(Nb * (Nr + 1) * 4));
}

void aes_cipher(uint8_t *in, uint8_t *out, uint8_t *w)
{
    uint8_t state[16];
    uint8_t round;
    uint8_t row;
    uint8_t col;

    aes_build_sbox();

    /*
     * AES state 使用列优先布局。
     * 输入 block 是 16 字节，转换成 4x4 state。
     */
    for (row = 0; row < 4; ++row) {
        for (col = 0; col < Nb; ++col) {
            state[Nb * row + col] = in[row + 4 * col];
        }
    }

    add_round_key(state, w, 0);

    for (round = 1; round < Nr; ++round) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, w, round);
    }

    /*
     * AES 最后一轮没有 MixColumns。
     */
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, w, (uint8_t)Nr);

    for (row = 0; row < 4; ++row) {
        for (col = 0; col < Nb; ++col) {
            out[row + 4 * col] = state[Nb * row + col];
        }
    }

    memset(state, 0, sizeof(state));
}

void aes_inv_cipher(uint8_t *in, uint8_t *out, uint8_t *w)
{
    uint8_t state[16];
    int round;
    uint8_t row;
    uint8_t col;

    aes_build_sbox();

    for (row = 0; row < 4; ++row) {
        for (col = 0; col < Nb; ++col) {
            state[Nb * row + col] = in[row + 4 * col];
        }
    }

    add_round_key(state, w, (uint8_t)Nr);

    for (round = Nr - 1; round >= 1; --round) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, w, (uint8_t)round);
        inv_mix_columns(state);
    }

    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, w, 0);

    for (row = 0; row < 4; ++row) {
        for (col = 0; col < Nb; ++col) {
            out[row + 4 * col] = state[Nb * row + col];
        }
    }

    memset(state, 0, sizeof(state));
}
