# CRYPT 实验说明

## 1. 项目简介

本项目实现了 CRYPT 算子的手写版本，用于完成数据的加密、解密和完整性校验。当前版本只包含两个算法目录：

```text
crypt/
├── aes/
├── chacha20/
└── CMakeLists.txt
```

实验中使用的数据来自本次运行生成的结果文件：

```text
result_chacha20_outofplace.txt
result_chacha20_inplace.txt
result_aes_outofplace.txt
result_aes_inplace.txt
result_aes_inplace_ctxreuse.txt
```

## 2. 算法实现

### 2.1 ChaCha20-Poly1305

`chacha20/` 目录实现 ChaCha20-Poly1305：

```text
chacha20/
├── chacha20_main.cpp
├── chacha20_interface.cpp
├── chacha20_interface.h
├── chacha20_poly1305.cpp
└── chacha20_poly1305.h
```

其中：

- ChaCha20 负责生成密钥流，并与明文或密文进行异或。
- Poly1305 负责根据密文和长度信息生成 16 字节认证标签。
- 解密时会重新计算 tag，只有 tag 一致才认为验证通过。
- 当前测试中 AAD 为空，主要验证密文和长度信息。

### 2.2 AES-256-GCM

`aes/` 目录实现 AES-256-GCM：

```text
aes/
├── aes.cpp
├── aes.h
├── gmult.cpp
├── gmult.h
├── aes_gcm.cpp
├── aes_gcm.h
├── aes_interface.cpp
├── aes_interface.h
└── aes_main.cpp
```

其中：

- `aes.cpp / aes.h` 实现 AES block 加密流程。
- `gmult.cpp / gmult.h` 实现 AES 中 GF(2^8) 乘法。
- `aes_gcm.cpp / aes_gcm.h` 在 AES block 的基础上实现 GCM，包括 CTR 加密、GHASH、tag 生成和 tag 验证。
- 解密时先验证 tag，再执行 CTR 解密。

## 3. 接口说明

两个算法都保留类似的三接口形式。

ChaCha20-Poly1305：

```cpp
int chacha20_freertos_iopointer(int scale, void *input, void *output);
int chacha20_freertos_ioself_profiling(int scale);
int chacha20_freertos_ioself(int scale);
```

AES-256-GCM：

```cpp
int aes_freertos_iopointer(int scale, void *input, void *output);
int aes_freertos_ioself_profiling(int scale);
int aes_freertos_ioself(int scale);
```

其中：

```text
scale = 输入数据长度，单位 byte
```

`ioself_profiling` 会自动生成测试数据，执行加密、解密、tag 验证和 `memcmp` 校验，并输出性能数据。

## 4. 编译与运行

### 4.1 默认版本

默认版本为 out-of-place：

```bash
cd crypt
rm -rf build bin
mkdir build
cd build

cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

运行：

```bash
../bin/aes_encrypt > result_aes_outofplace.txt
../bin/chacha20_encrypt > result_chacha20_outofplace.txt
```

### 4.2 AES in-place

```bash
cd crypt
rm -rf build_aes_inplace
mkdir build_aes_inplace
cd build_aes_inplace

cmake -DCMAKE_BUILD_TYPE=Release -DAES_ENABLE_INPLACE=ON ..
make -j$(nproc)

../bin/aes_encrypt > result_aes_inplace.txt
```

### 4.3 ChaCha20 in-place

```bash
cd crypt
rm -rf build_chacha20_inplace
mkdir build_chacha20_inplace
cd build_chacha20_inplace

cmake -DCMAKE_BUILD_TYPE=Release -DCHACHA20_ENABLE_INPLACE=ON ..
make -j$(nproc)

../bin/chacha20_encrypt > result_chacha20_inplace.txt
```

### 4.4 AES in-place + ctx reuse

```bash
cd crypt
rm -rf build_aes_inplace_ctxreuse
mkdir build_aes_inplace_ctxreuse
cd build_aes_inplace_ctxreuse

cmake -DCMAKE_BUILD_TYPE=Release \
      -DAES_ENABLE_INPLACE=ON \
      -DAES_ENABLE_CTX_REUSE=ON \
      ..

make -j$(nproc)

../bin/aes_encrypt > result_aes_inplace_ctxreuse.txt
```

## 5. 优化方案说明

本次手写版本只考虑两个优化方向：in-place 和 ctx reuse。

### 5.1 in-place 原地加解密

out-of-place 使用不同缓冲区保存输入和输出：

```text
plain_data -> cipher_data -> decrypted_data
```

in-place 使用同一块工作缓冲区进行加密和解密：

```text
working_data 明文 -> working_data 密文 -> working_data 明文
```

该方法不改变算法本身，只改变输入输出缓冲区的使用方式。它的目标是减少活跃缓冲区数量，降低内存占用和内存访问压力。

### 5.2 ctx reuse

在本手写 AES-256-GCM 版本中，ctx reuse 指复用 AES key expansion 后的 round key，而不是复用第三方库上下文。

普通 AES 版本每次会执行 key expansion。ctx reuse 版本在 key 不变的情况下复用扩展后的轮密钥，减少重复初始化开销。

本次结果中，ctx reuse 只测试了与 AES in-place 组合后的版本：

```text
AES-256-GCM + in-place + ctx reuse
```

## 6. Profiling 方法

每个输入规模执行：

```text
3 次 warm-up
30 次正式采样
```

输出指标包括：

```text
avg_ns / min_ns / max_ns
avg_ms / min_ms / max_ms
verify=OK
```

本 README 的主要分析使用 `avg_ms`。

## 7. 完整实验数据：avg_ms

| horizon | size   | operation | AES out-of-place | AES in-place | AES in-place + ctx reuse | ChaCha20 out-of-place | ChaCha20 in-place |
| ------: | ------ | --------- | ---------------: | -----------: | -----------------------: | --------------------: | ----------------: |
|      64 | 64 B   | decrypt   |         0.176906 |     0.165049 |                 0.148077 |              0.002084 |           0.00209 |
|      64 | 64 B   | encrypt   |          0.17341 |     0.167568 |                 0.147383 |              0.002062 |          0.002074 |
|     256 | 256 B  | decrypt   |         0.548635 |      0.49899 |                 0.457195 |              0.005274 |          0.005182 |
|     256 | 256 B  | encrypt   |         0.543062 |     0.516086 |                 0.458122 |              0.005869 |          0.005133 |
|    1024 | 1 KB   | decrypt   |          1.84204 |       1.9965 |                  1.83519 |              0.017144 |          0.017126 |
|    1024 | 1 KB   | encrypt   |          1.93129 |      1.78998 |                  1.86253 |              0.017104 |           0.01707 |
|    4096 | 4 KB   | decrypt   |          7.25916 |      7.25446 |                  7.68124 |              0.065562 |          0.065249 |
|    4096 | 4 KB   | encrypt   |          7.31136 |      7.22118 |                  7.57542 |              0.065255 |          0.065053 |
|   16384 | 16 KB  | decrypt   |          27.2889 |      29.6556 |                  28.4946 |              0.259224 |          0.257458 |
|   16384 | 16 KB  | encrypt   |          27.2357 |      30.0175 |                  28.3441 |              0.263685 |          0.258172 |
|   65536 | 64 KB  | decrypt   |           109.63 |      113.634 |                  115.751 |               1.03774 |           1.10193 |
|   65536 | 64 KB  | encrypt   |          108.729 |       114.39 |                  116.009 |               1.04241 |           1.12083 |
|  262144 | 256 KB | decrypt   |          422.681 |      455.723 |                  427.533 |               4.15542 |           4.16122 |
|  262144 | 256 KB | encrypt   |          420.119 |        437.9 |                   425.63 |               4.20648 |           4.16699 |
| 1048576 | 1 MB   | decrypt   |           1682.2 |      1683.29 |                  1684.09 |               16.8283 |            16.863 |
| 1048576 | 1 MB   | encrypt   |          1682.31 |      1683.18 |                  1684.17 |               16.9537 |           16.9314 |

## 8. 关键结果对比

### 8.1 算法对比：ChaCha20-Poly1305 vs AES-256-GCM

使用 out-of-place 版本进行对比：

| horizon | size  | operation | AES-256-GCM ms | ChaCha20-Poly1305 ms | ChaCha 相对 AES 耗时降低 | AES/ChaCha 耗时倍数 |
| ------: | :---- | :-------- | -------------: | -------------------: | :----------------------- | :------------------ |
|    1024 | 1 KB  | 加密      |        1.93129 |             0.017104 | 99.11%                   | 112.91x             |
|    1024 | 1 KB  | 解密      |        1.84204 |             0.017144 | 99.07%                   | 107.44x             |
|   65536 | 64 KB | 加密      |        108.729 |              1.04241 | 99.04%                   | 104.31x             |
|   65536 | 64 KB | 解密      |         109.63 |              1.03774 | 99.05%                   | 105.64x             |
| 1048576 | 1 MB  | 加密      |        1682.31 |              16.9537 | 98.99%                   | 99.23x              |
| 1048576 | 1 MB  | 解密      |         1682.2 |              16.8283 | 99.00%                   | 99.96x              |

结论：

- 在本次手写实现中，ChaCha20-Poly1305 明显快于 AES-256-GCM。
- 主要原因是 ChaCha20 核心为加法、异或、循环移位，适合普通软件实现。
- 当前 AES-256-GCM 包含手写 AES 多轮变换和 GHASH 计算，其中 GHASH 使用通用 GF(2^128) 乘法实现，因此开销较大。

### 8.2 ChaCha20-Poly1305 in-place

| horizon | size  | operation | out-of-place ms | in-place ms | 变化   |
| ------: | :---- | :-------- | --------------: | ----------: | :----- |
|    1024 | 1 KB  | 加密      |        0.017104 |     0.01707 | 0.20%  |
|    1024 | 1 KB  | 解密      |        0.017144 |    0.017126 | 0.10%  |
|   65536 | 64 KB | 加密      |         1.04241 |     1.12083 | -7.52% |
|   65536 | 64 KB | 解密      |         1.03774 |     1.10193 | -6.19% |
| 1048576 | 1 MB  | 加密      |         16.9537 |     16.9314 | 0.13%  |
| 1048576 | 1 MB  | 解密      |         16.8283 |      16.863 | -0.21% |

结论：

- ChaCha20-Poly1305 的 in-place 效果不稳定。
- 1 KB 和 1 MB 场景差异很小。
- 64 KB 场景下 in-place 平均耗时变大，可能受到内存访问模式和系统抖动影响。
- 因此，当前手写 ChaCha20-Poly1305 不建议把 in-place 作为主要优化结论。

### 8.3 AES-256-GCM in-place

| horizon | size  | operation | out-of-place ms | in-place ms | 变化   |
| ------: | :---- | :-------- | --------------: | ----------: | :----- |
|    1024 | 1 KB  | 加密      |         1.93129 |     1.78998 | 7.32%  |
|    1024 | 1 KB  | 解密      |         1.84204 |      1.9965 | -8.39% |
|   65536 | 64 KB | 加密      |         108.729 |      114.39 | -5.21% |
|   65536 | 64 KB | 解密      |          109.63 |     113.634 | -3.65% |
| 1048576 | 1 MB  | 加密      |         1682.31 |     1683.18 | -0.05% |
| 1048576 | 1 MB  | 解密      |          1682.2 |     1683.29 | -0.06% |

结论：

- AES-256-GCM 的 in-place 效果也不稳定。
- 1 KB 加密有一定改善，但 1 KB 解密和 64 KB 场景变慢。
- 1 MB 场景差异接近 0，可以认为没有明显收益。
- 当前手写 AES-GCM 的主要瓶颈更可能在 AES 轮函数和 GHASH，而不是输入输出缓冲区复制。

### 8.4 AES-256-GCM in-place + ctx reuse

| horizon | size  | operation | AES out-of-place ms | AES in-place + ctx reuse ms | 变化   |
| ------: | :---- | :-------- | ------------------: | --------------------------: | :----- |
|    1024 | 1 KB  | 加密      |             1.93129 |                     1.86253 | 3.56%  |
|    1024 | 1 KB  | 解密      |             1.84204 |                     1.83519 | 0.37%  |
|   65536 | 64 KB | 加密      |             108.729 |                     116.009 | -6.70% |
|   65536 | 64 KB | 解密      |              109.63 |                     115.751 | -5.58% |
| 1048576 | 1 MB  | 加密      |             1682.31 |                     1684.17 | -0.11% |
| 1048576 | 1 MB  | 解密      |              1682.2 |                     1684.09 | -0.11% |

结论：

- `AES in-place + ctx reuse` 在 1 KB 场景有小幅改善。
- 64 KB 和 1 MB 场景没有收益，甚至略慢。
- 说明在大数据场景下，重复 key expansion 不是主要瓶颈。
- 当前手写 AES-GCM 的主要耗时仍然来自 AES block 处理和 GHASH。

## 9. 总体结论

1. ChaCha20-Poly1305 和 AES-256-GCM 均完成了加密、解密和 tag 校验，输出中 `verify=OK` 表示功能自测通过。
2. 在本次手写实现中，ChaCha20-Poly1305 明显快于 AES-256-GCM。
3. in-place 对两个算法的收益都不稳定，不适合作为当前版本的主要优化结论。
4. AES 的 ctx reuse 当前只在 `in-place + ctx reuse` 组合中测试；结果显示小数据有一定改善，大数据无明显收益。
5. 当前 AES-256-GCM 的性能瓶颈主要来自手写 AES 轮函数和 GHASH 计算。
6. 如果后续继续优化 AES-256-GCM，应优先考虑优化 GHASH 和 AES block 实现，而不是只调整缓冲区模式。

## 10. 后续可改进方向

后续可以考虑：

- 为 GHASH 增加 4-bit 或 8-bit 查表优化。
- 为 AES 实现查表版或更高效的轮函数实现。
- 增加标准测试向量，验证 AES-GCM 和 ChaCha20-Poly1305 与标准输出一致。
- 增加独立 AES ctx reuse 测试，即只开启 `AES_ENABLE_CTX_REUSE`，不同时开启 in-place。
- 增加不同编译等级 `-O0 / -O2 / -O3` 的对比。
