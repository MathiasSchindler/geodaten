#if defined(__linux__) && defined(__STDC_HOSTED__) && __STDC_HOSTED__
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#if defined(__linux__) && defined(__STDC_HOSTED__) && __STDC_HOSTED__
#define NEWOS_PROFILER_USE_LIBC 1
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#elif defined(__linux__)
#include "../platform/linux/common.h"
#endif

#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define NEWOS_PROFILER_NOINSTR __attribute__((no_instrument_function))
#else
#define NEWOS_PROFILER_NOINSTR
#endif

#define NEWOS_PROFILE_BUFFER_SIZE 65536U

static int newos_profile_fd = -2;
static int newos_profile_initialized;
static int newos_profile_disabled;
static unsigned int newos_profile_depth;
static size_t newos_profile_buffer_length;
static char newos_profile_buffer[NEWOS_PROFILE_BUFFER_SIZE];

static int newos_profile_text_is_disabled(const char *text) NEWOS_PROFILER_NOINSTR;
static int newos_profile_streq(const char *left, const char *right) NEWOS_PROFILER_NOINSTR;
#if !defined(NEWOS_PROFILER_USE_LIBC) && defined(__linux__)
static int newos_profile_env_name_matches(const char *entry, const char *name) NEWOS_PROFILER_NOINSTR;
#endif
static const char *newos_profile_getenv(const char *name) NEWOS_PROFILER_NOINSTR;
static int newos_profile_open_write(const char *path) NEWOS_PROFILER_NOINSTR;
static long newos_profile_write(int fd, const void *buffer, size_t size) NEWOS_PROFILER_NOINSTR;
static unsigned long long newos_profile_time_ns(void) NEWOS_PROFILER_NOINSTR;
static void newos_profile_initialize(void) NEWOS_PROFILER_NOINSTR;
static void newos_profile_flush(void) NEWOS_PROFILER_NOINSTR;
static void newos_profile_append_char(char ch) NEWOS_PROFILER_NOINSTR;
static void newos_profile_append_cstr(const char *text) NEWOS_PROFILER_NOINSTR;
static void newos_profile_append_uint(unsigned long long value) NEWOS_PROFILER_NOINSTR;
static void newos_profile_append_hex(unsigned long long value) NEWOS_PROFILER_NOINSTR;
static void newos_profile_event(const char *kind, void *function_address) NEWOS_PROFILER_NOINSTR;

static int newos_profile_text_is_disabled(const char *text) {
    return text == 0 || text[0] == '\0' ||
           newos_profile_streq(text, "0") ||
           newos_profile_streq(text, "off") ||
           newos_profile_streq(text, "false") ||
           newos_profile_streq(text, "no");
}

static int newos_profile_streq(const char *left, const char *right) {
    size_t index = 0U;

    if (left == 0 || right == 0) {
        return left == right;
    }
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }
        index += 1U;
    }
    return left[index] == right[index];
}

#if !defined(NEWOS_PROFILER_USE_LIBC) && defined(__linux__)
static int newos_profile_env_name_matches(const char *entry, const char *name) {
    size_t index = 0U;

    while (name[index] != '\0') {
        if (entry[index] != name[index]) {
            return 0;
        }
        index += 1U;
    }
    return entry[index] == '=';
}
#endif

static const char *newos_profile_getenv(const char *name) {
#if defined(NEWOS_PROFILER_USE_LIBC)
    return getenv(name);
#elif defined(__linux__)
    static char env_buffer[8192];
    static char value_buffer[512];
    long fd;
    long bytes;
    size_t start = 0U;
    size_t index;

    fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)"/proc/self/environ", LINUX_O_RDONLY, 0);
    if (fd < 0) {
        return 0;
    }
    bytes = linux_syscall3(LINUX_SYS_READ, fd, (long)env_buffer, (long)(sizeof(env_buffer) - 1U));
    linux_syscall1(LINUX_SYS_CLOSE, fd);
    if (bytes <= 0) {
        return 0;
    }
    env_buffer[bytes] = '\0';
    for (index = 0U; index <= (size_t)bytes; ++index) {
        if (env_buffer[index] == '\0') {
            if (index > start && newos_profile_env_name_matches(env_buffer + start, name)) {
                size_t name_length = 0U;
                size_t out = 0U;

                while (name[name_length] != '\0') {
                    name_length += 1U;
                }
                start += name_length + 1U;
                while (start < index && out + 1U < sizeof(value_buffer)) {
                    value_buffer[out++] = env_buffer[start++];
                }
                value_buffer[out] = '\0';
                return value_buffer;
            }
            start = index + 1U;
        }
    }
#else
    (void)name;
#endif
    return 0;
}

static int newos_profile_open_write(const char *path) {
#if defined(NEWOS_PROFILER_USE_LIBC)
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
#elif defined(__linux__)
    return (int)linux_syscall4(LINUX_SYS_OPENAT,
                               LINUX_AT_FDCWD,
                               (long)path,
                               LINUX_O_WRONLY | LINUX_O_CREAT | LINUX_O_TRUNC,
                               0644);
#else
    (void)path;
    return -1;
#endif
}

static long newos_profile_write(int fd, const void *buffer, size_t size) {
#if defined(NEWOS_PROFILER_USE_LIBC)
    return (long)write(fd, buffer, size);
#elif defined(__linux__)
    return linux_syscall3(LINUX_SYS_WRITE, fd, (long)buffer, (long)size);
#else
    (void)fd;
    (void)buffer;
    (void)size;
    return -1;
#endif
}

static unsigned long long newos_profile_time_ns(void) {
#if defined(NEWOS_PROFILER_USE_LIBC)
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0ULL;
    }
    return ((unsigned long long)now.tv_sec * 1000000000ULL) + (unsigned long long)now.tv_nsec;
#elif defined(__linux__)
    struct linux_timespec now;

    if (linux_syscall2(LINUX_SYS_CLOCK_GETTIME, 1, (long)&now) < 0) {
        return 0ULL;
    }
    return ((unsigned long long)now.tv_sec * 1000000000ULL) + (unsigned long long)now.tv_nsec;
#else
    return 0ULL;
#endif
}

static void newos_profile_initialize(void) {
    const char *path;

    if (newos_profile_initialized) {
        return;
    }
    newos_profile_initialized = 1;
    path = newos_profile_getenv("NEWOS_PROFILE");
    if (newos_profile_text_is_disabled(path)) {
        newos_profile_disabled = 1;
        newos_profile_fd = -1;
        return;
    }
    newos_profile_fd = newos_profile_open_write(path);
    if (newos_profile_fd < 0) {
        newos_profile_disabled = 1;
    }
}

static void newos_profile_flush(void) {
    size_t written = 0U;

    if (newos_profile_fd < 0 || newos_profile_buffer_length == 0U) {
        return;
    }
    while (written < newos_profile_buffer_length) {
        long chunk = newos_profile_write(newos_profile_fd,
                                         newos_profile_buffer + written,
                                         newos_profile_buffer_length - written);
        if (chunk <= 0) {
            newos_profile_disabled = 1;
            newos_profile_buffer_length = 0U;
            return;
        }
        written += (size_t)chunk;
    }
    newos_profile_buffer_length = 0U;
}

static void newos_profile_append_char(char ch) {
    if (newos_profile_buffer_length >= sizeof(newos_profile_buffer)) {
        newos_profile_flush();
    }
    if (newos_profile_buffer_length < sizeof(newos_profile_buffer)) {
        newos_profile_buffer[newos_profile_buffer_length++] = ch;
    }
}

static void newos_profile_append_cstr(const char *text) {
    while (text != 0 && *text != '\0') {
        newos_profile_append_char(*text++);
    }
}

static void newos_profile_append_uint(unsigned long long value) {
    char digits[32];
    size_t count = 0U;

    if (value == 0ULL) {
        newos_profile_append_char('0');
        return;
    }
    while (value != 0ULL && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10ULL));
        value /= 10ULL;
    }
    while (count > 0U) {
        newos_profile_append_char(digits[--count]);
    }
}

static void newos_profile_append_hex(unsigned long long value) {
    static const char hex_digits[] = "0123456789abcdef";
    char digits[32];
    size_t count = 0U;

    newos_profile_append_cstr("0x");
    if (value == 0ULL) {
        newos_profile_append_char('0');
        return;
    }
    while (value != 0ULL && count < sizeof(digits)) {
        digits[count++] = hex_digits[value & 0xfULL];
        value >>= 4U;
    }
    while (count > 0U) {
        newos_profile_append_char(digits[--count]);
    }
}

static void newos_profile_event(const char *kind, void *function_address) {
    if (!newos_profile_initialized) {
        newos_profile_initialize();
    }
    if (newos_profile_disabled || newos_profile_fd < 0) {
        return;
    }
    newos_profile_append_cstr(kind);
    newos_profile_append_char(' ');
    newos_profile_append_uint(newos_profile_time_ns());
    newos_profile_append_char(' ');
    newos_profile_append_hex((unsigned long long)(size_t)function_address);
    newos_profile_append_char('\n');
}

void __cyg_profile_func_enter(void *this_fn, void *call_site) NEWOS_PROFILER_NOINSTR;
void __cyg_profile_func_exit(void *this_fn, void *call_site) NEWOS_PROFILER_NOINSTR;

void __cyg_profile_func_enter(void *this_fn, void *call_site) {
    (void)call_site;
    newos_profile_depth += 1U;
    newos_profile_event("enter", this_fn);
}

void __cyg_profile_func_exit(void *this_fn, void *call_site) {
    (void)call_site;
    newos_profile_event("exit", this_fn);
    if (newos_profile_depth > 0U) {
        newos_profile_depth -= 1U;
    }
    if (newos_profile_depth == 0U) {
        newos_profile_flush();
    }
}
