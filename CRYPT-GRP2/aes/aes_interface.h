#ifndef CRYPTION_AES_INTERFACE_H
#define CRYPTION_AES_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 保留原工程的三个接口。
 * scale 表示输入数据长度，单位 byte。
 */
int aes_freertos_iopointer(int scale, void *input, void *output);

int aes_freertos_ioself_profiling(int scale);

int aes_freertos_ioself(int scale);

#ifdef __cplusplus
}
#endif

#endif
