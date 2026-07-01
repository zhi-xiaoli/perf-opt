#ifndef FFT_LINUX_H
#define FFT_LINUX_H

#include <stdint.h>

typedef int RetCode_t;

typedef struct {
    float *real;
    float *imag;
} FftIO;

RetCode_t fft_linux_iopointer(int size, void *input, void *output);
RetCode_t fft_linux_ioself_profiling(int size);
RetCode_t fft_linux_ioself(int size);
RetCode_t fft_linux_iofile(int size, const char *in_path, const char *out_path);

#endif
