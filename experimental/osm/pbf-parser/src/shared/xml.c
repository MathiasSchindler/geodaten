#include "platform.h"
#include "runtime.h"
#include "tool_util.h"
#include "xml.h"


static int xml_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static int xml_is_ascii_char_allowed(char ch) {
    unsigned char uch = (unsigned char)ch;
    return uch >= 0x20U || ch == '\t' || ch == '\n' || ch == '\r';
}

static int xml_is_codepoint_allowed(unsigned long value) {
    return value == 0x9UL || value == 0xAUL || value == 0xDUL ||
        (value >= 0x20UL && value <= 0xD7FFUL) ||
        (value >= 0xE000UL && value <= 0xFFFDUL) ||
        (value >= 0x10000UL && value <= 0x10FFFFUL);
}

static void xml_validate_document_utf8(XmlParser *parser) {
    size_t index = 0U;
    unsigned long long line = 1ULL;
    unsigned long long column = 1ULL;

    while (index < parser->length) {
        unsigned int codepoint = 0U;
        size_t before = index;
        if (rt_utf8_decode(parser->input, parser->length, &index, &codepoint) != 0 ||
            !xml_is_codepoint_allowed((unsigned long)codepoint)) {
            rt_copy_string(parser->error, sizeof(parser->error), "invalid UTF-8 sequence");
            parser->error_line = line;
            parser->error_column = column;
            return;
        }
        if (codepoint == '\n') {
            line += 1ULL;
            column = 1ULL;
        } else {
            column += 1ULL;
        }
        if (index == before) {
            break;
        }
    }
}

static int xml_name_equals_ascii_ci(const XmlName *name, const char *text) {
    size_t i;
    if (name == 0 || text == 0) return 0;
    for (i = 0U; i < name->length; ++i) {
        char left = name->start[i];
        char right = text[i];
        if (right == '\0') return 0;
        if (left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
        if (left != right) return 0;
    }
    return text[name->length] == '\0';
}

static int xml_is_name_start(char ch) {
    unsigned char uch = (unsigned char)ch;
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' || ch == ':' || uch >= 0x80U;
}

static int xml_is_name_char(char ch) {
    return xml_is_name_start(ch) || (ch >= '0' && ch <= '9') || ch == '-' || ch == '.';
}

int xml_is_name(const char *text) {
    size_t i;
    if (text == 0 || text[0] == '\0' || !xml_is_name_start(text[0])) return 0;
    for (i = 1U; text[i] != '\0'; ++i) {
        if (!xml_is_name_char(text[i])) return 0;
    }
    return 1;
}

static void xml_set_error(XmlParser *parser, const char *message) {
    if (parser->error[0] == '\0') {
        rt_copy_string(parser->error, sizeof(parser->error), message);
        parser->error_line = parser->line;
        parser->error_column = parser->column;
    }
}

static int xml_validate_literal_chars(XmlParser *parser, const char *text, size_t length) {
    size_t i;
    for (i = 0U; i < length; ++i) {
        if (!xml_is_ascii_char_allowed(text[i])) {
            xml_set_error(parser, "invalid XML character");
            return -1;
        }
    }
    return 0;
}

static int xml_ensure_parser_stack(XmlParser *parser, unsigned int needed) {
    XmlName *resized;
    unsigned int next_capacity;
    if (needed <= parser->stack_capacity) return 0;
    next_capacity = parser->stack_capacity == 0U ? XML_INITIAL_DEPTH : parser->stack_capacity;
    while (next_capacity < needed) {
        if (next_capacity > (unsigned int)(~0U / 2U)) {
            xml_set_error(parser, "maximum XML depth exceeded");
            return -1;
        }
        next_capacity *= 2U;
    }
    resized = (XmlName *)rt_malloc((size_t)next_capacity * sizeof(*resized));
    if (resized == 0) {
        xml_set_error(parser, "out of memory");
        return -1;
    }
    if (parser->depth > 0U && parser->stack != 0) {
        memcpy(resized, parser->stack, (size_t)parser->depth * sizeof(*resized));
    }
    if (parser->stack != parser->inline_stack && parser->stack != 0) rt_free(parser->stack);
    parser->stack = resized;
    parser->stack_capacity = next_capacity;
    return 0;
}

static int xml_ensure_parser_attributes(XmlParser *parser, size_t needed) {
    XmlAttribute *resized;
    size_t next_capacity;
    if (needed <= parser->attribute_capacity) return 0;
    next_capacity = parser->attribute_capacity == 0U ? XML_INITIAL_ATTRIBUTES : parser->attribute_capacity;
    while (next_capacity < needed) {
        if (next_capacity > ((size_t)-1) / 2U) {
            xml_set_error(parser, "too many attributes");
            return -1;
        }
        next_capacity *= 2U;
    }
    resized = (XmlAttribute *)rt_malloc(next_capacity * sizeof(*resized));
    if (resized == 0) {
        xml_set_error(parser, "out of memory");
        return -1;
    }
    if (parser->attributes != 0 && parser->attribute_capacity > 0U) {
        memcpy(resized, parser->attributes, parser->attribute_capacity * sizeof(*resized));
    }
    if (parser->attributes != parser->inline_attributes && parser->attributes != 0) rt_free(parser->attributes);
    parser->attributes = resized;
    parser->attribute_capacity = next_capacity;
    return 0;
}

static void xml_advance(XmlParser *parser, size_t count) {
    size_t i;
    for (i = 0U; i < count && parser->pos < parser->length; ++i) {
        char ch = parser->input[parser->pos++];
        if (ch == '\n') {
            parser->line += 1ULL;
            parser->column = 1ULL;
        } else {
            parser->column += 1ULL;
        }
    }
}

static int xml_starts_with_at(const XmlParser *parser, size_t pos, const char *text) {
    size_t i = 0U;
    while (text[i] != '\0') {
        if (pos + i >= parser->length || parser->input[pos + i] != text[i]) {
            return 0;
        }
        i += 1U;
    }
    return 1;
}

static void xml_skip_space(XmlParser *parser) {
    while (parser->pos < parser->length && xml_is_space(parser->input[parser->pos])) {
        xml_advance(parser, 1U);
    }
}

static int xml_parse_name(XmlParser *parser, XmlName *name) {
    size_t start = parser->pos;

    if (parser->pos >= parser->length || !xml_is_name_start(parser->input[parser->pos])) {
        xml_set_error(parser, "expected XML name");
        return -1;
    }
    xml_advance(parser, 1U);
    while (parser->pos < parser->length && xml_is_name_char(parser->input[parser->pos])) {
        xml_advance(parser, 1U);
    }
    name->start = parser->input + start;
    name->length = parser->pos - start;
    return 0;
}

static int xml_expect(XmlParser *parser, char expected, const char *message) {
    if (parser->pos >= parser->length || parser->input[parser->pos] != expected) {
        xml_set_error(parser, message);
        return -1;
    }
    xml_advance(parser, 1U);
    return 0;
}

int xml_names_equal(const XmlName *left, const XmlName *right) {
    size_t i;
    if (left->length != right->length) {
        return 0;
    }
    for (i = 0U; i < left->length; ++i) {
        if (left->start[i] != right->start[i]) {
            return 0;
        }
    }
    return 1;
}

int xml_name_equals(const XmlName *name, const char *text) {
    if (name == 0 || text == 0) {
        return 0;
    }
    return xml_name_equals_slice(name, text, rt_strlen(text));
}

int xml_name_equals_slice(const XmlName *name, const char *text, size_t length) {
    size_t i;
    if (name == 0 || text == 0 || name->length != length) {
        return 0;
    }
    for (i = 0U; i < length; ++i) {
        if (name->start[i] != text[i]) return 0;
    }
    return 1;
}

void xml_copy_name(const XmlName *name, char *buffer, size_t buffer_size) {
    size_t i;
    size_t limit;
    if (buffer == 0 || buffer_size == 0U) {
        return;
    }
    if (name == 0 || name->start == 0) {
        buffer[0] = '\0';
        return;
    }
    limit = name->length < buffer_size - 1U ? name->length : buffer_size - 1U;
    for (i = 0U; i < limit; ++i) {
        buffer[i] = name->start[i];
    }
    buffer[limit] = '\0';
}

int xml_copy_slice(const char *text, size_t length, char *buffer, size_t buffer_size) {
    size_t count;
    if (buffer == 0 || buffer_size == 0U) {
        return -1;
    }
    count = length < buffer_size - 1U ? length : buffer_size - 1U;
    if (count > 0U && text != 0) {
        memcpy(buffer, text, count);
    }
    buffer[count] = '\0';
    return length < buffer_size ? 0 : -1;
}

char *xml_slice_dup(const char *text, size_t length) {
    char *copy = (char *)rt_malloc(length + 1U);
    if (copy == 0) return 0;
    if (length > 0U && text != 0) memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

void xml_name_stack_init(XmlNameStack *stack) {
    if (stack == 0) return;
    rt_memset(stack, 0, sizeof(*stack));
    stack->items = stack->inline_items;
    stack->positions = stack->inline_positions;
    stack->capacity = XML_INITIAL_DEPTH;
    stack->position_capacity = XML_INITIAL_DEPTH;
}

void xml_name_stack_free(XmlNameStack *stack) {
    unsigned int i;
    if (stack == 0) return;
    if (stack->frames != 0) {
        for (i = 0U; i < stack->frame_capacity; ++i) rt_free(stack->frames[i].counts);
        rt_free(stack->frames);
    }
    if (stack->items != 0 && stack->items != stack->inline_items) rt_free(stack->items);
    if (stack->positions != 0 && stack->positions != stack->inline_positions) rt_free(stack->positions);
    stack->items = stack->inline_items;
    stack->positions = stack->inline_positions;
    stack->frames = 0;
    stack->count = 0U;
    stack->capacity = XML_INITIAL_DEPTH;
    stack->position_capacity = XML_INITIAL_DEPTH;
    stack->frame_capacity = 0U;
}

static int xml_name_stack_ensure(XmlNameStack *stack, unsigned int needed) {
    XmlName *resized;
    unsigned int *position_resized;
    unsigned int next_capacity;
    if (stack == 0) return -1;
    if (stack->items == 0) xml_name_stack_init(stack);
    if (needed <= stack->capacity) return 0;
    next_capacity = stack->capacity == 0U ? XML_INITIAL_DEPTH : stack->capacity;
    while (next_capacity < needed) {
        if (next_capacity > (unsigned int)(~0U / 2U)) return -1;
        next_capacity *= 2U;
    }
    resized = (XmlName *)rt_malloc((size_t)next_capacity * sizeof(*resized));
    if (resized == 0) return -1;
    position_resized = (unsigned int *)rt_malloc((size_t)next_capacity * sizeof(*position_resized));
    if (position_resized == 0) { rt_free(resized); return -1; }
    if (stack->count > 0U && stack->items != 0) memcpy(resized, stack->items, (size_t)stack->count * sizeof(*resized));
    if (stack->count > 0U && stack->positions != 0) memcpy(position_resized, stack->positions, (size_t)stack->count * sizeof(*position_resized));
    if (stack->items != stack->inline_items && stack->items != 0) rt_free(stack->items);
    if (stack->positions != stack->inline_positions && stack->positions != 0) rt_free(stack->positions);
    stack->items = resized;
    stack->positions = position_resized;
    stack->capacity = next_capacity;
    stack->position_capacity = next_capacity;
    return 0;
}

static int xml_name_count_frame_ensure(XmlNameCountFrame *frame, unsigned int needed) {
    XmlNameCount *resized;
    unsigned int next_capacity;
    if (needed <= frame->capacity) return 0;
    next_capacity = frame->capacity == 0U ? 8U : frame->capacity;
    while (next_capacity < needed) {
        if (next_capacity > (unsigned int)(~0U / 2U)) return -1;
        next_capacity *= 2U;
    }
    resized = (XmlNameCount *)rt_realloc(frame->counts, (size_t)next_capacity * sizeof(*resized));
    if (resized == 0) return -1;
    frame->counts = resized;
    frame->capacity = next_capacity;
    return 0;
}

static int xml_name_stack_ensure_frames(XmlNameStack *stack, unsigned int needed) {
    XmlNameCountFrame *resized;
    unsigned int next_capacity;
    unsigned int i;
    if (needed <= stack->frame_capacity) return 0;
    next_capacity = stack->frame_capacity == 0U ? XML_INITIAL_DEPTH : stack->frame_capacity;
    while (next_capacity < needed) {
        if (next_capacity > (unsigned int)(~0U / 2U)) return -1;
        next_capacity *= 2U;
    }
    resized = (XmlNameCountFrame *)rt_realloc(stack->frames, (size_t)next_capacity * sizeof(*resized));
    if (resized == 0) return -1;
    for (i = stack->frame_capacity; i < next_capacity; ++i) {
        resized[i].counts = 0;
        resized[i].count = 0U;
        resized[i].capacity = 0U;
    }
    stack->frames = resized;
    stack->frame_capacity = next_capacity;
    return 0;
}

static int xml_name_stack_next_position(XmlNameStack *stack, XmlName name, unsigned int *position_out) {
    XmlNameCountFrame *frame;
    unsigned int i;
    unsigned int parent_depth = stack->count;
    if (xml_name_stack_ensure_frames(stack, parent_depth + 1U) != 0) return -1;
    frame = &stack->frames[parent_depth];
    for (i = 0U; i < frame->count; ++i) {
        if (xml_names_equal(&frame->counts[i].name, &name)) {
            frame->counts[i].count += 1U;
            *position_out = frame->counts[i].count;
            return 0;
        }
    }
    if (xml_name_count_frame_ensure(frame, frame->count + 1U) != 0) return -1;
    frame->counts[frame->count].name = name;
    frame->counts[frame->count].count = 1U;
    frame->count += 1U;
    *position_out = 1U;
    return 0;
}

int xml_name_stack_push(XmlNameStack *stack, XmlName name) {
    unsigned int position = 1U;
    if (xml_name_stack_ensure(stack, stack == 0 ? 0U : stack->count + 1U) != 0) return -1;
    if (xml_name_stack_next_position(stack, name, &position) != 0) return -1;
    stack->items[stack->count] = name;
    stack->positions[stack->count] = position;
    stack->count += 1U;
    return 0;
}

void xml_name_stack_pop(XmlNameStack *stack) {
    if (stack != 0 && stack->count > 0U) {
        stack->count -= 1U;
        if (stack->frames != 0 && stack->count + 1U < stack->frame_capacity) stack->frames[stack->count + 1U].count = 0U;
    }
}

void xml_stack_push(XmlName *stack, unsigned int *depth, XmlName name) {
    if (stack != 0 && depth != 0 && *depth < XML_MAX_DEPTH) {
        stack[*depth] = name;
        *depth += 1U;
    }
}

void xml_stack_pop(unsigned int *depth) {
    if (depth != 0 && *depth > 0U) {
        *depth -= 1U;
    }
}

int xml_token_attr_value(const XmlToken *token, const char *attr_name, char *buffer, size_t buffer_size) {
    size_t i;
    size_t attr_name_length;
    if (token == 0 || attr_name == 0) {
        return 0;
    }
    attr_name_length = rt_strlen(attr_name);
    for (i = 0U; i < token->attribute_count; ++i) {
        if (xml_name_equals_slice(&token->attributes[i].name, attr_name, attr_name_length)) {
            return xml_copy_slice(token->attributes[i].value, token->attributes[i].value_length, buffer, buffer_size) == 0 ? 1 : -1;
        }
    }
    if (buffer != 0 && buffer_size > 0U) {
        buffer[0] = '\0';
    }
    return 0;
}

int xml_token_attr_slice(const XmlToken *token, const char *attr_name, const char **value_out, size_t *length_out) {
    size_t i;
    size_t attr_name_length;
    if (value_out != 0) *value_out = "";
    if (length_out != 0) *length_out = 0U;
    if (token == 0 || attr_name == 0) return 0;
    attr_name_length = rt_strlen(attr_name);
    for (i = 0U; i < token->attribute_count; ++i) {
        if (xml_name_equals_slice(&token->attributes[i].name, attr_name, attr_name_length)) {
            if (value_out != 0) *value_out = token->attributes[i].value;
            if (length_out != 0) *length_out = token->attributes[i].value_length;
            return 1;
        }
    }
    return 0;
}

static int xml_parse_entity(XmlParser *parser, size_t *index_io) {
    size_t index = *index_io;
    if (index >= parser->length || parser->input[index] != '&') {
        return 0;
    }
    index += 1U;
    if (index >= parser->length) {
        xml_set_error(parser, "unterminated entity reference");
        return -1;
    }
    if (parser->input[index] == '#') {
        unsigned long value = 0UL;
        index += 1U;
        if (index < parser->length && (parser->input[index] == 'x' || parser->input[index] == 'X')) {
            index += 1U;
            if (index >= parser->length || !((parser->input[index] >= '0' && parser->input[index] <= '9') || (parser->input[index] >= 'a' && parser->input[index] <= 'f') || (parser->input[index] >= 'A' && parser->input[index] <= 'F'))) {
                xml_set_error(parser, "invalid hexadecimal character reference");
                return -1;
            }
            while (index < parser->length && ((parser->input[index] >= '0' && parser->input[index] <= '9') || (parser->input[index] >= 'a' && parser->input[index] <= 'f') || (parser->input[index] >= 'A' && parser->input[index] <= 'F'))) {
                unsigned int digit;
                if (parser->input[index] >= '0' && parser->input[index] <= '9') digit = (unsigned int)(parser->input[index] - '0');
                else if (parser->input[index] >= 'a' && parser->input[index] <= 'f') digit = (unsigned int)(parser->input[index] - 'a' + 10);
                else digit = (unsigned int)(parser->input[index] - 'A' + 10);
                if (value > (0x10FFFFUL - digit) / 16UL) {
                    xml_set_error(parser, "invalid character reference");
                    return -1;
                }
                value = value * 16UL + digit;
                index += 1U;
            }
        } else {
            if (index >= parser->length || !(parser->input[index] >= '0' && parser->input[index] <= '9')) {
                xml_set_error(parser, "invalid character reference");
                return -1;
            }
            while (index < parser->length && parser->input[index] >= '0' && parser->input[index] <= '9') {
                unsigned int digit = (unsigned int)(parser->input[index] - '0');
                if (value > (0x10FFFFUL - digit) / 10UL) {
                    xml_set_error(parser, "invalid character reference");
                    return -1;
                }
                value = value * 10UL + digit;
                index += 1U;
            }
        }
        if (!xml_is_codepoint_allowed(value)) {
            xml_set_error(parser, "invalid character reference");
            return -1;
        }
    } else {
        size_t start = index;
        while (index < parser->length && xml_is_name_char(parser->input[index])) {
            index += 1U;
        }
        if (index == start) {
            xml_set_error(parser, "invalid entity reference");
            return -1;
        }
        if (!((index - start == 3U && rt_strncmp(parser->input + start, "amp", 3U) == 0) ||
              (index - start == 2U && rt_strncmp(parser->input + start, "lt", 2U) == 0) ||
              (index - start == 2U && rt_strncmp(parser->input + start, "gt", 2U) == 0) ||
              (index - start == 4U && rt_strncmp(parser->input + start, "quot", 4U) == 0) ||
              (index - start == 4U && rt_strncmp(parser->input + start, "apos", 4U) == 0))) {
            xml_set_error(parser, "undeclared entity reference");
            return -1;
        }
    }
    if (index >= parser->length || parser->input[index] != ';') {
        xml_set_error(parser, "unterminated entity reference");
        return -1;
    }
    *index_io = index + 1U;
    return 0;
}

static int xml_validate_references(XmlParser *parser, const char *text, size_t length) {
    size_t base = (size_t)(text - parser->input);
    size_t index = base;
    size_t end = base + length;
    while (index < end) {
        if (parser->input[index] == '&') {
            size_t entity_index = index;
            if (xml_parse_entity(parser, &entity_index) != 0) {
                return -1;
            }
            if (entity_index > end) {
                xml_set_error(parser, "entity reference crosses token boundary");
                return -1;
            }
            index = entity_index;
        } else {
            index += 1U;
        }
    }
    return 0;
}

static int xml_parse_attr_value(XmlParser *parser, XmlAttribute *attr) {
    char quote;
    size_t start;
    if (parser->pos >= parser->length || (parser->input[parser->pos] != '"' && parser->input[parser->pos] != '\'')) {
        xml_set_error(parser, "expected quoted attribute value");
        return -1;
    }
    quote = parser->input[parser->pos];
    xml_advance(parser, 1U);
    start = parser->pos;
    while (parser->pos < parser->length && parser->input[parser->pos] != quote) {
        if (parser->input[parser->pos] == '<') {
            xml_set_error(parser, "attribute value contains '<'");
            return -1;
        }
        xml_advance(parser, 1U);
    }
    if (parser->pos >= parser->length) {
        xml_set_error(parser, "unterminated attribute value");
        return -1;
    }
    attr->value = parser->input + start;
    attr->value_length = parser->pos - start;
    if (xml_validate_literal_chars(parser, attr->value, attr->value_length) != 0) {
        return -1;
    }
    if (xml_validate_references(parser, attr->value, attr->value_length) != 0) {
        return -1;
    }
    xml_advance(parser, 1U);
    return 0;
}

static int xml_parse_markup(XmlParser *parser, XmlToken *token) {
    size_t raw_start = parser->pos;

    token->line = parser->line;
    token->column = parser->column;
    token->raw = parser->input + raw_start;
    token->depth = parser->depth;
    xml_advance(parser, 1U);

    if (xml_starts_with_at(parser, parser->pos, "!--")) {
        size_t text_start;
        xml_advance(parser, 3U);
        text_start = parser->pos;
        while (parser->pos + 2U < parser->length && !xml_starts_with_at(parser, parser->pos, "-->")) {
            if (parser->pos + 1U < parser->length && parser->input[parser->pos] == '-' && parser->input[parser->pos + 1U] == '-') {
                xml_set_error(parser, "comment contains '--'");
                return -1;
            }
            xml_advance(parser, 1U);
        }
        if (parser->pos + 2U >= parser->length) {
            xml_set_error(parser, "unterminated comment");
            return -1;
        }
        token->type = XML_TOKEN_COMMENT;
        token->text = parser->input + text_start;
        token->text_length = parser->pos - text_start;
        if (xml_validate_literal_chars(parser, token->text, token->text_length) != 0) {
            return -1;
        }
        xml_advance(parser, 3U);
    } else if (xml_starts_with_at(parser, parser->pos, "![CDATA[")) {
        size_t text_start;
        xml_advance(parser, 8U);
        text_start = parser->pos;
        while (parser->pos + 2U < parser->length && !xml_starts_with_at(parser, parser->pos, "]]>") ) {
            xml_advance(parser, 1U);
        }
        if (parser->pos + 2U >= parser->length) {
            xml_set_error(parser, "unterminated CDATA section");
            return -1;
        }
        token->type = XML_TOKEN_CDATA;
        token->text = parser->input + text_start;
        token->text_length = parser->pos - text_start;
        if (xml_validate_literal_chars(parser, token->text, token->text_length) != 0) {
            return -1;
        }
        token->text_is_blank = xml_text_is_blank(token->text, token->text_length);
        xml_advance(parser, 3U);
    } else if (xml_starts_with_at(parser, parser->pos, "!DOCTYPE")) {
        int quote = 0;
        unsigned int bracket_depth = 0U;
        xml_advance(parser, 8U);
        token->type = XML_TOKEN_DOCTYPE;
        token->text = parser->input + parser->pos;
        while (parser->pos < parser->length) {
            char ch = parser->input[parser->pos];
            if (quote != 0) {
                if (ch == quote) {
                    quote = 0;
                }
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '[') {
                bracket_depth += 1U;
            } else if (ch == ']' && bracket_depth > 0U) {
                bracket_depth -= 1U;
            } else if (ch == '>' && bracket_depth == 0U) {
                break;
            }
            xml_advance(parser, 1U);
        }
        if (parser->pos >= parser->length) {
            xml_set_error(parser, "unterminated doctype");
            return -1;
        }
        token->text_length = (size_t)(parser->input + parser->pos - token->text);
        xml_advance(parser, 1U);
    } else if (parser->pos < parser->length && parser->input[parser->pos] == '?') {
        size_t text_start;
        xml_advance(parser, 1U);
        if (xml_parse_name(parser, &token->name) != 0) {
            return -1;
        }
        text_start = parser->pos;
        while (parser->pos + 1U < parser->length && !xml_starts_with_at(parser, parser->pos, "?>")) {
            xml_advance(parser, 1U);
        }
        if (parser->pos + 1U >= parser->length) {
            xml_set_error(parser, "unterminated processing instruction");
            return -1;
        }
        token->type = XML_TOKEN_PI;
        if (xml_name_equals_ascii_ci(&token->name, "xml") && raw_start != 0U) {
            xml_set_error(parser, "XML declaration must be at document start");
            return -1;
        }
        token->text = parser->input + text_start;
        token->text_length = parser->pos - text_start;
        if (xml_validate_literal_chars(parser, token->text, token->text_length) != 0) {
            return -1;
        }
        xml_advance(parser, 2U);
    } else if (parser->pos < parser->length && parser->input[parser->pos] == '/') {
        xml_advance(parser, 1U);
        token->type = XML_TOKEN_END;
        if (xml_parse_name(parser, &token->name) != 0) {
            return -1;
        }
        xml_skip_space(parser);
        if (xml_expect(parser, '>', "expected '>' after end tag") != 0) {
            return -1;
        }
        if (parser->depth == 0U || !xml_names_equal(&parser->stack[parser->depth - 1U], &token->name)) {
            xml_set_error(parser, "mismatched end tag");
            return -1;
        }
        token->depth = parser->depth;
        parser->depth -= 1U;
    } else {
        token->type = XML_TOKEN_START;
        if (xml_parse_name(parser, &token->name) != 0) {
            return -1;
        }
        for (;;) {
            size_t i;
            xml_skip_space(parser);
            if (parser->pos >= parser->length) {
                xml_set_error(parser, "unterminated start tag");
                return -1;
            }
            if (parser->input[parser->pos] == '>') {
                xml_advance(parser, 1U);
                if (parser->depth == 0U) {
                    parser->root_count += 1U;
                    if (parser->root_count > 1U) {
                        xml_set_error(parser, "multiple document elements");
                        return -1;
                    }
                }
                if (xml_ensure_parser_stack(parser, parser->depth + 1U) != 0) {
                    return -1;
                }
                parser->stack[parser->depth++] = token->name;
                break;
            }
            if (parser->input[parser->pos] == '/' && parser->pos + 1U < parser->length && parser->input[parser->pos + 1U] == '>') {
                token->type = XML_TOKEN_EMPTY;
                xml_advance(parser, 2U);
                if (parser->depth == 0U) {
                    parser->root_count += 1U;
                    if (parser->root_count > 1U) {
                        xml_set_error(parser, "multiple document elements");
                        return -1;
                    }
                }
                break;
            }
            if (xml_ensure_parser_attributes(parser, token->attribute_count + 1U) != 0) {
                return -1;
            }
            token->attributes = parser->attributes;
            if (xml_parse_name(parser, &token->attributes[token->attribute_count].name) != 0) {
                return -1;
            }
            for (i = 0U; i < token->attribute_count; ++i) {
                if (xml_names_equal(&token->attributes[i].name, &token->attributes[token->attribute_count].name)) {
                    xml_set_error(parser, "duplicate attribute");
                    return -1;
                }
            }
            xml_skip_space(parser);
            if (xml_expect(parser, '=', "expected '=' after attribute name") != 0) {
                return -1;
            }
            xml_skip_space(parser);
            if (xml_parse_attr_value(parser, &token->attributes[token->attribute_count]) != 0) {
                return -1;
            }
            token->attribute_count += 1U;
        }
    }

    token->raw_length = parser->pos - raw_start;
    return 1;
}

void xml_parser_init(XmlParser *parser, const char *input, size_t length) {
    rt_memset(parser, 0, sizeof(*parser));
    parser->input = input;
    parser->length = length;
    parser->line = 1ULL;
    parser->column = 1ULL;
    parser->stack = parser->inline_stack;
    parser->stack_capacity = XML_INITIAL_DEPTH;
    parser->attributes = parser->inline_attributes;
    parser->attribute_capacity = XML_INITIAL_ATTRIBUTES;
    xml_validate_document_utf8(parser);
}

void xml_parser_free(XmlParser *parser) {
    if (parser == 0) return;
    if (parser->stack != 0 && parser->stack != parser->inline_stack) {
        rt_free(parser->stack);
    }
    if (parser->attributes != 0 && parser->attributes != parser->inline_attributes) {
        rt_free(parser->attributes);
    }
    parser->stack = parser->inline_stack;
    parser->attributes = parser->inline_attributes;
    parser->stack_capacity = XML_INITIAL_DEPTH;
    parser->attribute_capacity = XML_INITIAL_ATTRIBUTES;
}

int xml_next_token(XmlParser *parser, XmlToken *token) {
    size_t raw_start;

    rt_memset(token, 0, sizeof(*token));
    token->attributes = parser->attributes;
    if (parser->error[0] != '\0') {
        return -1;
    }
    if (parser->pos >= parser->length) {
        return 0;
    }
    if (parser->input[parser->pos] == '<') {
        return xml_parse_markup(parser, token);
    }

    raw_start = parser->pos;
    token->line = parser->line;
    token->column = parser->column;
    while (parser->pos < parser->length && parser->input[parser->pos] != '<') {
        if (parser->pos + 2U < parser->length && xml_starts_with_at(parser, parser->pos, "]]>") ) {
            xml_set_error(parser, "']]>' is not allowed in character data");
            return -1;
        }
        xml_advance(parser, 1U);
    }
    token->type = XML_TOKEN_TEXT;
    token->raw = parser->input + raw_start;
    token->raw_length = parser->pos - raw_start;
    token->text = token->raw;
    token->text_length = token->raw_length;
    token->depth = parser->depth;
    token->text_is_blank = xml_text_is_blank(token->text, token->text_length);
    if (xml_validate_literal_chars(parser, token->text, token->text_length) != 0) {
        return -1;
    }
    if (parser->depth == 0U && !token->text_is_blank) {
        xml_set_error(parser, "text outside document element");
        return -1;
    }
    if (xml_validate_references(parser, token->text, token->text_length) != 0) {
        return -1;
    }
    return 1;
}

int xml_parse_complete(XmlParser *parser) {
    if (parser->error[0] != '\0') {
        return -1;
    }
    if (parser->depth != 0U) {
        xml_set_error(parser, "unclosed element");
        return -1;
    }
    if (parser->root_count == 0U) {
        xml_set_error(parser, "missing document element");
        return -1;
    }
    return 0;
}

const char *xml_token_type_name(XmlTokenType type) {
    switch (type) {
        case XML_TOKEN_START: return "start";
        case XML_TOKEN_END: return "end";
        case XML_TOKEN_EMPTY: return "empty";
        case XML_TOKEN_TEXT: return "text";
        case XML_TOKEN_CDATA: return "cdata";
        case XML_TOKEN_COMMENT: return "comment";
        case XML_TOKEN_PI: return "pi";
        case XML_TOKEN_DOCTYPE: return "doctype";
        default: return "none";
    }
}

int xml_read_document(const char *path, char **buffer_out, size_t *length_out, const char *tool_name) {
    int fd;
    int should_close;
    char *buffer;
    size_t capacity = 65536U;
    size_t length = 0U;

    if (buffer_out == 0 || length_out == 0) {
        tool_write_error(tool_name, "invalid document output", 0);
        return -1;
    }
    *buffer_out = 0;
    *length_out = 0U;

    buffer = (char *)rt_malloc(capacity);
    if (buffer == 0) {
        tool_write_error(tool_name, "out of memory", 0);
        return -1;
    }

    if (tool_open_input(path, &fd, &should_close) != 0) {
        rt_free(buffer);
        tool_write_error(tool_name, "cannot open input: ", path == 0 ? "-" : path);
        return -1;
    }
    for (;;) {
        long count;
        if (length == capacity) {
            char *resized;
            size_t next_capacity = capacity * 2U;
            if (next_capacity <= capacity) {
                tool_close_input(fd, should_close);
                rt_free(buffer);
                tool_write_error(tool_name, "input too large", 0);
                return -1;
            }
            resized = (char *)rt_realloc(buffer, next_capacity);
            if (resized == 0) {
                tool_close_input(fd, should_close);
                rt_free(buffer);
                tool_write_error(tool_name, "out of memory", 0);
                return -1;
            }
            buffer = resized;
            capacity = next_capacity;
        }
        count = platform_read(fd, buffer + length, capacity - length);
        if (count < 0) {
            tool_close_input(fd, should_close);
            rt_free(buffer);
            tool_write_error(tool_name, "read failed: ", path == 0 ? "-" : path);
            return -1;
        }
        if (count == 0) {
            break;
        }
        length += (size_t)count;
    }
    tool_close_input(fd, should_close);
    *buffer_out = buffer;
    *length_out = length;
    return 0;
}

void xml_free_document(char *buffer) {
    rt_free(buffer);
}

void xml_report_error(const char *tool_name, const char *path, const XmlParser *parser) {
    if (tool_name != 0) {
        rt_write_cstr(2, tool_name);
        rt_write_cstr(2, ": ");
    }
    if (path != 0) {
        rt_write_cstr(2, path);
        rt_write_char(2, ':');
    }
    rt_write_uint(2, parser->error_line == 0ULL ? parser->line : parser->error_line);
    rt_write_char(2, ':');
    rt_write_uint(2, parser->error_column == 0ULL ? parser->column : parser->error_column);
    rt_write_cstr(2, ": ");
    rt_write_cstr(2, parser->error[0] == '\0' ? "invalid XML" : parser->error);
    rt_write_char(2, '\n');
}

int xml_text_is_blank(const char *text, size_t length) {
    size_t i;
    for (i = 0U; i < length; ++i) {
        if (!xml_is_space(text[i])) {
            return 0;
        }
    }
    return 1;
}

int xml_write_raw(int fd, const char *text, size_t length) {
    if (length == 0U) {
        return 0;
    }
    return rt_write_all(fd, text, length);
}

int xml_write_escaped_text(int fd, const char *text, size_t length) {
    size_t i;
    for (i = 0U; i < length; ++i) {
        char ch = text[i];
        if (ch == '&') {
            if (rt_write_cstr(fd, "&amp;") != 0) return -1;
        } else if (ch == '<') {
            if (rt_write_cstr(fd, "&lt;") != 0) return -1;
        } else if (ch == '>') {
            if (rt_write_cstr(fd, "&gt;") != 0) return -1;
        } else if (rt_write_char(fd, ch) != 0) {
            return -1;
        }
    }
    return 0;
}

int xml_write_escaped_attr(int fd, const char *text, size_t length) {
    size_t i;
    for (i = 0U; i < length; ++i) {
        char ch = text[i];
        if (ch == '&') {
            if (rt_write_cstr(fd, "&amp;") != 0) return -1;
        } else if (ch == '<') {
            if (rt_write_cstr(fd, "&lt;") != 0) return -1;
        } else if (ch == '"') {
            if (rt_write_cstr(fd, "&quot;") != 0) return -1;
        } else if (rt_write_char(fd, ch) != 0) {
            return -1;
        }
    }
    return 0;
}

int xml_write_indent(int fd, unsigned int depth, unsigned int indent_width) {
    unsigned int i;
    unsigned int count = depth * indent_width;
    for (i = 0U; i < count; ++i) {
        if (rt_write_char(fd, ' ') != 0) {
            return -1;
        }
    }
    return 0;
}

static int xml_copy_part(const char *src, size_t length, char *dst, size_t dst_size) {
    size_t i;
    if (dst_size == 0U || length + 1U > dst_size) {
        return -1;
    }
    for (i = 0U; i < length; ++i) {
        dst[i] = src[i];
    }
    dst[length] = '\0';
    return 0;
}

int xml_selector_attribute(const char *selector, char *attr_name, size_t attr_name_size, char *element_selector, size_t element_selector_size) {
    size_t i = 0U;
    size_t last_slash = 0U;
    if (selector == 0) {
        return 0;
    }
    while (selector[i] != '\0') {
        if (selector[i] == '/') {
            last_slash = i;
        }
        i += 1U;
    }
    if (selector[last_slash] == '/' && selector[last_slash + 1U] == '@') {
        if (xml_copy_part(selector, last_slash == 0U ? 1U : last_slash, element_selector, element_selector_size) != 0) {
            return -1;
        }
        if (last_slash == 0U) {
            element_selector[0] = '/';
            element_selector[1] = '\0';
        }
        return xml_copy_part(selector + last_slash + 2U, i - last_slash - 2U, attr_name, attr_name_size) == 0 ? 1 : -1;
    }
    return 0;
}

int xml_selector_attribute_dup(const char *selector, char **attr_name_out, char **element_selector_out) {
    size_t i = 0U;
    size_t last_slash = 0U;
    int bracket_depth = 0;
    char quote = '\0';
    if (attr_name_out == 0 || element_selector_out == 0) return -1;
    *attr_name_out = 0;
    *element_selector_out = 0;
    if (selector == 0) return 0;
    while (selector[i] != '\0') {
        if (quote != '\0') {
            if (selector[i] == quote) quote = '\0';
        } else if (selector[i] == '\'' || selector[i] == '"') quote = selector[i];
        else if (selector[i] == '[') bracket_depth += 1;
        else if (selector[i] == ']') {
            if (bracket_depth > 0) bracket_depth -= 1;
        } else if (selector[i] == '/' && bracket_depth == 0) last_slash = i;
        i += 1U;
    }
    if (selector[last_slash] == '/' && selector[last_slash + 1U] == '@') {
        size_t element_length = last_slash == 0U ? 1U : last_slash;
        *element_selector_out = xml_slice_dup(selector, element_length);
        *attr_name_out = xml_slice_dup(selector + last_slash + 2U, i - last_slash - 2U);
        if (*element_selector_out == 0 || *attr_name_out == 0) {
            rt_free(*element_selector_out);
            rt_free(*attr_name_out);
            *element_selector_out = 0;
            *attr_name_out = 0;
            return -1;
        }
        return 1;
    }
    return 0;
}

static int xml_match_component(const XmlName *name, const char *component, size_t length) {
    size_t i;
    if (length == 1U && component[0] == '*') {
        return 1;
    }
    if (name->length != length) {
        return 0;
    }
    for (i = 0U; i < length; ++i) {
        if (name->start[i] != component[i]) {
            return 0;
        }
    }
    return 1;
}

static int xml_selector_ensure_components(XmlSelector *compiled, unsigned int needed) {
    XmlSelectorComponent *resized;
    unsigned int next_capacity;
    if (needed <= compiled->component_capacity) return 0;
    next_capacity = compiled->component_capacity == 0U ? XML_INITIAL_DEPTH : compiled->component_capacity;
    while (next_capacity < needed) {
        if (next_capacity > (unsigned int)(~0U / 2U)) return -1;
        next_capacity *= 2U;
    }
    resized = (XmlSelectorComponent *)rt_realloc(compiled->components, (size_t)next_capacity * sizeof(*resized));
    if (resized == 0) return -1;
    compiled->components = resized;
    compiled->component_capacity = next_capacity;
    return 0;
}

static int xml_selector_ensure_predicates(XmlSelector *compiled, unsigned int needed) {
    XmlSelectorPredicate *resized;
    unsigned int next_capacity;
    if (needed <= compiled->predicate_capacity) return 0;
    next_capacity = compiled->predicate_capacity == 0U ? 4U : compiled->predicate_capacity;
    while (next_capacity < needed) {
        if (next_capacity > (unsigned int)(~0U / 2U)) return -1;
        next_capacity *= 2U;
    }
    resized = (XmlSelectorPredicate *)rt_realloc(compiled->predicates, (size_t)next_capacity * sizeof(*resized));
    if (resized == 0) return -1;
    compiled->predicates = resized;
    compiled->predicate_capacity = next_capacity;
    return 0;
}

static int xml_selector_add_predicate(XmlSelector *compiled, XmlSelectorComponent *component, const XmlSelectorPredicate *predicate) {
    if (xml_selector_ensure_predicates(compiled, compiled->predicate_count + 1U) != 0) return -1;
    if (component->predicate_count == 0U) component->predicate_index = compiled->predicate_count;
    compiled->predicates[compiled->predicate_count] = *predicate;
    compiled->predicate_count += 1U;
    component->predicate_count += 1U;
    compiled->has_predicates = 1;
    return 0;
}

static int xml_selector_parse_uint(const char *text, size_t length, unsigned int *value_out) {
    size_t i;
    unsigned int value = 0U;
    if (length == 0U) return -1;
    for (i = 0U; i < length; ++i) {
        unsigned int digit;
        if (text[i] < '0' || text[i] > '9') return -1;
        digit = (unsigned int)(text[i] - '0');
        if (value > (unsigned int)((~0U - digit) / 10U)) return -1;
        value = value * 10U + digit;
    }
    if (value == 0U) return -1;
    *value_out = value;
    return 0;
}

static size_t xml_selector_component_name_length(const char *text, size_t length) {
    size_t i = 0U;
    while (i < length && text[i] != '[') i += 1U;
    return i;
}

static int xml_selector_parse_predicates(XmlSelector *compiled, XmlSelectorComponent *component, const char *text, size_t length, size_t pos) {
    while (pos < length) {
        XmlSelectorPredicate predicate;
        size_t start;
        size_t end;
        rt_memset(&predicate, 0, sizeof(predicate));
        if (text[pos] != '[') return -1;
        start = pos + 1U;
        end = start;
        if (start >= length) return -1;
        if (text[start] == '@') {
            size_t name_start = start + 1U;
            size_t name_end = name_start;
            while (name_end < length && text[name_end] != '=' && text[name_end] != ']') name_end += 1U;
            if (name_end == name_start) return -1;
            predicate.name = text + name_start;
            predicate.name_length = name_end - name_start;
            if (name_end < length && text[name_end] == '=') {
                size_t value_start = name_end + 1U;
                size_t value_end;
                predicate.type = XML_SELECTOR_PREDICATE_ATTR_EQUALS;
                if (value_start >= length) return -1;
                if (text[value_start] == '\'' || text[value_start] == '"') {
                    char quote = text[value_start];
                    value_start += 1U;
                    value_end = value_start;
                    while (value_end < length && text[value_end] != quote) value_end += 1U;
                    if (value_end >= length || value_end + 1U >= length || text[value_end + 1U] != ']') return -1;
                    end = value_end + 1U;
                } else {
                    value_end = value_start;
                    while (value_end < length && text[value_end] != ']') value_end += 1U;
                    if (value_end >= length) return -1;
                    end = value_end;
                }
                predicate.value = text + value_start;
                predicate.value_length = value_end - value_start;
            } else if (name_end < length && text[name_end] == ']') {
                predicate.type = XML_SELECTOR_PREDICATE_ATTR_EXISTS;
                end = name_end;
            } else return -1;
        } else {
            while (end < length && text[end] != ']') end += 1U;
            if (end >= length) return -1;
            if (xml_selector_parse_uint(text + start, end - start, &predicate.position) != 0) return -1;
            predicate.type = XML_SELECTOR_PREDICATE_POSITION;
        }
        if (xml_selector_add_predicate(compiled, component, &predicate) != 0) return -1;
        pos = end + 1U;
    }
    return 0;
}

static size_t xml_selector_component_end(const char *selector, size_t pos) {
    int bracket_depth = 0;
    char quote = '\0';
    while (selector[pos] != '\0') {
        char ch = selector[pos];
        if (quote != '\0') {
            if (ch == quote) quote = '\0';
        } else if (ch == '\'' || ch == '"') quote = ch;
        else if (ch == '[') bracket_depth += 1;
        else if (ch == ']') {
            if (bracket_depth > 0) bracket_depth -= 1;
        } else if (ch == '/' && bracket_depth == 0) break;
        pos += 1U;
    }
    return pos;
}

int xml_selector_compile(XmlSelector *compiled, const char *selector) {
    size_t pos = 0U;
    int pending_descendant = 0;
    if (compiled == 0 || selector == 0 || selector[0] == '\0') return -1;
    rt_memset(compiled, 0, sizeof(*compiled));
    if (selector[0] == '/' && selector[1] == '/') {
        compiled->absolute = 0;
        pending_descendant = 1;
        pos = 2U;
    } else if (selector[0] == '/') {
        compiled->absolute = 1;
        pos = 1U;
    } else {
        compiled->absolute = 0;
    }
    while (selector[pos] != '\0') {
        size_t start;
        if (selector[pos] == '/') {
            pos += 1U;
            if (selector[pos] == '/') {
                pending_descendant = 1;
                while (selector[pos] == '/') pos += 1U;
            }
            continue;
        }
        if (xml_selector_ensure_components(compiled, compiled->component_count + 1U) != 0) {
            xml_selector_free(compiled);
            return -1;
        }
        start = pos;
        pos = xml_selector_component_end(selector, pos);
        compiled->components[compiled->component_count].start = selector + start;
        compiled->components[compiled->component_count].length = xml_selector_component_name_length(selector + start, pos - start);
        compiled->components[compiled->component_count].predicate_index = compiled->predicate_count;
        compiled->components[compiled->component_count].predicate_count = 0U;
        compiled->components[compiled->component_count].descendant = pending_descendant;
        if (compiled->components[compiled->component_count].length == 0U) {
            xml_selector_free(compiled);
            return -1;
        }
        if (xml_selector_parse_predicates(compiled, &compiled->components[compiled->component_count], selector + start, pos - start, compiled->components[compiled->component_count].length) != 0) {
            xml_selector_free(compiled);
            return -1;
        }
        if (pending_descendant) compiled->has_descendant = 1;
        compiled->component_count += 1U;
        pending_descendant = 0;
    }
    if (compiled->component_count == 0U) {
        xml_selector_free(compiled);
        return -1;
    }
    if (compiled->has_predicates) {
        unsigned int i;
        for (i = 0U; i + 1U < compiled->component_count; ++i) {
            if (compiled->components[i].predicate_count > 0U) {
                xml_selector_free(compiled);
                return -1;
            }
        }
    }
    return 0;
}

void xml_selector_free(XmlSelector *compiled) {
    if (compiled == 0) return;
    rt_free(compiled->components);
    rt_free(compiled->predicates);
    compiled->components = 0;
    compiled->predicates = 0;
    compiled->component_capacity = 0U;
    compiled->component_count = 0U;
    compiled->predicate_capacity = 0U;
    compiled->predicate_count = 0U;
    compiled->absolute = 0;
    compiled->has_descendant = 0;
    compiled->has_predicates = 0;
}

static int xml_match_selector_from(XmlName *stack, unsigned int depth, const XmlSelector *selector, unsigned int component_index, unsigned int stack_index) {
    if (component_index == selector->component_count) return stack_index == depth;
    if (selector->components[component_index].descendant) {
        unsigned int candidate;
        for (candidate = stack_index; candidate < depth; ++candidate) {
            if (xml_match_component(&stack[candidate], selector->components[component_index].start, selector->components[component_index].length) &&
                xml_match_selector_from(stack, depth, selector, component_index + 1U, candidate + 1U)) {
                return 1;
            }
        }
        return 0;
    }
    if (stack_index >= depth) return 0;
    if (!xml_match_component(&stack[stack_index], selector->components[component_index].start, selector->components[component_index].length)) return 0;
    return xml_match_selector_from(stack, depth, selector, component_index + 1U, stack_index + 1U);
}

int xml_compiled_path_matches(XmlName *stack, unsigned int depth, const XmlSelector *selector) {
    unsigned int first_stack = 0U;
    if (stack == 0 || selector == 0) return 0;
    if (selector->component_count > depth) return 0;
    if (!selector->absolute && !selector->components[0].descendant) {
        if (selector->has_descendant) {
            for (first_stack = 0U; first_stack < depth; ++first_stack) {
                if (xml_match_selector_from(stack, depth, selector, 0U, first_stack)) return 1;
            }
            return 0;
        }
        first_stack = depth - selector->component_count;
    } else if (selector->absolute && selector->components[0].descendant) {
        return 0;
    }
    return xml_match_selector_from(stack, depth, selector, 0U, first_stack);
}

int xml_path_matches(XmlName *stack, unsigned int depth, const char *selector) {
    static char cached_text[1024];
    static XmlSelector cached_selector;
    static int cached_valid = 0;
    XmlSelector compiled;
    if (selector == 0) return 0;
    if (cached_valid && rt_strcmp(cached_text, selector) == 0) {
        if (cached_selector.has_predicates) return 0;
        return xml_compiled_path_matches(stack, depth, &cached_selector);
    }
    if (rt_strlen(selector) < sizeof(cached_text)) {
        rt_copy_string(cached_text, sizeof(cached_text), selector);
        xml_selector_free(&cached_selector);
        if (xml_selector_compile(&cached_selector, cached_text) != 0) {
            cached_valid = 0;
            return 0;
        }
        if (cached_selector.has_predicates) {
            cached_valid = 0;
            return 0;
        }
        cached_valid = 1;
        return xml_compiled_path_matches(stack, depth, &cached_selector);
    }
    if (xml_selector_compile(&compiled, selector) != 0) return 0;
    {
        int matched = compiled.has_predicates ? 0 : xml_compiled_path_matches(stack, depth, &compiled);
        xml_selector_free(&compiled);
        return matched;
    }
}

int xml_name_stack_matches(const XmlNameStack *stack, const char *selector) {
    if (stack == 0) return 0;
    return xml_path_matches(stack->items, stack->count, selector);
}

int xml_name_stack_matches_compiled(const XmlNameStack *stack, const XmlSelector *selector) {
    if (stack == 0) return 0;
    if (selector != 0 && selector->has_predicates) return 0;
    return xml_compiled_path_matches(stack->items, stack->count, selector);
}

static int xml_token_has_attr_slice(const XmlToken *token, const char *name, size_t name_length, const char **value_out, size_t *value_length_out) {
    size_t i;
    for (i = 0U; i < token->attribute_count; ++i) {
        if (xml_name_equals_slice(&token->attributes[i].name, name, name_length)) {
            if (value_out != 0) *value_out = token->attributes[i].value;
            if (value_length_out != 0) *value_length_out = token->attributes[i].value_length;
            return 1;
        }
    }
    return 0;
}

static int xml_selector_token_predicates_match(const XmlNameStack *stack, const XmlToken *token, const XmlSelector *selector) {
    const XmlSelectorComponent *component;
    unsigned int i;
    if (selector->component_count == 0U) return 0;
    component = &selector->components[selector->component_count - 1U];
    for (i = 0U; i < component->predicate_count; ++i) {
        const XmlSelectorPredicate *predicate = &selector->predicates[component->predicate_index + i];
        if (predicate->type == XML_SELECTOR_PREDICATE_ATTR_EXISTS) {
            if (!xml_token_has_attr_slice(token, predicate->name, predicate->name_length, 0, 0)) return 0;
        } else if (predicate->type == XML_SELECTOR_PREDICATE_ATTR_EQUALS) {
            const char *value = 0;
            size_t value_length = 0U;
            if (!xml_token_has_attr_slice(token, predicate->name, predicate->name_length, &value, &value_length)) return 0;
            if (value_length != predicate->value_length || rt_strncmp(value, predicate->value, value_length) != 0) return 0;
        } else if (predicate->type == XML_SELECTOR_PREDICATE_POSITION) {
            if (stack->count == 0U || stack->positions[stack->count - 1U] != predicate->position) return 0;
        } else return 0;
    }
    return 1;
}

int xml_name_stack_matches_token(const XmlNameStack *stack, const XmlToken *token, const XmlSelector *selector) {
    if (stack == 0 || token == 0 || selector == 0) return 0;
    if (!xml_compiled_path_matches(stack->items, stack->count, selector)) return 0;
    if (!selector->has_predicates) return 1;
    return xml_selector_token_predicates_match(stack, token, selector);
}

void xml_write_path(int fd, XmlName *stack, unsigned int depth) {
    unsigned int i;
    if (depth == 0U) {
        rt_write_char(fd, '/');
        return;
    }
    for (i = 0U; i < depth; ++i) {
        rt_write_char(fd, '/');
        xml_write_raw(fd, stack[i].start, stack[i].length);
    }
}

void xml_write_name_stack_path(int fd, const XmlNameStack *stack) {
    if (stack == 0) {
        rt_write_char(fd, '/');
        return;
    }
    xml_write_path(fd, stack->items, stack->count);
}
