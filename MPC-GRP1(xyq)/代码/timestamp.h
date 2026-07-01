#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#include <time.h>
#include <stdint.h>

// 使用 struct timespec 作为时间戳类型，支持纳秒精度
typedef struct timespec timestamp_t;

timestamp_t timestamp();
int64_t timestamp_diff(timestamp_t start, timestamp_t end);

#endif