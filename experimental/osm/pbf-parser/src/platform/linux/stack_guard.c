#include "common.h"

#include <stdint.h>

unsigned long __stack_chk_guard;

static __attribute__((no_stack_protector))
int newos_read_stack_guard_random(unsigned long *guard) {
    unsigned char *cursor = (unsigned char *)guard;
    unsigned long offset = 0;
    long fd;

    while (offset < (unsigned long)sizeof(*guard)) {
        long bytes = linux_syscall3(
            LINUX_SYS_GETRANDOM,
            (long)(cursor + offset),
            (long)(sizeof(*guard) - offset),
            0
        );

        if (bytes == -LINUX_EINTR) {
            continue;
        }
        if (bytes == -LINUX_ENOSYS || bytes == -LINUX_EINVAL) {
            break;
        }
        if (bytes <= 0) {
            break;
        }
        offset += (unsigned long)bytes;
    }
    if (offset == (unsigned long)sizeof(*guard)) {
        return 0;
    }

    fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)"/dev/urandom", LINUX_O_RDONLY | LINUX_O_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    offset = 0;
    while (offset < (unsigned long)sizeof(*guard)) {
        long bytes = linux_syscall3(LINUX_SYS_READ, fd, (long)(cursor + offset), (long)(sizeof(*guard) - offset));

        if (bytes == -LINUX_EINTR) {
            continue;
        }
        if (bytes <= 0) {
            linux_syscall1(LINUX_SYS_CLOSE, fd);
            return -1;
        }
        offset += (unsigned long)bytes;
    }
    linux_syscall1(LINUX_SYS_CLOSE, fd);
    return 0;
}

__attribute__((noreturn, no_stack_protector))
void __stack_chk_fail(void) {
    static const char message[] = "newos: stack check failure\n";

    (void)linux_syscall3(LINUX_SYS_WRITE, 2, (long)message, (long)(sizeof(message) - 1U));
    linux_syscall1(LINUX_SYS_EXIT, 127);
    for (;;) {
    }
}

__attribute__((no_stack_protector))
void __newos_stack_guard_init(long argc, char **argv) {
    unsigned long guard = 0;
    struct linux_timespec ts;

    if (newos_read_stack_guard_random(&guard) != 0) {
        long pid = linux_syscall0(LINUX_SYS_GETPID);
        long clock_rc = linux_syscall2(LINUX_SYS_CLOCK_GETTIME, 1, (long)&ts);

        guard = 0x9e3779b97f4a7c15UL;
        guard ^= ((unsigned long)pid << 32) ^ (unsigned long)pid;
        guard ^= (unsigned long)(uintptr_t)&guard;
        guard ^= (unsigned long)(uintptr_t)argv;
        guard ^= (unsigned long)argc * 0xbf58476d1ce4e5b9UL;
        if (clock_rc == 0) {
            guard ^= (unsigned long)ts.tv_sec;
            guard ^= (unsigned long)ts.tv_nsec << 17;
        }
    }

    guard &= ~0xffUL;
    if (guard == 0) {
        guard = 0x00f00dfeedface00UL;
    }
    __stack_chk_guard = guard;
}
