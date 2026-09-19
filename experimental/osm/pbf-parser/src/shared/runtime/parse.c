#include "runtime.h"

#include <limits.h>

int rt_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f';
}

int rt_parse_uint(const char *text, unsigned long long *value_out) {
    unsigned long long value = 0;
    size_t i = 0;

    if (text == 0 || text[0] == '\0' || value_out == 0) {
        return -1;
    }

    while (text[i] != '\0') {
        char ch = text[i];

        if (ch < '0' || ch > '9') {
            return -1;
        }

        if (value > (ULLONG_MAX - (unsigned long long)(ch - '0')) / 10ULL) {
            return -1;
        }
        value = (value * 10ULL) + (unsigned long long)(ch - '0');
        i += 1;
    }

    *value_out = value;
    return 0;
}
