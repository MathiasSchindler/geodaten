#include "platform.h"
#include "runtime.h"
#include "tool_util.h"
#include "xml.h"


typedef struct {
    int fd;
    int should_close;
    char buffer[8192];
    size_t pos;
    size_t length;
    unsigned long long line;
    unsigned long long column;
    char error[160];
    unsigned long long error_line;
    unsigned long long error_column;
    unsigned int utf8_codepoint;
    unsigned int utf8_min;
    unsigned int utf8_remaining;
    unsigned long long utf8_line;
    unsigned long long utf8_column;
    int eof;
} XmlStream;

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StringStack;

static int stream_is_space(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; }
static int stream_is_name_start(char ch) {
    unsigned char uch = (unsigned char)ch;
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' || ch == ':' || uch >= 0x80U;
}
static int stream_is_name_char(char ch) { return stream_is_name_start(ch) || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.'; }
static int stream_char_allowed(char ch) {
    unsigned char uch = (unsigned char)ch;
    return uch >= 0x20U || ch == '\t' || ch == '\n' || ch == '\r';
}
static int stream_codepoint_allowed(unsigned long value) {
    return value == 0x9UL || value == 0xAUL || value == 0xDUL ||
        (value >= 0x20UL && value <= 0xD7FFUL) ||
        (value >= 0xE000UL && value <= 0xFFFDUL) ||
        (value >= 0x10000UL && value <= 0x10FFFFUL);
}

static void stream_error(XmlStream *stream, const char *message) {
    if (stream->error[0] == '\0') {
        rt_copy_string(stream->error, sizeof(stream->error), message);
        stream->error_line = stream->line;
        stream->error_column = stream->column;
    }
}

static void stream_utf8_error(XmlStream *stream, unsigned long long line, unsigned long long column) {
    if (stream->error[0] == '\0') {
        rt_copy_string(stream->error, sizeof(stream->error), "invalid UTF-8 sequence");
        stream->error_line = line;
        stream->error_column = column;
    }
}

static void stream_note_utf8_byte(XmlStream *stream, unsigned char byte, unsigned long long line, unsigned long long column) {
    if (stream->error[0] != '\0') {
        return;
    }

    if (stream->utf8_remaining == 0U) {
        if (byte < 0x80U) {
            if (!stream_codepoint_allowed((unsigned long)byte)) {
                stream_error(stream, "invalid XML character");
            }
            return;
        }
        stream->utf8_line = line;
        stream->utf8_column = column;
        if (byte >= 0xc2U && byte <= 0xdfU) {
            stream->utf8_codepoint = (unsigned int)(byte & 0x1fU);
            stream->utf8_min = 0x80U;
            stream->utf8_remaining = 1U;
        } else if (byte >= 0xe0U && byte <= 0xefU) {
            stream->utf8_codepoint = (unsigned int)(byte & 0x0fU);
            stream->utf8_min = 0x800U;
            stream->utf8_remaining = 2U;
        } else if (byte >= 0xf0U && byte <= 0xf4U) {
            stream->utf8_codepoint = (unsigned int)(byte & 0x07U);
            stream->utf8_min = 0x10000U;
            stream->utf8_remaining = 3U;
        } else {
            stream_utf8_error(stream, line, column);
        }
        return;
    }

    if ((byte & 0xc0U) != 0x80U) {
        stream_utf8_error(stream, stream->utf8_line, stream->utf8_column);
        return;
    }
    stream->utf8_codepoint = (stream->utf8_codepoint << 6) | (unsigned int)(byte & 0x3fU);
    stream->utf8_remaining -= 1U;
    if (stream->utf8_remaining == 0U &&
        (stream->utf8_codepoint < stream->utf8_min ||
         !stream_codepoint_allowed((unsigned long)stream->utf8_codepoint))) {
        stream_utf8_error(stream, stream->utf8_line, stream->utf8_column);
    }
}

static int stream_fill(XmlStream *stream) {
    long count;
    if (stream->pos < stream->length) return 0;
    if (stream->eof) return 0;
    count = platform_read(stream->fd, stream->buffer, sizeof(stream->buffer));
    if (count < 0) {
        stream_error(stream, "read failed");
        return -1;
    }
    stream->pos = 0U;
    stream->length = (size_t)count;
    if (count == 0) stream->eof = 1;
    return 0;
}

static int stream_peek(XmlStream *stream, char *ch_out) {
    if (stream_fill(stream) != 0) return -1;
    if (stream->pos >= stream->length) return 0;
    *ch_out = stream->buffer[stream->pos];
    return 1;
}

static int stream_get(XmlStream *stream, char *ch_out) {
    int result = stream_peek(stream, ch_out);
    if (result <= 0) return result;
    stream->pos += 1U;
    stream_note_utf8_byte(stream, (unsigned char)*ch_out, stream->line, stream->column);
    if (*ch_out == '\n') {
        stream->line += 1ULL;
        stream->column = 1ULL;
    } else {
        stream->column += 1ULL;
    }
    return 1;
}

static int stream_expect(XmlStream *stream, char expected, const char *message) {
    char ch;
    if (stream_get(stream, &ch) != 1 || ch != expected) {
        stream_error(stream, message);
        return -1;
    }
    return 0;
}

static int stream_skip_space(XmlStream *stream) {
    char ch;
    int result;
    while ((result = stream_peek(stream, &ch)) == 1 && stream_is_space(ch)) {
        stream_get(stream, &ch);
    }
    return result < 0 ? -1 : 0;
}

static int stream_stack_push(StringStack *stack, char *name) {
    char **resized;
    size_t next_capacity;
    if (stack->count == stack->capacity) {
        next_capacity = stack->capacity == 0U ? 128U : stack->capacity * 2U;
        if (next_capacity <= stack->capacity) return -1;
        resized = (char **)rt_realloc(stack->items, next_capacity * sizeof(*resized));
        if (resized == 0) return -1;
        stack->items = resized;
        stack->capacity = next_capacity;
    }
    stack->items[stack->count++] = name;
    return 0;
}

static void stream_stack_free(StringStack *stack) {
    size_t i;
    for (i = 0U; i < stack->count; ++i) rt_free(stack->items[i]);
    rt_free(stack->items);
}

static char *stream_parse_name(XmlStream *stream) {
    char local[128];
    char *dynamic = 0;
    size_t length = 0U;
    size_t capacity = 0U;
    char ch;
    int result;
    if (stream_peek(stream, &ch) != 1 || !stream_is_name_start(ch)) {
        stream_error(stream, "expected XML name");
        return 0;
    }
    while ((result = stream_peek(stream, &ch)) == 1 && stream_is_name_char(ch)) {
        stream_get(stream, &ch);
        if (dynamic == 0 && length < sizeof(local) - 1U) {
            local[length++] = ch;
        } else {
            char *resized;
            if (dynamic == 0) {
                capacity = sizeof(local) * 2U;
                dynamic = (char *)rt_malloc(capacity);
                if (dynamic == 0) return 0;
                memcpy(dynamic, local, length);
            } else if (length + 1U >= capacity) {
                capacity *= 2U;
                resized = (char *)rt_realloc(dynamic, capacity);
                if (resized == 0) {
                    rt_free(dynamic);
                    return 0;
                }
                dynamic = resized;
            }
            dynamic[length++] = ch;
        }
    }
    if (result < 0) {
        rt_free(dynamic);
        return 0;
    }
    if (dynamic == 0) {
        local[length] = '\0';
        return xml_slice_dup(local, length);
    }
    dynamic[length] = '\0';
    return dynamic;
}

static int stream_parse_reference(XmlStream *stream) {
    char ch;
    unsigned long value = 0UL;
    if (stream_get(stream, &ch) != 1) {
        stream_error(stream, "unterminated entity reference");
        return -1;
    }
    if (ch == '#') {
        int hex = 0;
        int digits = 0;
        if (stream_peek(stream, &ch) == 1 && (ch == 'x' || ch == 'X')) {
            hex = 1;
            stream_get(stream, &ch);
        }
        while (stream_peek(stream, &ch) == 1) {
            unsigned int digit;
            if (ch >= '0' && ch <= '9') digit = (unsigned int)(ch - '0');
            else if (hex && ch >= 'a' && ch <= 'f') digit = (unsigned int)(ch - 'a' + 10);
            else if (hex && ch >= 'A' && ch <= 'F') digit = (unsigned int)(ch - 'A' + 10);
            else break;
            if ((!hex && value > (0x10FFFFUL - digit) / 10UL) || (hex && value > (0x10FFFFUL - digit) / 16UL)) {
                stream_error(stream, "invalid character reference");
                return -1;
            }
            value = value * (hex ? 16UL : 10UL) + digit;
            digits += 1;
            stream_get(stream, &ch);
        }
        if (digits == 0 || !stream_codepoint_allowed(value)) {
            stream_error(stream, "invalid character reference");
            return -1;
        }
    } else {
        char name[6];
        size_t length = 0U;
        while (stream_is_name_char(ch)) {
            if (length + 1U < sizeof(name)) name[length] = ch;
            length += 1U;
            if (stream_peek(stream, &ch) != 1) break;
            if (!stream_is_name_char(ch)) break;
            stream_get(stream, &ch);
        }
        if (length + 1U >= sizeof(name)) {
            stream_error(stream, "undeclared entity reference");
            return -1;
        }
        name[length] = '\0';
        if (!(rt_strcmp(name, "amp") == 0 || rt_strcmp(name, "lt") == 0 || rt_strcmp(name, "gt") == 0 || rt_strcmp(name, "quot") == 0 || rt_strcmp(name, "apos") == 0)) {
            stream_error(stream, "undeclared entity reference");
            return -1;
        }
    }
    return stream_expect(stream, ';', "unterminated entity reference");
}

static int stream_parse_until(XmlStream *stream, const char *end, const char *error) {
    size_t matched = 0U;
    char ch;
    while (stream_get(stream, &ch) == 1) {
        if (!stream_char_allowed(ch)) {
            stream_error(stream, "invalid XML character");
            return -1;
        }
        if (ch == end[matched]) {
            matched += 1U;
            if (end[matched] == '\0') return 0;
        } else {
            matched = ch == end[0] ? 1U : 0U;
        }
    }
    stream_error(stream, error);
    return -1;
}

static int stream_parse_comment(XmlStream *stream) {
    char previous = 0;
    char ch;
    int dash_count = 0;
    while (stream_get(stream, &ch) == 1) {
        if (!stream_char_allowed(ch)) {
            stream_error(stream, "invalid XML character");
            return -1;
        }
        if (previous == '-' && ch == '-') dash_count = 2;
        else if (dash_count == 2 && ch == '>') return 0;
        else if (dash_count == 2) {
            stream_error(stream, "comment contains '--'");
            return -1;
        }
        previous = ch;
    }
    stream_error(stream, "unterminated comment");
    return -1;
}

static int stream_name_is_xml(const char *name) {
    char a;
    char b;
    char c;
    if (name == 0) return 0;
    a = name[0];
    b = name[1];
    c = name[2];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return a == 'x' && b == 'm' && c == 'l' && name[3] == '\0';
}

static int stream_parse_pi(XmlStream *stream, int at_start) {
    char *name = stream_parse_name(stream);
    if (name == 0) return -1;
    if (!at_start && stream_name_is_xml(name)) {
        rt_free(name);
        stream_error(stream, "XML declaration must be at document start");
        return -1;
    }
    rt_free(name);
    return stream_parse_until(stream, "?>", "unterminated processing instruction");
}

static int stream_parse_doctype(XmlStream *stream) {
    char ch;
    int quote = 0;
    unsigned int bracket_depth = 0U;
    while (stream_get(stream, &ch) == 1) {
        if (quote != 0) {
            if (ch == quote) quote = 0;
        } else if (ch == '\'' || ch == '"') quote = ch;
        else if (ch == '[') bracket_depth += 1U;
        else if (ch == ']' && bracket_depth > 0U) bracket_depth -= 1U;
        else if (ch == '>' && bracket_depth == 0U) return 0;
    }
    stream_error(stream, "unterminated doctype");
    return -1;
}

static int stream_parse_attributes(XmlStream *stream, char ***attrs_out, size_t *count_out) {
    char **attrs = 0;
    size_t count = 0U;
    size_t capacity = 0U;
    char ch;
    *attrs_out = 0;
    *count_out = 0U;
    for (;;) {
        char *name;
        char quote;
        size_t i;
        if (stream_skip_space(stream) != 0) goto fail;
        if (stream_peek(stream, &ch) != 1 || ch == '>' || ch == '/') break;
        name = stream_parse_name(stream);
        if (name == 0) goto fail;
        for (i = 0U; i < count; ++i) {
            if (rt_strcmp(attrs[i], name) == 0) {
                rt_free(name);
                stream_error(stream, "duplicate attribute");
                goto fail;
            }
        }
        if (count == capacity) {
            char **resized;
            capacity = capacity == 0U ? 16U : capacity * 2U;
            resized = (char **)rt_realloc(attrs, capacity * sizeof(*attrs));
            if (resized == 0) { rt_free(name); goto fail; }
            attrs = resized;
        }
        attrs[count++] = name;
        if (stream_skip_space(stream) != 0 || stream_expect(stream, '=', "expected '=' after attribute name") != 0 || stream_skip_space(stream) != 0) goto fail;
        if (stream_get(stream, &quote) != 1 || (quote != '\'' && quote != '"')) {
            stream_error(stream, "expected quoted attribute value");
            goto fail;
        }
        while (stream_get(stream, &ch) == 1 && ch != quote) {
            if (!stream_char_allowed(ch)) { stream_error(stream, "invalid XML character"); goto fail; }
            if (ch == '<') { stream_error(stream, "attribute value contains '<'"); goto fail; }
            if (ch == '&' && stream_parse_reference(stream) != 0) goto fail;
        }
        if (ch != quote) { stream_error(stream, "unterminated attribute value"); goto fail; }
    }
    *attrs_out = attrs;
    *count_out = count;
    return 0;
fail:
    while (count > 0U) rt_free(attrs[--count]);
    rt_free(attrs);
    return -1;
}

static void free_attrs(char **attrs, size_t count) {
    while (count > 0U) rt_free(attrs[--count]);
    rt_free(attrs);
}

int xml_stream_validate_document_with_options(const char *path, const char *tool_name, const XmlStreamOptions *options) {
    XmlStream stream;
    StringStack stack;
    unsigned int root_count = 0U;
    int at_start = 1;
    int exit_code = 0;
    char ch;
    rt_memset(&stream, 0, sizeof(stream));
    rt_memset(&stack, 0, sizeof(stack));
    stream.line = 1ULL;
    stream.column = 1ULL;
    if (tool_open_input(path, &stream.fd, &stream.should_close) != 0) {
        tool_write_error(tool_name, "cannot open input: ", path == 0 ? "-" : path);
        return 1;
    }
    while (stream_peek(&stream, &ch) == 1) {
        if (ch != '<') {
            char previous1 = 0;
            char previous2 = 0;
            while (stream_peek(&stream, &ch) == 1 && ch != '<') {
                stream_get(&stream, &ch);
                if (!stream_char_allowed(ch)) { stream_error(&stream, "invalid XML character"); break; }
                if (stack.count == 0U && !stream_is_space(ch)) { stream_error(&stream, "text outside document element"); break; }
                if (previous2 == ']' && previous1 == ']' && ch == '>') { stream_error(&stream, "']]>' is not allowed in character data"); break; }
                if (ch == '&' && stream_parse_reference(&stream) != 0) break;
                previous2 = previous1;
                previous1 = ch;
            }
            if (stream.error[0] != '\0') break;
            at_start = 0;
            continue;
        }
        stream_get(&stream, &ch);
        if (stream_get(&stream, &ch) != 1) { stream_error(&stream, "unterminated markup"); break; }
        if (ch == '/') {
            char *name = stream_parse_name(&stream);
            if (name == 0) break;
            if (stream_skip_space(&stream) != 0 || stream_expect(&stream, '>', "expected '>' after end tag") != 0) { rt_free(name); break; }
            if (stack.count == 0U || rt_strcmp(stack.items[stack.count - 1U], name) != 0) { rt_free(name); stream_error(&stream, "mismatched end tag"); break; }
            rt_free(stack.items[--stack.count]);
            rt_free(name);
        } else if (ch == '?') {
            if (options != 0 && !options->allow_pi) { stream_error(&stream, "processing instruction is not allowed"); break; }
            if (stream_parse_pi(&stream, at_start) != 0) break;
        } else if (ch == '!') {
            if (stream_get(&stream, &ch) != 1) { stream_error(&stream, "unterminated markup"); break; }
            if (ch == '-') {
                if (options != 0 && !options->allow_comments) { stream_error(&stream, "comment is not allowed"); break; }
                if (stream_expect(&stream, '-', "expected comment") != 0 || stream_parse_comment(&stream) != 0) break;
            } else if (ch == '[') {
                const char *rest = "CDATA[";
                size_t i;
                for (i = 0U; rest[i] != '\0'; ++i) if (stream_expect(&stream, rest[i], "expected CDATA section") != 0) break;
                if (rest[i] != '\0' || stream_parse_until(&stream, "]]>", "unterminated CDATA section") != 0) break;
            } else if (ch == 'D') {
                const char *rest = "OCTYPE";
                if (options != 0 && !options->allow_doctype) { stream_error(&stream, "doctype is not allowed"); break; }
                size_t i;
                for (i = 0U; rest[i] != '\0'; ++i) if (stream_expect(&stream, rest[i], "expected doctype") != 0) break;
                if (rest[i] != '\0' || stream_parse_doctype(&stream) != 0) break;
            } else { stream_error(&stream, "unknown markup declaration"); break; }
        } else {
            char *name;
            char **attrs;
            size_t attr_count;
            int empty = 0;
            stream.pos -= 1U;
            stream.column -= 1ULL;
            name = stream_parse_name(&stream);
            if (name == 0) break;
            if (stream_parse_attributes(&stream, &attrs, &attr_count) != 0) { rt_free(name); break; }
            if (stream_peek(&stream, &ch) == 1 && ch == '/') { stream_get(&stream, &ch); empty = 1; }
            if (stream_expect(&stream, '>', empty ? "expected '>' after empty tag" : "expected '>' after start tag") != 0) { free_attrs(attrs, attr_count); rt_free(name); break; }
            if (stack.count == 0U) {
                root_count += 1U;
                if (root_count > 1U) { free_attrs(attrs, attr_count); rt_free(name); stream_error(&stream, "multiple document elements"); break; }
                if (options != 0 && options->root_name != 0 && rt_strcmp(name, options->root_name) != 0) { free_attrs(attrs, attr_count); rt_free(name); stream_error(&stream, "unexpected root element"); break; }
            }
            if (options != 0 && options->max_depth > 0U && stack.count + 1U > options->max_depth) { free_attrs(attrs, attr_count); rt_free(name); stream_error(&stream, "maximum depth exceeded"); break; }
            if (!empty && stream_stack_push(&stack, name) != 0) { free_attrs(attrs, attr_count); rt_free(name); stream_error(&stream, "out of memory"); break; }
            if (empty) rt_free(name);
            free_attrs(attrs, attr_count);
        }
        at_start = 0;
    }
    if (stream.error[0] != '\0') exit_code = 1;
    else if (stream.utf8_remaining != 0U) { stream_utf8_error(&stream, stream.utf8_line, stream.utf8_column); exit_code = 1; }
    else if (stack.count != 0U) { stream_error(&stream, "unclosed element"); exit_code = 1; }
    else if (root_count == 0U) { stream_error(&stream, "missing document element"); exit_code = 1; }
    if (exit_code != 0) {
        if (tool_name != 0) { rt_write_cstr(2, tool_name); rt_write_cstr(2, ": "); }
        if (path != 0) { rt_write_cstr(2, path); rt_write_char(2, ':'); }
        rt_write_uint(2, stream.error_line == 0ULL ? stream.line : stream.error_line);
        rt_write_char(2, ':');
        rt_write_uint(2, stream.error_column == 0ULL ? stream.column : stream.error_column);
        rt_write_cstr(2, ": ");
        rt_write_cstr(2, stream.error[0] == '\0' ? "invalid XML" : stream.error);
        rt_write_char(2, '\n');
    }
    tool_close_input(stream.fd, stream.should_close);
    stream_stack_free(&stack);
    return exit_code;
}

int xml_stream_validate_document(const char *path, const char *tool_name) {
    return xml_stream_validate_document_with_options(path, tool_name, 0);
}
