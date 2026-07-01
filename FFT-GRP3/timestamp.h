#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#include <stdint.h>
#include <time.h>

typedef struct timespec timestamp_t;

timestamp_t timestamp(void);
int64_t timestamp_diff(timestamp_t start, timestamp_t end);

#endif
