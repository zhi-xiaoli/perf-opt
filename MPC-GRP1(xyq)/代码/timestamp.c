#include "timestamp.h"

timestamp_t timestamp() {
    timestamp_t t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
}

int64_t timestamp_diff(timestamp_t start, timestamp_t end) {
    int64_t sec_diff = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    int64_t nsec_diff = (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;
    return sec_diff * 1000000000LL + nsec_diff;
}