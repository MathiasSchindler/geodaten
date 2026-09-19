/*
 * signal_util.h - signal name matching helpers shared between platform implementations.
 *
 * Both posix and linux platform process.c files use identical case-insensitive signal
 * name matching logic. This header provides those helpers to eliminate the duplication.
 */

#ifndef NEWOS_PLATFORM_COMMON_SIGNAL_UTIL_H
#define NEWOS_PLATFORM_COMMON_SIGNAL_UTIL_H

static char signal_upper_char(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - 'a' + 'A');
    }
    return ch;
}

static int signal_name_matches(const char *input, const char *name) {
    size_t offset = 0;
    size_t index = 0;

    if (input == 0 || name == 0) {
        return 0;
    }

    if (signal_upper_char(input[0]) == 'S' &&
        signal_upper_char(input[1]) == 'I' &&
        signal_upper_char(input[2]) == 'G') {
        offset = 3;
    }

    while (input[offset] != '\0' && name[index] != '\0') {
        if (signal_upper_char(input[offset]) != signal_upper_char(name[index])) {
            return 0;
        }
        offset += 1U;
        index += 1U;
    }

    return input[offset] == '\0' && name[index] == '\0';
}

#endif /* NEWOS_PLATFORM_COMMON_SIGNAL_UTIL_H */
