#include "platform.h"
#include "runtime.h"

#ifndef UINT8_C
#define UINT8_C(value) value
#endif
#ifndef UINT16_C
#define UINT16_C(value) value
#endif
#ifndef UINT32_C
#define UINT32_C(value) value##U
#endif
#ifndef UINT64_C
#define UINT64_C(value) value##ULL
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

long platform_write(int fd, const void *buffer, size_t count) {
    return (long)write(fd, buffer, count);
}

long platform_read(int fd, void *buffer, size_t count) {
    return (long)read(fd, buffer, count);
}

void *platform_allocate_pages(size_t size) {
    return malloc(size);
}

int platform_open_read(const char *path) {
    return open(path, O_RDONLY);
}

int platform_open_write(const char *path, unsigned int mode) {
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)mode);
}

int platform_open_write_mode(const char *path, unsigned int mode, int truncate_existing) {
    int flags = O_WRONLY | O_CREAT;
    if (truncate_existing) flags |= O_TRUNC;
    return open(path, flags, (mode_t)mode);
}

int platform_open_append(const char *path, unsigned int mode) {
    return open(path, O_WRONLY | O_CREAT | O_APPEND, (mode_t)mode);
}

int platform_open_append_existing(const char *path) {
    return open(path, O_WRONLY | O_APPEND);
}

long long platform_seek(int fd, long long offset, int whence) {
    int native_whence = SEEK_SET;
    off_t result;
    if (whence == PLATFORM_SEEK_CUR) native_whence = SEEK_CUR;
    else if (whence == PLATFORM_SEEK_END) native_whence = SEEK_END;
    result = lseek(fd, (off_t)offset, native_whence);
    return result < 0 ? -1 : (long long)result;
}

int platform_close(int fd) {
    return close(fd);
}

int platform_remove_file(const char *path) {
    return unlink(path);
}

int platform_make_directory(const char *path, unsigned int mode) {
    return mkdir(path, (mode_t)mode);
}

const char *platform_getenv(const char *name) {
    return getenv(name);
}

int platform_isatty(int fd) {
    return isatty(fd);
}

long long platform_get_epoch_time(void) {
    return (long long)time(0);
}

unsigned long long platform_get_monotonic_time_ns(void) {
    struct timeval tv;
    if (gettimeofday(&tv, 0) != 0) return 0ULL;
    return (unsigned long long)tv.tv_sec * 1000000000ULL + (unsigned long long)tv.tv_usec * 1000ULL;
}

int platform_sleep_milliseconds(unsigned long long milliseconds) {
    (void)milliseconds;
    return 0;
}

int platform_sleep_seconds(unsigned int seconds) {
    (void)seconds;
    return 0;
}

int platform_random_bytes(unsigned char *buffer, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index) buffer[index] = (unsigned char)(rand() & 0xff);
    return 0;
}

int platform_create_temp_file(char *path_buffer, size_t buffer_size, const char *prefix, unsigned int mode) {
    const char *actual_prefix = prefix != 0 ? prefix : "/tmp/newos";
    size_t prefix_len = rt_strlen(actual_prefix);
    int fd;
    (void)mode;
    if (prefix_len + 12U >= buffer_size) return -1;
    memcpy(path_buffer, actual_prefix, prefix_len);
    memcpy(path_buffer + prefix_len, "XXXXXX", 7U);
    fd = mkstemp(path_buffer);
    return fd;
}

int platform_spawn_process(char *const argv[], int stdin_fd, int stdout_fd, const char *input_path, const char *output_path, int output_append, int *pid_out) {
    (void)argv;
    (void)stdin_fd;
    (void)stdout_fd;
    (void)input_path;
    (void)output_path;
    (void)output_append;
    (void)pid_out;
    errno = ENOSYS;
    return -1;
}

int platform_wait_process(int pid, int *exit_status_out) {
    (void)pid;
    if (exit_status_out != 0) *exit_status_out = 127;
    errno = ENOSYS;
    return -1;
}
