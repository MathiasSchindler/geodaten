#include "platform.h"
#include "runtime.h"
#include "tool_util.h"

static int tool_global_color_mode = TOOL_COLOR_AUTO;

static int env_value_is_nonzero(const char *value) {
    return value != 0 && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
}

static const char *tool_text_style_code(int style) {
    switch (style) {
        case TOOL_STYLE_BOLD: return "1";
        case TOOL_STYLE_RED: return "31";
        case TOOL_STYLE_GREEN: return "32";
        case TOOL_STYLE_YELLOW: return "33";
        case TOOL_STYLE_BLUE: return "34";
        case TOOL_STYLE_MAGENTA: return "35";
        case TOOL_STYLE_CYAN: return "36";
        case TOOL_STYLE_BOLD_RED: return "1;31";
        case TOOL_STYLE_BOLD_GREEN: return "1;32";
        case TOOL_STYLE_BOLD_YELLOW: return "1;33";
        case TOOL_STYLE_BOLD_BLUE: return "1;34";
        case TOOL_STYLE_BOLD_MAGENTA: return "1;35";
        case TOOL_STYLE_BOLD_CYAN: return "1;36";
        default: return "";
    }
}

int tool_parse_color_mode(const char *text, int *mode_out) {
    if (text == 0 || mode_out == 0) {
        return -1;
    }

    if (rt_strcmp(text, "always") == 0 || rt_strcmp(text, "yes") == 0 || rt_strcmp(text, "force") == 0) {
        *mode_out = TOOL_COLOR_ALWAYS;
        return 0;
    }
    if (rt_strcmp(text, "auto") == 0) {
        *mode_out = TOOL_COLOR_AUTO;
        return 0;
    }
    if (rt_strcmp(text, "never") == 0 || rt_strcmp(text, "none") == 0 || rt_strcmp(text, "no") == 0) {
        *mode_out = TOOL_COLOR_NEVER;
        return 0;
    }

    return -1;
}

void tool_set_global_color_mode(int mode) {
    tool_global_color_mode = mode;
}

int tool_get_global_color_mode(void) {
    return tool_global_color_mode;
}

int tool_should_use_color_fd(int fd, int mode) {
    const char *term;
    const char *no_color;
    const char *clicolor;
    const char *clicolor_force;

    if (mode == TOOL_COLOR_ALWAYS) {
        return 1;
    }
    if (mode == TOOL_COLOR_NEVER) {
        return 0;
    }

    clicolor_force = platform_getenv("CLICOLOR_FORCE");
    if (env_value_is_nonzero(clicolor_force)) {
        return 1;
    }

    no_color = platform_getenv("NO_COLOR");
    if (no_color != 0) {
        return 0;
    }

    clicolor = platform_getenv("CLICOLOR");
    if (clicolor != 0 && clicolor[0] == '0' && clicolor[1] == '\0') {
        return 0;
    }

    term = platform_getenv("TERM");
    if (term != 0 && rt_strcmp(term, "dumb") == 0) {
        return 0;
    }

    return platform_isatty(fd) != 0;
}

void tool_style_begin(int fd, int mode, int style) {
    const char *code;

    if (!tool_should_use_color_fd(fd, mode) || style == TOOL_STYLE_PLAIN) {
        return;
    }

    code = tool_text_style_code(style);
    if (code[0] == '\0') {
        return;
    }

    rt_write_cstr(fd, "\033[");
    rt_write_cstr(fd, code);
    rt_write_char(fd, 'm');
}

void tool_style_end(int fd, int mode) {
    if (tool_should_use_color_fd(fd, mode)) {
        rt_write_cstr(fd, "\033[0m");
    }
}

void tool_write_styled(int fd, int mode, int style, const char *text) {
    int use_style;

    if (text == 0) {
        return;
    }
    use_style = tool_should_use_color_fd(fd, mode) && style != TOOL_STYLE_PLAIN;
    if (use_style) {
        tool_style_begin(fd, mode, style);
    }
    rt_write_cstr(fd, text);
    if (use_style) {
        tool_style_end(fd, mode);
    }
}

void tool_write_usage(const char *program_name, const char *usage_suffix) {
    if (tool_json_is_enabled()) {
        (void)tool_json_write_usage(program_name, usage_suffix);
        return;
    }
    tool_write_styled(2, tool_get_global_color_mode(), TOOL_STYLE_BOLD_CYAN, "Usage:");
    rt_write_char(2, ' ');
    rt_write_cstr(2, program_name);
    if (usage_suffix != 0 && usage_suffix[0] != '\0') {
        rt_write_char(2, ' ');
        rt_write_cstr(2, usage_suffix);
    }
    rt_write_char(2, '\n');
}

void tool_write_error(const char *tool_name, const char *message, const char *detail) {
    if (tool_json_is_enabled()) {
        (void)tool_json_write_diagnostic(tool_name, "error", message, detail);
        return;
    }
    tool_write_styled(2, tool_get_global_color_mode(), TOOL_STYLE_BOLD_RED, tool_name);
    tool_write_styled(2, tool_get_global_color_mode(), TOOL_STYLE_BOLD_RED, ": ");
    if (message != 0) {
        rt_write_cstr(2, message);
    }
    if (detail != 0) {
        rt_write_cstr(2, detail);
    }
    rt_write_char(2, '\n');
}

int tool_write_visible(int fd, const char *text, size_t length) {
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (text == 0) {
        return 0;
    }

    for (i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)text[i];

        if (ch == '\n') {
            if (rt_write_cstr(fd, "\\n") != 0) {
                return -1;
            }
        } else if (ch == '\r') {
            if (rt_write_cstr(fd, "\\r") != 0) {
                return -1;
            }
        } else if (ch == '\t') {
            if (rt_write_cstr(fd, "\\t") != 0) {
                return -1;
            }
        } else if (ch < 0x20U || ch == 0x7fU) {
            char escaped[4];

            escaped[0] = '\\';
            escaped[1] = 'x';
            escaped[2] = hex[(ch >> 4) & 0x0fU];
            escaped[3] = hex[ch & 0x0fU];
            if (rt_write_all(fd, escaped, sizeof(escaped)) != 0) {
                return -1;
            }
        } else if (rt_write_char(fd, (char)ch) != 0) {
            return -1;
        }
    }

    return 0;
}

int tool_write_visible_line(int fd, const char *text) {
    size_t length = text != 0 ? rt_strlen(text) : 0U;

    if (tool_write_visible(fd, text, length) != 0) {
        return -1;
    }
    return rt_write_char(fd, '\n');
}

void tool_output_buffer_init(ToolOutputBuffer *output, int fd) {
    output->fd = fd;
    output->length = 0U;
}

int tool_output_buffer_flush(ToolOutputBuffer *output) {
    if (output->length == 0U) return 0;
    if (rt_write_all(output->fd, output->buffer, output->length) != 0) return -1;
    output->length = 0U;
    return 0;
}

int tool_output_buffer_write(ToolOutputBuffer *output, const char *text, size_t length) {
    size_t copied = 0U;
    if (length == 0U) return 0;
    if (text == 0) return -1;
    if (length >= sizeof(output->buffer)) {
        if (tool_output_buffer_flush(output) != 0) return -1;
        return rt_write_all(output->fd, text, length);
    }
    while (copied < length) {
        size_t available = sizeof(output->buffer) - output->length;
        size_t chunk;
        if (available == 0U) {
            if (tool_output_buffer_flush(output) != 0) return -1;
            available = sizeof(output->buffer);
        }
        chunk = length - copied;
        if (chunk > available) chunk = available;
        memcpy(output->buffer + output->length, text + copied, chunk);
        output->length += chunk;
        copied += chunk;
    }
    return 0;
}

int tool_output_buffer_write_char(ToolOutputBuffer *output, char ch) {
    return tool_output_buffer_write(output, &ch, 1U);
}

int tool_output_buffer_write_cstr(ToolOutputBuffer *output, const char *text) {
    return tool_output_buffer_write(output, text, rt_strlen(text));
}

void tool_record_reader_init(ToolRecordReader *reader, int fd, char delimiter) {
    reader->fd = fd;
    reader->delimiter = delimiter;
    reader->chunk_len = 0;
    reader->chunk_pos = 0;
    reader->eof = 0;
}

int tool_record_reader_next(ToolRecordReader *reader, char *record, size_t record_size, int *has_record_out) {
    size_t length = 0U;

    if (record_size == 0U || has_record_out == 0) {
        return -1;
    }

    while (!reader->eof) {
        if (reader->chunk_pos >= reader->chunk_len) {
            reader->chunk_len = platform_read(reader->fd, reader->chunk, sizeof(reader->chunk));
            reader->chunk_pos = 0;
            if (reader->chunk_len < 0) {
                return -1;
            }
            if (reader->chunk_len == 0) {
                reader->eof = 1;
                break;
            }
        }

        while (reader->chunk_pos < reader->chunk_len) {
            char ch = reader->chunk[reader->chunk_pos++];
            if (ch == reader->delimiter) {
                record[length] = '\0';
                *has_record_out = 1;
                return 0;
            }
            if (length + 1U >= record_size) {
                return -1;
            }
            record[length++] = ch;
        }
    }

    if (length > 0U) {
        record[length] = '\0';
        *has_record_out = 1;
        return 0;
    }

    *has_record_out = 0;
    return 0;
}

int tool_parse_escaped_string(const char *text, char *buffer, size_t buffer_size, size_t *length_out) {
    size_t in_index = 0;
    size_t out_index = 0;

    if (text == 0 || buffer == 0 || buffer_size == 0) {
        return -1;
    }

    while (text[in_index] != '\0') {
        char ch = text[in_index];

        if (ch == '\\' && text[in_index + 1U] != '\0') {
            in_index += 1U;
            ch = text[in_index];
            if (ch == 'n') {
                ch = '\n';
            } else if (ch == 'r') {
                ch = '\r';
            } else if (ch == 't') {
                ch = '\t';
            } else if (ch == 'f') {
                ch = '\f';
            } else if (ch == 'v') {
                ch = '\v';
            } else if (ch == '0') {
                ch = '\0';
            }
        }

        if (out_index + 1U >= buffer_size) {
            buffer[buffer_size - 1U] = '\0';
            return -1;
        }

        buffer[out_index++] = ch;
        in_index += 1U;
    }

    buffer[out_index] = '\0';
    if (length_out != 0) {
        *length_out = out_index;
    }
    return 0;
}

int tool_prompt_yes_no(const char *message, const char *path) {
    char ch = '\0';
    char answer = '\0';
    long bytes_read;

    if (message != 0) {
        rt_write_cstr(2, message);
    }
    if (path != 0) {
        rt_write_cstr(2, path);
    }
    rt_write_cstr(2, "? ");

    for (;;) {
        bytes_read = platform_read(0, &ch, 1U);
        if (bytes_read <= 0) {
            return 0;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            break;
        }
        if (answer == '\0' && ch != ' ' && ch != '\t') {
            answer = ch;
        }
    }

    return answer == 'y' || answer == 'Y';
}

static size_t tool_buffer_append_char(char *buffer, size_t buffer_size, size_t length, char ch) {
    if (buffer_size == 0) {
        return 0;
    }

    if (length + 1 < buffer_size) {
        buffer[length] = ch;
        length += 1U;
        buffer[length] = '\0';
    } else {
        buffer[buffer_size - 1U] = '\0';
    }

    return length;
}

static size_t tool_buffer_append_cstr(char *buffer, size_t buffer_size, size_t length, const char *text) {
    size_t i = 0;

    while (text != 0 && text[i] != '\0') {
        length = tool_buffer_append_char(buffer, buffer_size, length, text[i]);
        i += 1U;
    }

    return length;
}

void tool_format_size(unsigned long long value, int human_readable, char *buffer, size_t buffer_size) {
    static const char units[] = { 'B', 'K', 'M', 'G', 'T', 'P' };
    size_t unit_index = 0;
    unsigned long long scaled = value;
    unsigned long long remainder = 0;
    char digits[32];
    size_t length = 0;

    if (buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    if (!human_readable) {
        rt_unsigned_to_string(value, buffer, buffer_size);
        return;
    }

    while (scaled >= 1024ULL && unit_index + 1U < sizeof(units)) {
        remainder = scaled % 1024ULL;
        scaled /= 1024ULL;
        unit_index += 1U;
    }

    rt_unsigned_to_string(scaled, digits, sizeof(digits));
    length = tool_buffer_append_cstr(buffer, buffer_size, length, digits);

    if (unit_index > 0U && scaled < 10ULL && remainder != 0ULL) {
        unsigned long long tenths = (remainder * 10ULL) / 1024ULL;
        length = tool_buffer_append_char(buffer, buffer_size, length, '.');
        length = tool_buffer_append_char(buffer, buffer_size, length, (char)('0' + (tenths > 9ULL ? 9ULL : tenths)));
    }

    (void)tool_buffer_append_char(buffer, buffer_size, length, units[unit_index]);
}

int tool_parse_uint_arg(const char *text, unsigned long long *value_out, const char *tool_name, const char *what) {
    if (text == 0 || rt_parse_uint(text, value_out) != 0) {
        tool_write_error(tool_name, "invalid ", what);
        return -1;
    }
    return 0;
}

int tool_parse_int_arg(const char *text, long long *value_out, const char *tool_name, const char *what) {
    unsigned long long magnitude = 0;
    int negative = 0;

    if (text == 0 || value_out == 0 || text[0] == '\0') {
        tool_write_error(tool_name, "invalid ", what);
        return -1;
    }

    if (text[0] == '-') {
        negative = 1;
        text += 1;
    } else if (text[0] == '+') {
        text += 1;
    }

    if (text[0] == '\0' || rt_parse_uint(text, &magnitude) != 0) {
        tool_write_error(tool_name, "invalid ", what);
        return -1;
    }

    *value_out = negative ? -(long long)magnitude : (long long)magnitude;
    return 0;
}

int tool_parse_duration_ms(const char *text, unsigned long long *milliseconds_out) {
    unsigned long long whole = 0;
    unsigned long long fraction = 0;
    unsigned long long divisor = 1;
    unsigned long long unit_ms = 1000ULL;
    int saw_digit = 0;
    int saw_fraction = 0;

    if (text == 0 || text[0] == '\0' || milliseconds_out == 0) {
        return -1;
    }

    while (*text >= '0' && *text <= '9') {
        saw_digit = 1;
        whole = (whole * 10ULL) + (unsigned long long)(*text - '0');
        text += 1;
    }

    if (*text == '.') {
        text += 1;
        while (*text >= '0' && *text <= '9') {
            saw_digit = 1;
            saw_fraction = 1;
            if (divisor < 1000000ULL) {
                fraction = (fraction * 10ULL) + (unsigned long long)(*text - '0');
                divisor *= 10ULL;
            }
            text += 1;
        }
    }

    if (!saw_digit) {
        return -1;
    }

    if (text[0] == '\0' || (text[0] == 's' && text[1] == '\0')) {
        unit_ms = 1000ULL;
    } else if (text[0] == 'm' && text[1] == 's' && text[2] == '\0') {
        unit_ms = 1ULL;
    } else if (text[0] == 'm' && text[1] == '\0') {
        unit_ms = 60ULL * 1000ULL;
    } else if (text[0] == 'h' && text[1] == '\0') {
        unit_ms = 60ULL * 60ULL * 1000ULL;
    } else if (text[0] == 'd' && text[1] == '\0') {
        unit_ms = 24ULL * 60ULL * 60ULL * 1000ULL;
    } else {
        return -1;
    }

    *milliseconds_out = whole * unit_ms;
    if (saw_fraction) {
        unsigned long long fraction_ms = ((fraction * unit_ms) + (divisor / 2ULL)) / divisor;
        if (fraction_ms == 0ULL && fraction > 0ULL) {
            fraction_ms = 1ULL;
        }
        *milliseconds_out += fraction_ms;
    }

    return 0;
}

int tool_parse_signal_name(const char *text, int *signal_out) {
    return platform_parse_signal_name(text, signal_out);
}

const char *tool_signal_name(int signal_number) {
    return platform_signal_name(signal_number);
}

void tool_write_signal_list(int fd) {
    platform_write_signal_list(fd);
}
