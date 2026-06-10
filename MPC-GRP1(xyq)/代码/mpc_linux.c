#include "mpc_linux.h"
#include "mpc.h"
#include "timestamp.h"
#include <stdio.h>
#include <stdlib.h>

// 实现第一个接口：指针调用
RetCode_t mpc_linux_iopointer(int horizon, const void *input, void *output) {
    if (horizon <= 0 || input == NULL || output == NULL) return -1;
    const State_t *in = (const State_t *)input;
    Controls_t *out = (Controls_t *)output;
    mpc_control_with_plan(*in, out); // 调用 mpc.c 里的核心算法
    return 0;
}

// 实现第二个接口：带性能统计
RetCode_t mpc_linux_ioself_profiling(int horizon) {
    if (horizon <= 0) return -1;
    State_t in = {0.0f, 1.0f, 0.321f, 2.84f, 1.0f, 0.03f};
    Controls_t out;
    init_controls(&out, horizon);

    timestamp_t start = timestamp(); // 使用新的时间函数
    RetCode_t ret = mpc_linux_iopointer(horizon, &in, &out);
    timestamp_t end = timestamp();

    if (ret == 0) {
        int64_t ns = timestamp_diff(start, end);
        printf("[Profiling] Horizon: %d, Time: %ld ns\n", horizon, ns);
    }
    free_controls(&out);
    return ret;
}

// 实现第三个接口：简单调用
RetCode_t mpc_linux_ioself(int horizon) {
    State_t in = {0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f};
    Controls_t out;
    init_controls(&out, horizon);
    RetCode_t ret = mpc_linux_iopointer(horizon, &in, &out);
    free_controls(&out);
    return ret;
}