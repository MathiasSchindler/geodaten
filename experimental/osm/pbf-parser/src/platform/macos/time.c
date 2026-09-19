#include "platform.h"
#include "syscall.h"

typedef struct {
    long tv_sec;
    int tv_usec;
} DarwinTimeval;

int platform_sleep_milliseconds(unsigned long long milliseconds) {
    DarwinTimeval timeout;

    timeout.tv_sec = (long)(milliseconds / 1000ULL);
    timeout.tv_usec = (int)((milliseconds % 1000ULL) * 1000ULL);
    return darwin_syscall5(DARWIN_SYS_SELECT, 0, 0, 0, 0, (long)&timeout) < 0 ? -1 : 0;
}

int platform_sleep_seconds(unsigned int seconds) {
    return platform_sleep_milliseconds((unsigned long long)seconds * 1000ULL);
}

long long platform_get_epoch_time(void) {
    DarwinTimeval tv;

    if (darwin_syscall2(DARWIN_SYS_GETTIMEOFDAY, (long)&tv, 0) < 0) {
        return -1;
    }
    return (long long)tv.tv_sec;
}

unsigned long long platform_get_monotonic_time_ns(void) {
    DarwinTimeval tv;

    if (darwin_syscall2(DARWIN_SYS_GETTIMEOFDAY, (long)&tv, 0) < 0) {
        return 0ULL;
    }
    return (unsigned long long)tv.tv_sec * 1000000000ULL + (unsigned long long)tv.tv_usec * 1000ULL;
}