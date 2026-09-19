/*
 * proc_util.h - /proc file parsing helpers shared between platform implementations.
 *
 * Both posix and linux platform time.c files need identical logic for reading
 * /proc/meminfo and /proc/loadavg. This header provides those helpers as static
 * functions so each translation unit gets its own copy without duplication in source.
 */

#ifndef NEWOS_PLATFORM_COMMON_PROC_UTIL_H
#define NEWOS_PLATFORM_COMMON_PROC_UTIL_H

#include "platform.h"
#include "runtime.h"

static int read_text_file(const char *path, char *buffer, size_t buffer_size) {
    int fd;
    size_t used = 0;

    if (buffer == 0 || buffer_size == 0) {
        return -1;
    }

    fd = platform_open_read(path);
    if (fd < 0) {
        buffer[0] = '\0';
        return -1;
    }

    while (used + 1U < buffer_size) {
        long bytes_read = platform_read(fd, buffer + used, buffer_size - used - 1U);
        if (bytes_read < 0) {
            platform_close(fd);
            buffer[0] = '\0';
            return -1;
        }
        if (bytes_read == 0) {
            break;
        }
        used += (size_t)bytes_read;
    }

    buffer[used] = '\0';
    platform_close(fd);
    return 0;
}

static int parse_unsigned_prefix(const char *text, unsigned long long *value_out) {
    unsigned long long value = 0;
    size_t index = 0;

    if (text == 0 || value_out == 0 || text[0] < '0' || text[0] > '9') {
        return -1;
    }

    while (text[index] >= '0' && text[index] <= '9') {
        value = (value * 10ULL) + (unsigned long long)(text[index] - '0');
        index += 1U;
    }

    *value_out = value;
    return 0;
}

static int match_field_name(const char *text, const char *field_name) {
    size_t index = 0;

    while (field_name[index] != '\0') {
        if (text[index] != field_name[index]) {
            return 0;
        }
        index += 1U;
    }

    return text[index] == ':';
}

static int parse_meminfo_field(const char *contents, const char *field_name, unsigned long long *value_out) {
    const char *cursor = contents;

    while (cursor != 0 && *cursor != '\0') {
        if (match_field_name(cursor, field_name)) {
            unsigned long long kibibytes = 0;

            cursor += rt_strlen(field_name) + 1U;
            while (*cursor == ' ' || *cursor == '\t') {
                cursor += 1;
            }

            if (parse_unsigned_prefix(cursor, &kibibytes) != 0) {
                return -1;
            }

            *value_out = kibibytes * 1024ULL;
            return 0;
        }

        while (*cursor != '\0' && *cursor != '\n') {
            cursor += 1;
        }
        if (*cursor == '\n') {
            cursor += 1;
        }
    }

    return -1;
}

static void clear_memory_info(PlatformMemoryInfo *info_out) {
    info_out->total_bytes = 0;
    info_out->free_bytes = 0;
    info_out->available_bytes = 0;
    info_out->shared_bytes = 0;
    info_out->buffer_bytes = 0;
    info_out->cache_bytes = 0;
    info_out->swap_total_bytes = 0;
    info_out->swap_free_bytes = 0;
}

static int read_loadavg_text(char *buffer, size_t buffer_size) {
    char contents[128];
    size_t index = 0;
    size_t length = 0;
    int fields = 0;

    if (buffer == 0 || buffer_size == 0) {
        return -1;
    }

    if (read_text_file("/proc/loadavg", contents, sizeof(contents)) != 0) {
        return -1;
    }

    buffer[0] = '\0';
    while (contents[index] != '\0' && fields < 3) {
        while (rt_is_space(contents[index])) {
            index += 1U;
        }
        if (contents[index] == '\0') {
            break;
        }
        if (fields > 0 && length + 1U < buffer_size) {
            buffer[length++] = ' ';
        }
        while (contents[index] != '\0' && !rt_is_space(contents[index])) {
            if (length + 1U < buffer_size) {
                buffer[length++] = contents[index];
            }
            index += 1U;
        }
        fields += 1;
    }
    buffer[length < buffer_size ? length : buffer_size - 1U] = '\0';

    return fields == 3 ? 0 : -1;
}

#endif /* NEWOS_PLATFORM_COMMON_PROC_UTIL_H */
