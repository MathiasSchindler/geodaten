#include "platform.h"
#include "common.h"

long platform_write(int fd, const void *buffer, size_t count) {
    return linux_syscall3(LINUX_SYS_WRITE, fd, (long)buffer, (long)count);
}

long platform_read(int fd, void *buffer, size_t count) {
    return linux_syscall3(LINUX_SYS_READ, fd, (long)buffer, (long)count);
}

void *platform_allocate_pages(size_t size) {
    long mapped = linux_syscall6(
        LINUX_SYS_MMAP,
        0,
        (long)size,
        LINUX_PROT_READ | LINUX_PROT_WRITE,
        LINUX_MAP_PRIVATE | LINUX_MAP_ANONYMOUS,
        -1,
        0
    );
    return mapped < 0 ? 0 : (void *)mapped;
}

int platform_open_read(const char *path) {
    long fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return 0;
    }

    fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)path, LINUX_O_RDONLY | LINUX_O_CLOEXEC, 0);
    return fd < 0 ? -1 : (int)fd;
}

int platform_open_write_mode(const char *path, unsigned int mode, int truncate_existing) {
    long fd;
    long flags = LINUX_O_WRONLY | LINUX_O_CREAT | LINUX_O_CLOEXEC;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return 1;
    }

    if (truncate_existing) {
        flags |= LINUX_O_TRUNC;
    }

    fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)path, flags, (long)mode);
    return fd < 0 ? -1 : (int)fd;
}

int platform_open_write(const char *path, unsigned int mode) {
    return platform_open_write_mode(path, mode, 1);
}

int platform_open_create_exclusive(const char *path, unsigned int mode) {
    long fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return -1;
    }

    fd = linux_syscall4(
        LINUX_SYS_OPENAT,
        LINUX_AT_FDCWD,
        (long)path,
        LINUX_O_WRONLY | LINUX_O_CREAT | LINUX_O_EXCL | LINUX_O_CLOEXEC,
        (long)mode
    );
    return fd < 0 ? -1 : (int)fd;
}

int platform_open_append(const char *path, unsigned int mode) {
    long fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return 1;
    }

    fd = linux_syscall4(
        LINUX_SYS_OPENAT,
        LINUX_AT_FDCWD,
        (long)path,
        LINUX_O_WRONLY | LINUX_O_CREAT | LINUX_O_APPEND | LINUX_O_CLOEXEC,
        (long)mode
    );
    return fd < 0 ? -1 : (int)fd;
}

int platform_open_append_existing(const char *path) {
    long fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return 1;
    }

    fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)path, LINUX_O_WRONLY | LINUX_O_APPEND | LINUX_O_CLOEXEC, 0);
    return fd < 0 ? -1 : (int)fd;
}

long long platform_seek(int fd, long long offset, int whence) {
    long result = linux_syscall3(LINUX_SYS_LSEEK, fd, (long)offset, whence);
    return result < 0 ? -1 : (long long)result;
}

int platform_close(int fd) {
    if (fd == 0 || fd == 1) {
        return 0;
    }

    return linux_syscall1(LINUX_SYS_CLOSE, fd) < 0 ? -1 : 0;
}