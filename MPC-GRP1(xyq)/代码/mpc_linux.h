#ifndef MPC_LINUX_H
#define MPC_LINUX_H

#include <stdint.h>

// 定义返回码
typedef int RetCode_t;

// 1. 核心指针调用接口
RetCode_t mpc_linux_iopointer(int horizon, const void *input, void *output);

// 2. 带性能分析的自测试接口
RetCode_t mpc_linux_ioself_profiling(int horizon);

// 3. 静默功能自测试接口
RetCode_t mpc_linux_ioself(int horizon);

#endif