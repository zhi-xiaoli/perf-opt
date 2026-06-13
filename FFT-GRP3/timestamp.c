#include "timestamp.h"

timestamp_t timestamp(void)
{
    timestamp_t ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}

int64_t timestamp_diff(timestamp_t start, timestamp_t end)
{
    return (int64_t)(end.tv_sec - start.tv_sec) * 1000000000LL
         + (int64_t)(end.tv_nsec - start.tv_nsec);
}
