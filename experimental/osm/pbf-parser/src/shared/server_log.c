#include "server_log.h"

#include "platform.h"
#include "runtime.h"

static size_t server_log_append_char(char *buffer, size_t buffer_size, size_t used, char ch) {
    if (used + 1U < buffer_size) {
        buffer[used++] = ch;
        buffer[used] = '\0';
    }
    return used;
}

static size_t server_log_append_text(char *buffer, size_t buffer_size, size_t used, const char *text) {
    size_t index = 0U;

    while (text != NULL && text[index] != '\0') {
        used = server_log_append_char(buffer, buffer_size, used, text[index]);
        index += 1U;
    }
    return used;
}

static size_t server_log_append_hex_byte(char *buffer, size_t buffer_size, size_t used, unsigned char value) {
    static const char digits[] = "0123456789abcdef";

    used = server_log_append_char(buffer, buffer_size, used, '\\');
    used = server_log_append_char(buffer, buffer_size, used, 'x');
    used = server_log_append_char(buffer, buffer_size, used, digits[(value >> 4) & 0x0fU]);
    used = server_log_append_char(buffer, buffer_size, used, digits[value & 0x0fU]);
    return used;
}

int server_log_escape_text(const char *input, char *buffer, size_t buffer_size) {
    size_t used = 0U;
    size_t index = 0U;

    if (buffer == NULL || buffer_size == 0U) {
        return -1;
    }

    buffer[0] = '\0';
    while (input != NULL && input[index] != '\0') {
        unsigned char ch = (unsigned char)input[index];
        if (ch == '\n') {
            used = server_log_append_text(buffer, buffer_size, used, "\\n");
        } else if (ch == '\r') {
            used = server_log_append_text(buffer, buffer_size, used, "\\r");
        } else if (ch == '\t') {
            used = server_log_append_text(buffer, buffer_size, used, "\\t");
        } else if (ch == '\\') {
            used = server_log_append_text(buffer, buffer_size, used, "\\\\");
        } else if (ch >= 32U && ch <= 126U) {
            used = server_log_append_char(buffer, buffer_size, used, (char)ch);
        } else {
            used = server_log_append_hex_byte(buffer, buffer_size, used, ch);
        }
        index += 1U;
    }

    return 0;
}

int server_log_write(int fd, const char *component, const char *level, const char *message, const char *detail) {
    char line[2048];
    char escaped[1536];
    char time_text[64];
    size_t used = 0U;

    line[0] = '\0';
    if (platform_format_time(platform_get_epoch_time(), 1, "%Y-%m-%d %H:%M:%S", time_text, sizeof(time_text)) == 0) {
        used = server_log_append_text(line, sizeof(line), used, time_text);
        used = server_log_append_char(line, sizeof(line), used, ' ');
    }

    used = server_log_append_char(line, sizeof(line), used, '[');
    used = server_log_append_text(line, sizeof(line), used, level != NULL ? level : "INFO");
    used = server_log_append_text(line, sizeof(line), used, "] ");
    used = server_log_append_text(line, sizeof(line), used, component != NULL ? component : "server");
    used = server_log_append_text(line, sizeof(line), used, ": ");
    used = server_log_append_text(line, sizeof(line), used, message != NULL ? message : "event");

    if (detail != NULL && detail[0] != '\0') {
        if (server_log_escape_text(detail, escaped, sizeof(escaped)) == 0) {
            used = server_log_append_text(line, sizeof(line), used, " - ");
            used = server_log_append_text(line, sizeof(line), used, escaped);
        }
    }

    used = server_log_append_char(line, sizeof(line), used, '\n');
    return rt_write_all(fd, line, used);
}
