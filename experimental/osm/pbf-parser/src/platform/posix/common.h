#ifndef NEWOS_POSIX_COMMON_H
#define NEWOS_POSIX_COMMON_H

#include "runtime.h"

#include <errno.h>
#include <stddef.h>

#define posix_copy_string rt_copy_string
#define posix_is_digit_string rt_is_digit_string
#define posix_parse_pid_value rt_parse_pid_value
#define posix_trim_newline rt_trim_newline

static inline int posix_join_path(const char *dir_path, const char *name, char *buffer, size_t buffer_size) {
    if (rt_join_path(dir_path, name, buffer, buffer_size) != 0) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}

#endif
