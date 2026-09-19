#include "runtime.h"

#include <limits.h>

size_t rt_strlen(const char *text) {
    size_t len = 0;

    while (text[len] != '\0') {
        len += 1;
    }

    return len;
}

int rt_strcmp(const char *lhs, const char *rhs) {
    while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
        lhs += 1;
        rhs += 1;
    }

    return (unsigned char)*lhs - (unsigned char)*rhs;
}

int rt_strncmp(const char *lhs, const char *rhs, size_t count) {
    size_t i = 0;

    while (i < count && lhs[i] != '\0' && rhs[i] != '\0' && lhs[i] == rhs[i]) {
        i += 1U;
    }

    if (i == count) {
        return 0;
    }

    return (unsigned char)lhs[i] - (unsigned char)rhs[i];
}

void rt_copy_string(char *dst, size_t dst_size, const char *src) {
    size_t i = 0;

    if (dst_size == 0) {
        return;
    }

    while (src[i] != '\0' && i + 1 < dst_size) {
        dst[i] = src[i];
        i += 1;
    }

    dst[i] = '\0';
}

int rt_join_path(const char *dir_path, const char *name, char *buffer, size_t buffer_size) {
    size_t dir_len = rt_strlen(dir_path);
    size_t name_len = rt_strlen(name);
    int need_slash = (dir_len > 0 && dir_path[dir_len - 1] != '/');

    if (dir_len + (size_t)need_slash + name_len + 1 > buffer_size) {
        return -1;
    }

    memcpy(buffer, dir_path, dir_len);
    if (need_slash) {
        buffer[dir_len] = '/';
        memcpy(buffer + dir_len + 1, name, name_len + 1);
    } else {
        memcpy(buffer + dir_len, name, name_len + 1);
    }

    return 0;
}

void rt_unsigned_to_string(unsigned long long value, char *buffer, size_t buffer_size) {
    char reverse[32];
    size_t digits = 0;
    size_t i;

    if (buffer_size == 0) {
        return;
    }

    do {
        reverse[digits] = (char)('0' + (value % 10));
        value /= 10;
        digits += 1;
    } while (value != 0 && digits < sizeof(reverse));

    if (digits + 1 > buffer_size) {
        digits = buffer_size - 1;
    }

    for (i = 0; i < digits; ++i) {
        buffer[i] = reverse[digits - i - 1];
    }

    buffer[digits] = '\0';
}

int rt_is_digit_string(const char *text) {
    size_t i = 0;

    if (text == 0 || text[0] == '\0') {
        return 0;
    }

    while (text[i] != '\0') {
        if (text[i] < '0' || text[i] > '9') {
            return 0;
        }
        i += 1;
    }

    return 1;
}

int rt_parse_pid_value(const char *text) {
    unsigned long long value = 0;

    if (text == 0 || rt_parse_uint(text, &value) != 0 || value > (unsigned long long)INT_MAX) {
        return -1;
    }

    return (int)value;
}

void rt_trim_newline(char *text) {
    size_t len = rt_strlen(text);

    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[len - 1] = '\0';
        len -= 1;
    }
}
