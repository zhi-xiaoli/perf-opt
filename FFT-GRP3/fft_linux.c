#include <stdio.h>
#include "fft_linux.h"
#include "timestamp.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#if defined(OPT_SIMD)
#include <arm_neon.h>
#endif

#if defined(OPT_OMP)
#include <omp.h>
#endif

/* SIMD 依赖 float 类型 twiddle */
#if defined(OPT_SIMD) && !defined(OPT_TWIDDLE_ITER)
#define OPT_TWIDDLE_ITER
#endif

#define PI 3.14159265358979323846

static inline void *aligned_malloc16(size_t size)
{
    void *p;
    if (posix_memalign(&p, 16, size) != 0) return NULL;
    return p;
}

/* ============================================================
 * twiddle factor table
 * ============================================================ */
#ifdef OPT_TWIDDLE_ITER
typedef float tw_t;
#else
typedef double tw_t;
#endif

typedef struct {
    tw_t **real;
    tw_t **imag;
    int max_level;
} TwTable;

static int tw_init(TwTable *t, int size)
{
    int max_level = 0;
    for (int s = size; s > 1; s >>= 1) max_level++;
    t->real = (tw_t **)malloc(sizeof(tw_t *) * max_level);
    t->imag = (tw_t **)malloc(sizeof(tw_t *) * max_level);
    if (!t->real || !t->imag) return -1;
    t->max_level = max_level;

    int level = 0;
    for (int le = 2; le <= size; le <<= 1, level++) {
        int le2 = le / 2;
#ifdef OPT_TWIDDLE_ITER
        t->real[level] = (tw_t *)aligned_malloc16(sizeof(tw_t) * le2);
        t->imag[level] = (tw_t *)aligned_malloc16(sizeof(tw_t) * le2);
#else
        t->real[level] = (tw_t *)malloc(sizeof(tw_t) * le2);
        t->imag[level] = (tw_t *)malloc(sizeof(tw_t) * le2);
#endif
        if (!t->real[level] || !t->imag[level]) {
            for (int i = 0; i <= level; i++) {
                free(t->real[i]); free(t->imag[i]);
            }
            free(t->real); free(t->imag);
            return -1;
        }

#ifdef OPT_TWIDDLE_ITER
        double angle = -2.0 * PI / le;
        float wr_step = (float)cos(angle);
        float wi_step = (float)sin(angle);
        float wr = 1.0f, wi = 0.0f;
        t->real[level][0] = 1.0f;
        t->imag[level][0] = 0.0f;
        for (int k = 1; k < le2; k++) {
            float wr_next = wr * wr_step - wi * wi_step;
            float wi_next = wr * wi_step + wi * wr_step;
            t->real[level][k] = wr_next;
            t->imag[level][k] = wi_next;
            wr = wr_next;
            wi = wi_next;
        }
#else
        for (int k = 0; k < le2; k++) {
            double a = -2.0 * PI * k / le;
            t->real[level][k] = (tw_t)cos(a);
            t->imag[level][k] = (tw_t)sin(a);
        }
#endif
    }
    return 0;
}

static void tw_free(TwTable *t)
{
    if (!t) return;
    for (int i = 0; i < t->max_level; i++) {
        free(t->real[i]);
        free(t->imag[i]);
    }
    free(t->real);
    free(t->imag);
}

/* ============================================================
 * twiddle cache
 * ============================================================ */
#ifdef OPT_CACHE
static TwTable  tw_cache_;
static int      tw_cache_size_ = 0;
static int      tw_cache_valid_ = 0;

static TwTable *tw_get_for_size(int size)
{
    if (!tw_cache_valid_ || tw_cache_size_ != size) {
        if (tw_cache_valid_) tw_free(&tw_cache_);
        if (tw_init(&tw_cache_, size) != 0) {
            tw_cache_valid_ = 0;
            return NULL;
        }
        tw_cache_size_ = size;
        tw_cache_valid_ = 1;
    }
    return &tw_cache_;
}
#else
static TwTable *tw_get_for_size(int size)
{
    TwTable *t = (TwTable *)malloc(sizeof(TwTable));
    if (!t) return NULL;
    if (tw_init(t, size) != 0) { free(t); return NULL; }
    return t;
}

static void tw_put(TwTable *t)
{
    if (!t) return;
    tw_free(t);
    free(t);
}
#endif

#if defined(OPT_SIMD)
static inline void butterfly_neon(
    float *xr, float *xi,
    const float *wr, const float *wi,
    int base, int le2)
{
    float32x4_t wr_v = vld1q_f32(wr);
    float32x4_t wi_v = vld1q_f32(wi);
    float32x4_t xr_ip = vld1q_f32(&xr[base + le2]);
    float32x4_t xi_ip = vld1q_f32(&xi[base + le2]);
    float32x4_t t_real = vmlsq_f32(vmulq_f32(wr_v, xr_ip), wi_v, xi_ip);
    float32x4_t t_imag = vmlaq_f32(vmulq_f32(wr_v, xi_ip), wi_v, xr_ip);
    float32x4_t xr_u = vld1q_f32(&xr[base]);
    float32x4_t xi_u = vld1q_f32(&xi[base]);
    vst1q_f32(&xr[base],          vaddq_f32(xr_u, t_real));
    vst1q_f32(&xi[base],          vaddq_f32(xi_u, t_imag));
    vst1q_f32(&xr[base + le2],    vsubq_f32(xr_u, t_real));
    vst1q_f32(&xi[base + le2],    vsubq_f32(xi_u, t_imag));
}
#endif

/* ============================================================
 * FFT core
 * ============================================================ */
static void fft(float *x_real, float *x_imag, int size, const TwTable *tw)
{
    int n = size;

    {
        int j = 0;
        for (int i = 1; i < n; i++) {
            int m = n >> 1;
            while (j >= m) { j -= m; m >>= 1; }
            j += m;
            if (i < j) {
                float tmp_r = x_real[i], tmp_i = x_imag[i];
                x_real[i] = x_real[j]; x_imag[i] = x_imag[j];
                x_real[j] = tmp_r;      x_imag[j] = tmp_i;
            }
        }
    }

    int level = 0;
    for (int le = 2; le <= n; le <<= 1, level++) {
        int le2 = le / 2;

#if defined(OPT_SPECIALIZE)
        if (le == 2) {
            for (int j = 0; j < n; j += 2) {
                float xr0 = x_real[j], xi0 = x_imag[j];
                float xr1 = x_real[j+1], xi1 = x_imag[j+1];
                x_real[j]   = xr0 + xr1;
                x_imag[j]   = xi0 + xi1;
                x_real[j+1] = xr0 - xr1;
                x_imag[j+1] = xi0 - xi1;
            }
            continue;
        }
        if (le == 4) {
            for (int j = 0; j < n; j += 4) {
                float xr0 = x_real[j],   xi0 = x_imag[j];
                float xr1 = x_real[j+1], xi1 = x_imag[j+1];
                float xr2 = x_real[j+2], xi2 = x_imag[j+2];
                float xr3 = x_real[j+3], xi3 = x_imag[j+3];
                float t0r = xr2, t0i = xi2;
                x_real[j]   = xr0 + t0r;   x_imag[j]   = xi0 + t0i;
                x_real[j+2] = xr0 - t0r;   x_imag[j+2] = xi0 - t0i;
                float t1r = xi3, t1i = -xr3;
                x_real[j+1] = xr1 + t1r;   x_imag[j+1] = xi1 + t1i;
                x_real[j+3] = xr1 - t1r;   x_imag[j+3] = xi1 - t1i;
            }
            continue;
        }
#endif

#if defined(OPT_SIMD)
        {
            const float *wr_l = (const float *)tw->real[level];
            const float *wi_l = (const float *)tw->imag[level];
#if defined(OPT_OMP)
            if (n > 4096) {
                #pragma omp parallel for schedule(static)
                for (int j = 0; j < n; j += le)
                    for (int k = 0; k < le2; k += 4)
                        butterfly_neon(x_real, x_imag, &wr_l[k], &wi_l[k], j + k, le2);
            } else
#endif
            {
                for (int j = 0; j < n; j += le)
                    for (int k = 0; k < le2; k += 4)
                        butterfly_neon(x_real, x_imag, &wr_l[k], &wi_l[k], j + k, le2);
            }
        }
#else
        {
            const tw_t *wr_l = tw->real[level];
            const tw_t *wi_l = tw->imag[level];
            for (int j = 0; j < n; j += le) {
                for (int k = 0; k < le2; k++) {
                    tw_t wr = wr_l[k];
                    tw_t wi = wi_l[k];
                    int ip = j + k + le2;
                    float tr = (float)(wr * x_real[ip] - wi * x_imag[ip]);
                    float ti = (float)(wr * x_imag[ip] + wi * x_real[ip]);
                    float u_re = x_real[j + k];
                    float u_im = x_imag[j + k];
                    x_real[j + k] = u_re + tr;
                    x_imag[j + k] = u_im + ti;
                    x_real[ip] = u_re - tr;
                    x_imag[ip] = u_im - ti;
                }
            }
        }
#endif
    }
}

/* ============================================================
 * IFFT core
 * ============================================================ */
static void ifft(float *x_real, float *x_imag, int size, const TwTable *tw)
{
#if defined(OPT_SIMD)
    float32x4_t vneg = vdupq_n_f32(-1.0f);
    for (int i = 0; i < size; i += 4)
        vst1q_f32(&x_imag[i], vmulq_f32(vld1q_f32(&x_imag[i]), vneg));
    fft(x_real, x_imag, size, tw);
    float32x4_t vscale_r = vdupq_n_f32(1.0f / size);
    float32x4_t vscale_i = vdupq_n_f32(-1.0f / size);
    for (int i = 0; i < size; i += 4) {
        vst1q_f32(&x_real[i], vmulq_f32(vld1q_f32(&x_real[i]), vscale_r));
        vst1q_f32(&x_imag[i], vmulq_f32(vld1q_f32(&x_imag[i]), vscale_i));
    }
#else
    for (int i = 0; i < size; i++)
        x_imag[i] = -x_imag[i];
    fft(x_real, x_imag, size, tw);
    for (int i = 0; i < size; i++) {
        x_real[i] = x_real[i] / size;
        x_imag[i] = -x_imag[i] / size;
    }
#endif
}

/* ============================================================
 * public API
 * ============================================================ */
RetCode_t fft_linux_iopointer(int size, void *input, void *output)
{
    if (input == NULL || output == NULL) return -1;
    if ((size & (size - 1)) != 0 || size < 2) return -2;

    FftIO *in  = (FftIO *)input;
    FftIO *out = (FftIO *)output;
    if (in->real == NULL || in->imag == NULL) return -3;
    if (out->real == NULL || out->imag == NULL) return -4;

    if (in->real != out->real)
        memcpy(out->real, in->real, sizeof(float) * size);
    if (in->imag != out->imag)
        memcpy(out->imag, in->imag, sizeof(float) * size);

    TwTable *tw = tw_get_for_size(size);
    if (!tw) return -5;

    fft(out->real, out->imag, size, tw);

#ifndef OPT_CACHE
    tw_put(tw);
#endif
    return 0;
}

RetCode_t fft_linux_ioself_profiling(int size)
{
    if ((size & (size - 1)) != 0 || size < 2) return -1;

    float *in_real  = (float *)aligned_malloc16(sizeof(float) * size);
    float *in_imag  = (float *)aligned_malloc16(sizeof(float) * size);
    float *out_real = (float *)aligned_malloc16(sizeof(float) * size);
    float *out_imag = (float *)aligned_malloc16(sizeof(float) * size);
    if (!in_real || !in_imag || !out_real || !out_imag) {
        free(in_real); free(in_imag); free(out_real); free(out_imag);
        return -2;
    }

    srand((unsigned int)time(NULL));
    for (int i = 0; i < size; i++) {
        in_real[i] = (float)(rand() % 65536 - 32768);
        in_imag[i] = 0.0f;
    }

    FftIO input  = {in_real,  in_imag};
    FftIO output = {out_real, out_imag};

    timestamp_t t0 = timestamp();
    RetCode_t ret = fft_linux_iopointer(size, &input, &output);
    timestamp_t t1 = timestamp();

    if (ret != 0) {
        free(in_real); free(in_imag); free(out_real); free(out_imag);
        return ret;
    }

    int64_t ns = timestamp_diff(t0, t1);

    /* 打印前 8 个频点的 FFT 结果 */
    int show = size < 8 ? size : 8;
    printf("Size: %6d | FFT Time: %.4f ms\n", size, ns / 1000000.0);
    printf("  FFT result (first %d bins):\n", show);
    for (int i = 0; i < show; i++)
        printf("    [%3d] %12.4f + %12.4fi\n", i, out_real[i], out_imag[i]);

    /* IFFT 验证 */
    double max_diff = 0.0;
    {
        TwTable *tw = tw_get_for_size(size);
        if (tw) {
            ifft(out_real, out_imag, size, tw);
        }
#ifndef OPT_CACHE
        tw_put(tw);
#endif
        for (int i = 0; i < size; i++) {
            double diff = fabs(out_real[i] - in_real[i]);
            if (diff > max_diff) max_diff = diff;
        }
    }

    printf("  Max Error: %.6f\n\n", max_diff);

    free(in_real); free(in_imag); free(out_real); free(out_imag);
    return 0;
}

RetCode_t fft_linux_ioself(int size)
{
    if ((size & (size - 1)) != 0 || size < 2) return -1;

    float *in_real  = (float *)aligned_malloc16(sizeof(float) * size);
    float *in_imag  = (float *)aligned_malloc16(sizeof(float) * size);
    float *out_real = (float *)aligned_malloc16(sizeof(float) * size);
    float *out_imag = (float *)aligned_malloc16(sizeof(float) * size);
    if (!in_real || !in_imag || !out_real || !out_imag) {
        free(in_real); free(in_imag); free(out_real); free(out_imag);
        return -2;
    }

    srand((unsigned int)time(NULL));
    for (int i = 0; i < size; i++) {
        in_real[i] = (float)(rand() % 65536 - 32768);
        in_imag[i] = 0.0f;
    }

    FftIO input  = {in_real,  in_imag};
    FftIO output = {out_real, out_imag};

    RetCode_t ret = fft_linux_iopointer(size, &input, &output);

    free(in_real); free(in_imag); free(out_real); free(out_imag);
    return ret;
}

/* ============================================================
 * file I/O 接口：从文件读数据做 FFT，结果写回文件
 * 格式：每行 "real imag" 或 "real, imag"（imag 可省略，# 开头为注释）
 * ============================================================ */
RetCode_t fft_linux_iofile(int size, const char *in_path, const char *out_path)
{
    if (size < 2 || (size & (size - 1)) != 0) return -1;
    if (!in_path || !out_path) return -2;

    FILE *fin = fopen(in_path, "r");
    if (!fin) { printf("Error: cannot open %s\n", in_path); return -3; }

    float *real = (float *)aligned_malloc16(sizeof(float) * size);
    float *imag = (float *)aligned_malloc16(sizeof(float) * size);
    if (!real || !imag) {
        fclose(fin); free(real); free(imag);
        return -4;
    }

    int count = 0;
    char line[256];
    while (count < size && fgets(line, sizeof(line), fin)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char *end;
        double rd = strtod(line, &end);
        if (end == line) continue;
        double id = strtod(end, NULL);
        real[count] = (float)rd;
        imag[count] = (float)id;
        count++;
    }
    fclose(fin);

    if (count < size) {
        printf("Warning: only %d data points (expected %d), zero-padding\n", count, size);
        for (int i = count; i < size; i++) real[i] = imag[i] = 0.0f;
    }

    float *out_real = (float *)aligned_malloc16(sizeof(float) * size);
    float *out_imag = (float *)aligned_malloc16(sizeof(float) * size);
    if (!out_real || !out_imag) {
        free(real); free(imag); free(out_real); free(out_imag);
        return -4;
    }
    memcpy(out_real, real, sizeof(float) * size);
    memcpy(out_imag, imag, sizeof(float) * size);

    FftIO in = {real, imag}, out = {out_real, out_imag};
    RetCode_t ret = fft_linux_iopointer(size, &in, &out);

    if (ret == 0) {
        FILE *fout = fopen(out_path, "w");
        if (!fout) {
            printf("Error: cannot write %s\n", out_path);
            free(real); free(imag); free(out_real); free(out_imag);
            return -5;
        }
        for (int i = 0; i < size; i++)
            fprintf(fout, "%.6f %.6f\n", out_real[i], out_imag[i]);
        fclose(fout);
        printf("FFT done. %d bins written to %s\n", size, out_path);
    }

    free(real); free(imag); free(out_real); free(out_imag);
    return ret;
}
