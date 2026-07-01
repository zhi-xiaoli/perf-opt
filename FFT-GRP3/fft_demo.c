#include <stdio.h>
#include <math.h>
#include "fft_linux.h"

#define N 1024

int main(void)
{
    float in_real[N], in_imag[N];
    float out_real[N], out_imag[N];

    /* 生成测试信号：50Hz 正弦波 + 120Hz 正弦波，采样率 1000Hz */
    float fs = 1000.0f;
    for (int i = 0; i < N; i++) {
        float t = i / fs;
        in_real[i] = sinf(2.0f * 3.14159f * 50.0f * t)
                   + 0.5f * sinf(2.0f * 3.14159f * 120.0f * t);
        in_imag[i] = 0.0f;
    }

    FftIO input  = {in_real,  in_imag};
    FftIO output = {out_real, out_imag};

    if (fft_linux_iopointer(N, &input, &output) != 0) {
        printf("FFT failed\n");
        return 1;
    }

    /* 打印前 16 个频点的幅度谱 */
    printf("index\tfreq(Hz)\tmagnitude\n");
    for (int i = 0; i < 16; i++) {
        float mag = sqrtf(out_real[i] * out_real[i] + out_imag[i] * out_imag[i]);
        float freq = (float)i * fs / N;
        printf("%3d\t%7.2f\t%8.2f\n", i, freq, mag);
    }
    return 0;
}
