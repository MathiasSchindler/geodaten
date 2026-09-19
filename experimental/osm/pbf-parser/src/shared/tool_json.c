#include "runtime.h"
#include "tool_util.h"

static int tool_json_enabled = 0;
static unsigned long long tool_json_sequence = 0ULL;

void tool_json_set_enabled(int enabled) {
    tool_json_enabled = enabled != 0;
    if (tool_json_enabled) {
        tool_set_global_color_mode(TOOL_COLOR_NEVER);
    }
}

int tool_json_is_enabled(void) {
    return tool_json_enabled;
}

unsigned long long tool_json_next_seq(void) {
    tool_json_sequence += 1ULL;
    return tool_json_sequence;
}

static int write_u00_escape(int fd, unsigned char ch) {
    static const char hex[] = "0123456789abcdef";

    if (rt_write_cstr(fd, "\\u00") != 0) return -1;
    if (rt_write_char(fd, hex[(ch >> 4U) & 0x0fU]) != 0) return -1;
    return rt_write_char(fd, hex[ch & 0x0fU]);
}

int tool_json_write_string_n(int fd, const char *text, size_t length) {
    size_t index;

    if (rt_write_char(fd, '"') != 0) return -1;
    if (text != 0) {
        for (index = 0U; index < length; ++index) {
            unsigned char ch = (unsigned char)text[index];

            if (ch == '"' || ch == '\\') {
                if (rt_write_char(fd, '\\') != 0 || rt_write_char(fd, (char)ch) != 0) return -1;
            } else if (ch == '\n') {
                if (rt_write_cstr(fd, "\\n") != 0) return -1;
            } else if (ch == '\r') {
                if (rt_write_cstr(fd, "\\r") != 0) return -1;
            } else if (ch == '\t') {
                if (rt_write_cstr(fd, "\\t") != 0) return -1;
            } else if (ch < 0x20U) {
                if (write_u00_escape(fd, ch) != 0) return -1;
            } else if (rt_write_char(fd, (char)ch) != 0) {
                return -1;
            }
        }
    }
    return rt_write_char(fd, '"');
}

int tool_json_write_string(int fd, const char *text) {
    return tool_json_write_string_n(fd, text, text != 0 ? rt_strlen(text) : 0U);
}

int tool_json_write_base64(int fd, const unsigned char *data, size_t length) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t index = 0U;

    if (rt_write_char(fd, '"') != 0) return -1;
    while (index < length) {
        size_t remaining = length - index;
        unsigned int b0 = data[index++];
        unsigned int b1 = remaining > 1U ? data[index++] : 0U;
        unsigned int b2 = remaining > 2U ? data[index++] : 0U;

        if (rt_write_char(fd, alphabet[(b0 >> 2U) & 0x3fU]) != 0) return -1;
        if (rt_write_char(fd, alphabet[((b0 << 4U) | (b1 >> 4U)) & 0x3fU]) != 0) return -1;
        if (remaining > 1U) {
            if (rt_write_char(fd, alphabet[((b1 << 2U) | (b2 >> 6U)) & 0x3fU]) != 0) return -1;
        } else if (rt_write_char(fd, '=') != 0) {
            return -1;
        }
        if (remaining > 2U) {
            if (rt_write_char(fd, alphabet[b2 & 0x3fU]) != 0) return -1;
        } else if (rt_write_char(fd, '=') != 0) {
            return -1;
        }
    }
    return rt_write_char(fd, '"');
}

int tool_json_begin_event(int fd, const char *tool_name, const char *stream_name, const char *event_name) {
    if (rt_write_cstr(fd, "{\"schema\":\"newos.tool.v1\",\"tool\":") != 0) return -1;
    if (tool_json_write_string(fd, tool_name != 0 ? tool_name : "tool") != 0) return -1;
    if (rt_write_cstr(fd, ",\"stream\":") != 0) return -1;
    if (tool_json_write_string(fd, stream_name != 0 ? stream_name : (fd == 2 ? "stderr" : "stdout")) != 0) return -1;
    if (rt_write_cstr(fd, ",\"event\":") != 0) return -1;
    if (tool_json_write_string(fd, event_name != 0 ? event_name : "event") != 0) return -1;
    if (rt_write_cstr(fd, ",\"seq\":") != 0) return -1;
    return rt_write_uint(fd, tool_json_next_seq());
}

int tool_json_end_event(int fd) {
    return rt_write_cstr(fd, "}\n");
}

int tool_json_write_diagnostic(const char *tool_name, const char *level, const char *message, const char *detail) {
    if (tool_json_begin_event(2, tool_name, "stderr", "diagnostic") != 0) return -1;
    if (rt_write_cstr(2, ",\"level\":") != 0) return -1;
    if (tool_json_write_string(2, level != 0 ? level : "error") != 0) return -1;
    if (rt_write_cstr(2, ",\"message\":") != 0) return -1;
    if (tool_json_write_string(2, message != 0 ? message : "") != 0) return -1;
    if (rt_write_cstr(2, ",\"detail\":") != 0) return -1;
    if (detail != 0) {
        if (tool_json_write_string(2, detail) != 0) return -1;
    } else if (rt_write_cstr(2, "null") != 0) {
        return -1;
    }
    return tool_json_end_event(2);
}

int tool_json_write_usage(const char *tool_name, const char *usage_suffix) {
    if (tool_json_begin_event(2, tool_name, "stderr", "usage") != 0) return -1;
    if (rt_write_cstr(2, ",\"data\":{\"program\":") != 0) return -1;
    if (tool_json_write_string(2, tool_name != 0 ? tool_name : "tool") != 0) return -1;
    if (rt_write_cstr(2, ",\"usage_suffix\":") != 0) return -1;
    if (usage_suffix != 0) {
        if (tool_json_write_string(2, usage_suffix) != 0) return -1;
    } else if (rt_write_cstr(2, "null") != 0) {
        return -1;
    }
    if (rt_write_char(2, '}') != 0) return -1;
    return tool_json_end_event(2);
}
