#include "runtime.h"
#include "platform.h"

int rt_write_all(int fd, const void *data, size_t count) {
    const unsigned char *cursor = (const unsigned char *)data;
    size_t written = 0;

    while (written < count) {
        long result = platform_write(fd, cursor + written, count - written);
        if (result <= 0) {
            return -1;
        }
        written += (size_t)result;
    }

    return 0;
}

int rt_write_cstr(int fd, const char *text) {
    return rt_write_all(fd, text, rt_strlen(text));
}

int rt_write_line(int fd, const char *text) {
    if (rt_write_cstr(fd, text) != 0) {
        return -1;
    }

    return rt_write_char(fd, '\n');
}

int rt_write_char(int fd, char ch) {
    return rt_write_all(fd, &ch, 1);
}

static size_t format_unsigned_decimal(unsigned long long value, char *buffer) {
    char reverse[32];
    size_t digits = 0;
    size_t i;

    do {
        reverse[digits] = (char)('0' + (value % 10));
        value /= 10;
        digits += 1;
    } while (value != 0);

    for (i = 0; i < digits; ++i) {
        buffer[i] = reverse[digits - i - 1];
    }

    return digits;
}

int rt_write_uint(int fd, unsigned long long value) {
    char buffer[32];
    size_t len = format_unsigned_decimal(value, buffer);
    return rt_write_all(fd, buffer, len);
}

int rt_write_int(int fd, long long value) {
    unsigned long long magnitude;

    if (value < 0) {
        if (rt_write_char(fd, '-') != 0) {
            return -1;
        }
        magnitude = (unsigned long long)(-(value + 1)) + 1;
        return rt_write_uint(fd, magnitude);
    }

    return rt_write_uint(fd, (unsigned long long)value);
}
