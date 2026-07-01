1. 环境要求：
   - 操作系统：Linux (openEuler)
   - 架构：ARM64 (鲲鹏处理器)
   - 编译器：gcc (需支持 OpenMP 和 NEON)

2. 编译命令：
   gcc mpc_linux_test.c mpc_linux.c timestamp.c mpc.c \
       -o mpc_test \
       -fopenmp -lm -O3 -march=armv8-a

   说明：
   -fopenmp: 启用多线程支持
   -lm: 链接数学库
   -O3: 开启高级优化
   -march=armv8-a: 针对鲲鹏架构优化

3. 执行方式：
   ./mpc_test

4. 文件结构：
   - mpc_linux.h/c: 三个算子标准接口
   - mpc_linux_test.c: 主测试程序
   - timestamp.h/c: 纳秒计时工具
   - mpc.h/c: 原始算法实现逻辑