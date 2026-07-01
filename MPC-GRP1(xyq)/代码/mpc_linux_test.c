#include "mpc_linux.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    int test_horizons[] = {10, 50, 100};
    int num_tests = sizeof(test_horizons) / sizeof(int);

    printf("==== MPC Operator Performance Sampling (Kunpeng) ====\n");

    for (int i = 0; i < num_tests; i++) {
        // 执行采样，每次采样内部独立分配释放内存
        mpc_linux_ioself_profiling(test_horizons[i]);
    }

    printf("==== Test Completed ====\n");
    return 0;
}