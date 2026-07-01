#include <stdio.h>
#include "fft_linux.h"

int main(void)
{
    int sizes[] = {
        256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536,
        1 << 17, 1 << 18, 1 << 19, 1 << 20
    };
    int count = sizeof(sizes) / sizeof(sizes[0]);

    printf("==================== FFT Profiling ====================\n");
    printf("%-8s | %-12s | %-18s\n", "Size", "FFT Time (ms)", "Max Error");
    printf("========================================================\n");

    for (int i = 0; i < count; i++) {
        RetCode_t ret = fft_linux_ioself_profiling(sizes[i]);
        if (ret != 0)
            printf("Size: %6d | ERROR: %d\n", sizes[i], ret);
    }

    printf("========================================================\n");
    printf("Profiling completed.\n");
    return 0;
}
