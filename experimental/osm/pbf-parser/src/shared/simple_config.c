#include "simple_config.h"

#include "platform.h"
#include "runtime.h"

#define SIMPLE_CONFIG_CAPACITY 8192U

static void simple_config_trim_whitespace(char *text) {
    size_t start = 0U;
    size_t end;
    size_t out = 0U;

    if (text == 0) return;
    end = rt_strlen(text);
    while (text[start] != '\0' && rt_is_space(text[start])) start += 1U;
    while (end > start && rt_is_space(text[end - 1U])) end -= 1U;
    while (start + out < end) {
        text[out] = text[start + out];
        out += 1U;
    }
    text[out] = '\0';
}

static int simple_config_parse_buffer(char *buffer, SimpleConfigVisitor visitor, void *context) {
    char *cursor = buffer;

    if (buffer == NULL || visitor == NULL) {
        return -1;
    }

    while (*cursor != '\0') {
        char *line = cursor;
        char *line_end = cursor;
        char *equals;

        while (*line_end != '\0' && *line_end != '\n') {
            line_end += 1;
        }
        if (*line_end == '\n') {
            *line_end = '\0';
            cursor = line_end + 1;
        } else {
            cursor = line_end;
        }

        simple_config_trim_whitespace(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        equals = line;
        while (*equals != '\0' && *equals != '=') {
            equals += 1;
        }
        if (*equals != '=') {
            return -1;
        }

        *equals = '\0';
        simple_config_trim_whitespace(line);
        simple_config_trim_whitespace(equals + 1);
        if (line[0] == '\0') {
            return -1;
        }

        if (visitor(line, equals + 1, context) != 0) {
            return -1;
        }
    }

    return 0;
}

int simple_config_parse_file(const char *path, SimpleConfigVisitor visitor, void *context) {
    char buffer[SIMPLE_CONFIG_CAPACITY];
    size_t used = 0U;
    int fd;

    if (path == NULL || visitor == NULL) {
        return -1;
    }

    fd = platform_open_read(path);
    if (fd < 0) {
        return -1;
    }

    while (used + 1U < sizeof(buffer)) {
        size_t index;
        long bytes = platform_read(fd, buffer + used, sizeof(buffer) - used - 1U);
        if (bytes < 0) {
            (void)platform_close(fd);
            return -1;
        }
        if (bytes == 0) {
            break;
        }
        for (index = used; index < used + (size_t)bytes; ++index) {
            if (buffer[index] == '\0') {
                (void)platform_close(fd);
                return -1;
            }
        }
        used += (size_t)bytes;
    }
    (void)platform_close(fd);

    if (used + 1U >= sizeof(buffer)) {
        return -1;
    }

    buffer[used] = '\0';
    return simple_config_parse_buffer(buffer, visitor, context);
}
