FFT Library for ARM Linux (Kunpeng)
=====================================

Group: FFT-GRP3
Seminar topic: FFT program optimization on ARM Linux / Kunpeng platform

文件清单：
  fft_linux.h        - 公开 API 声明
  fft_linux.c        - FFT 核心实现（含 5 个独立编译开关）
  fft_linux_test.c   - 性能测试：循环 13 种规模，打印时间/误差/频点结果
  fft_demo.c         - 示例程序：50Hz+120Hz 正弦波 FFT 演示
  timestamp.h        - 纳秒级时间函数声明
  timestamp.c        - 纳秒级时间函数实现
  input.txt          - 示例输入数据文件（1024 点，两路正弦波叠加）
  readme.txt         - 本文件
  presentation.pdf   - 研讨汇报 PPT（Beamer 生成）


=== 一、四个 API 函数 ===

1. fft_linux_iopointer(int size, void *input, void *output)
   - 功能：用户提供输入输出数据，计算 FFT
   - input / output：指向 FftIO 结构体的指针
     typedef struct {
         float *real;   // 实部数组，长度 size
         float *imag;   // 虚部数组，长度 size
     } FftIO;
   - 返回 0 成功，负数表示错误

2. fft_linux_ioself_profiling(int size)
   - 功能：自生成随机数据，调用 fft_linux_iopointer 并计时
   - 打印：时间（ms）、前 8 个频点的 FFT 结果、IFFT 验证误差
   - 适用于性能测试和正确性验证

3. fft_linux_ioself(int size)
   - 功能：自生成数据，调用 iopointer，不计时不打印
   - 适用于纯功能测试

4. fft_linux_iofile(int size, const char *in_path, const char *out_path)
   - 功能：从文本文件读取数据做 FFT，结果写回文件
   - 输入文件格式：每行一个 "real imag"，# 开头的行忽略
   - imag 可省略（默认为 0）
   - 输出文件格式：每行 "real imag"
   - 参考 input.txt


=== 二、五个独立编译开关 ===

  开关                     优化内容                    默认行为
  ─────────────────────────────────────────────────────────────
  -DOPT_TWIDDLE_ITER      旋转因子递推生成            double + cos/sin
                           (float + 迭代)
  -DOPT_SPECIALIZE        le=2 / le=4 蝶形特化        通用循环
  -DOPT_SIMD              NEON SIMD 向量化            标量
                           (自动开启 TWIDDLE_ITER)
  -DOPT_OMP               OpenMP 多核并行             单线程
  -DOPT_CACHE             Twiddle Cache 复用          每次新建/释放
  ─────────────────────────────────────────────────────────────
  不加任何 -D  = 基线版（所有优化关闭）


=== 三、编译命令 ===

  1) 基线版（全关）
     gcc -O3 -march=armv8-a -o fft_test fft_linux_test.c fft_linux.c timestamp.c -lm

  2) 逐步开启优化
     # 旋转因子递推 + 蝶形特化
     gcc -O3 -march=armv8-a -DOPT_TWIDDLE_ITER -DOPT_SPECIALIZE \
         -o fft_test fft_linux_test.c fft_linux.c timestamp.c -lm

     # +SIMD
     gcc -O3 -march=armv8-a -DOPT_SPECIALIZE -DOPT_SIMD \
         -o fft_test fft_linux_test.c fft_linux.c timestamp.c -lm

     # +OpenMP +Cache（全开）
     gcc -O3 -march=armv8-a -DOPT_SPECIALIZE -DOPT_SIMD -DOPT_OMP -DOPT_CACHE \
         -fopenmp -o fft_test fft_linux_test.c fft_linux.c timestamp.c -lm

  3) 示例程序（自定义输入演示）
     gcc -O3 -march=armv8-a -DOPT_TWIDDLE_ITER -DOPT_SPECIALIZE -DOPT_SIMD \
         -DOPT_OMP -DOPT_CACHE -fopenmp \
         -o fft_demo fft_demo.c fft_linux.c timestamp.c -lm


=== 四、运行方法 ===

  性能测试：
     ./fft_test

  示例程序：
     ./fft_demo

  文件 I/O：
     写一个 main 调用 fft_linux_iofile(1024, "input.txt", "output.txt");
     编译运行即可。结果保存在 output.txt 中。


=== 五、输出示例 ===

  Size:   1024 | FFT Time: 0.0276 ms
    FFT result (first 8 bins):
      [  0]     123.4567 +       0.0000i
      [  1]    -123.4567 +     456.7890i
      [  2]      45.6789 +     -12.3456i
      ...
    Max Error: 0.246094


=== 六、交叉编译（x86 → aarch64） ===

  aarch64-linux-gnu-gcc -static -O3 -march=armv8-a \
      -DOPT_SPECIALIZE -DOPT_SIMD -DOPT_OMP -DOPT_CACHE -fopenmp \
      -o fft_test fft_linux_test.c fft_linux.c timestamp.c -lm
