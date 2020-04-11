#include "receiver.h"

uint64_t gettime_ns() {
    struct timespec tp;
    int err = clock_gettime(CLOCK_REALTIME, &tp);
    if (err < 0) {
        perror("clock_gettime() failed!");
        exit(-1);
    }
    return ((uint64_t)tp.tv_sec) * 1000000000 + tp.tv_nsec;
}

void settime_ns(uint64_t time_ns) {
    struct timespec tp;
    tp.tv_sec = time_ns / 1000000000;
    tp.tv_nsec = time_ns % 1000000000;
    int err = clock_settime(CLOCK_REALTIME, &tp);
    if (err < 0) {
        perror("clock_settime() failed!");
        exit(-1);
    }
}