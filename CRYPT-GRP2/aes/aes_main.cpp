#include <stdio.h>

#include "aes_interface.h"

int main(void)
{
    int test_scale[] = {
        64,
        256,
        1024,
        4096,
        16 * 1024,
        64 * 1024,
        256 * 1024,
        1024 * 1024
    };

    int test_num = (int)(sizeof(test_scale) / sizeof(test_scale[0]));
    int i;

    printf("CRYPT AES-256-GCM profiling start\n");
    printf("horizon means input data length in bytes\n\n");

    for (i = 0; i < test_num; ++i) {
        int ret = aes_freertos_ioself_profiling(test_scale[i]);

        if (ret != 0) {
            printf("CRYPT profiling failed, horizon=%d, ret=%d\n",
                   test_scale[i],
                   ret);
            return ret;
        }

        printf("\n");
    }

    printf("CRYPT AES-256-GCM profiling finished\n");

    return 0;
}
