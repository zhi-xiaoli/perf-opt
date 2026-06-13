#ifndef CHACHA20_INTERFACE_H
#define CHACHA20_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 保留和原 aes_interface 类似的三接口形式。
 * scale 表示输入数据长度，单位 byte。
 */
int chacha20_freertos_iopointer(int scale, void *input, void *output);

int chacha20_freertos_ioself_profiling(int scale);

int chacha20_freertos_ioself(int scale);

#ifdef __cplusplus
}
#endif

#endif
