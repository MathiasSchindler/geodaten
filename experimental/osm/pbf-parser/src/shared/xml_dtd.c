#include "runtime.h"
#include "tool_util.h"
#include "xml_dtd.h"


typedef struct {
    const char **items;
    unsigned int count;
    unsigned int capacity;
} DtdStringList;

typedef struct {
    int *items;
    unsigned int count;
    unsigned int capacity;
} DtdIntStack;

static char *dtd_slice_dup(const char *text, size_t length) {
    char *copy = (char *)rt_malloc(length + 1U);
    if (copy == 0) return 0;
    if (length > 0U) memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static int dtd_is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static void dtd_skip_space(const char *text, size_t length, size_t *pos) {
    while (*pos < length && dtd_is_space(text[*pos])) *pos += 1U;
}

static int dtd_name_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == ':' || ch == '.';
}

static int dtd_parse_name(const char *text, size_t length, size_t *pos, const char **start_out, size_t *length_out) {
    size_t start;
    dtd_skip_space(text, length, pos);
    start = *pos;
    if (start >= length || !dtd_name_char(text[start]) || (text[start] >= '0' && text[start] <= '9')) return -1;
    while (*pos < length && dtd_name_char(text[*pos])) *pos += 1U;
    *start_out = text + start;
    *length_out = *pos - start;
    return 0;
}

static int dtd_parse_quoted(const char *text, size_t length, size_t *pos, const char **start_out, size_t *length_out) {
    char quote;
    size_t start;
    dtd_skip_space(text, length, pos);
    if (*pos >= length || (text[*pos] != '\'' && text[*pos] != '"')) return -1;
    quote = text[*pos];
    *pos += 1U;
    start = *pos;
    while (*pos < length && text[*pos] != quote) *pos += 1U;
    if (*pos >= length) return -1;
    *start_out = text + start;
    *length_out = *pos - start;
    *pos += 1U;
    return 0;
}

static int dtd_token_equals(const char *left, size_t left_length, const char *right) {
    size_t right_length = rt_strlen(right);
    return left_length == right_length && rt_strncmp(left, right, left_length) == 0;
}

static int dtd_ensure_elements(XmlDtd *dtd, unsigned int needed) {
    XmlDtdElementDecl *resized;
    unsigned int next_capacity;
    if (needed <= dtd->element_capacity) return 0;
    next_capacity = dtd->element_capacity == 0U ? 16U : dtd->element_capacity;
    while (next_capacity < needed) next_capacity *= 2U;
    resized = (XmlDtdElementDecl *)rt_realloc(dtd->elements, (size_t)next_capacity * sizeof(*resized));
    if (resized == 0) return -1;
    dtd->elements = resized;
    dtd->element_capacity = next_capacity;
    return 0;
}

static int dtd_ensure_attributes(XmlDtd *dtd, unsigned int needed) {
    XmlDtdAttributeDecl *resized;
    unsigned int next_capacity;
    if (needed <= dtd->attribute_capacity) return 0;
    next_capacity = dtd->attribute_capacity == 0U ? 16U : dtd->attribute_capacity;
    while (next_capacity < needed) next_capacity *= 2U;
    resized = (XmlDtdAttributeDecl *)rt_realloc(dtd->attributes, (size_t)next_capacity * sizeof(*resized));
    if (resized == 0) return -1;
    dtd->attributes = resized;
    dtd->attribute_capacity = next_capacity;
    return 0;
}

void xml_dtd_init(XmlDtd *dtd) {
    rt_memset(dtd, 0, sizeof(*dtd));
}

void xml_dtd_free(XmlDtd *dtd) {
    unsigned int index;
    if (dtd == 0) return;
    rt_free(dtd->root_name);
    for (index = 0U; index < dtd->element_count; ++index) {
        rt_free(dtd->elements[index].name);
        rt_free(dtd->elements[index].content);
    }
    for (index = 0U; index < dtd->attribute_count; ++index) {
        rt_free(dtd->attributes[index].element_name);
        rt_free(dtd->attributes[index].name);
        rt_free(dtd->attributes[index].type);
        rt_free(dtd->attributes[index].default_value);
    }
    rt_free(dtd->elements);
    rt_free(dtd->attributes);
    xml_dtd_init(dtd);
}

static int dtd_set_root(XmlDtd *dtd, const char *name, size_t name_length) {
    char *copy;
    if (dtd->root_name != 0) return 0;
    copy = dtd_slice_dup(name, name_length);
    if (copy == 0) return -1;
    dtd->root_name = copy;
    return 0;
}

static int dtd_add_element(XmlDtd *dtd, const char *name, size_t name_length, const char *content, size_t content_length) {
    XmlDtdElementDecl *decl;
    if (dtd_ensure_elements(dtd, dtd->element_count + 1U) != 0) return -1;
    decl = &dtd->elements[dtd->element_count];
    rt_memset(decl, 0, sizeof(*decl));
    decl->name = dtd_slice_dup(name, name_length);
    decl->content = dtd_slice_dup(content, content_length);
    if (decl->name == 0 || decl->content == 0) return -1;
    decl->is_empty = dtd_token_equals(content, content_length, "EMPTY");
    decl->is_any = dtd_token_equals(content, content_length, "ANY");
    dtd->element_count += 1U;
    return 0;
}

static int dtd_add_attribute(XmlDtd *dtd, const char *element_name, size_t element_name_length, const char *name, size_t name_length, const char *type, size_t type_length, XmlDtdDefaultKind default_kind, const char *default_value, size_t default_value_length) {
    XmlDtdAttributeDecl *decl;
    if (dtd_ensure_attributes(dtd, dtd->attribute_count + 1U) != 0) return -1;
    decl = &dtd->attributes[dtd->attribute_count];
    rt_memset(decl, 0, sizeof(*decl));
    decl->element_name = dtd_slice_dup(element_name, element_name_length);
    decl->name = dtd_slice_dup(name, name_length);
    decl->type = dtd_slice_dup(type, type_length);
    decl->default_kind = default_kind;
    if (default_value != 0) decl->default_value = dtd_slice_dup(default_value, default_value_length);
    if (decl->element_name == 0 || decl->name == 0 || decl->type == 0 || (default_value != 0 && decl->default_value == 0)) return -1;
    dtd->attribute_count += 1U;
    return 0;
}

static int dtd_find_decl_end(const char *text, size_t length, size_t pos, size_t *end_out) {
    char quote = '\0';
    unsigned int depth = 0U;
    while (pos < length) {
        char ch = text[pos];
        if (quote != '\0') {
            if (ch == quote) quote = '\0';
        } else if (ch == '\'' || ch == '"') quote = ch;
        else if (ch == '(') depth += 1U;
        else if (ch == ')' && depth > 0U) depth -= 1U;
        else if (ch == '>' && depth == 0U) { *end_out = pos; return 0; }
        pos += 1U;
    }
    return -1;
}

static int dtd_parse_element_decl(XmlDtd *dtd, const char *text, size_t length, const char *tool_name) {
    const char *name;
    size_t name_length;
    size_t pos = 0U;
    size_t content_start;
    size_t content_end;
    if (dtd_parse_name(text, length, &pos, &name, &name_length) != 0) {
        tool_write_error(tool_name, "invalid ELEMENT declaration", 0);
        return -1;
    }
    dtd_skip_space(text, length, &pos);
    content_start = pos;
    content_end = length;
    while (content_end > content_start && dtd_is_space(text[content_end - 1U])) content_end -= 1U;
    if (content_end == content_start) {
        tool_write_error(tool_name, "empty ELEMENT declaration", 0);
        return -1;
    }
    if (dtd_add_element(dtd, name, name_length, text + content_start, content_end - content_start) != 0) {
        tool_write_error(tool_name, "out of memory", 0);
        return -1;
    }
    return 0;
}

static int dtd_parse_attlist_decl(XmlDtd *dtd, const char *text, size_t length, const char *tool_name) {
    const char *element_name;
    size_t element_name_length;
    size_t pos = 0U;
    if (dtd_parse_name(text, length, &pos, &element_name, &element_name_length) != 0) {
        tool_write_error(tool_name, "invalid ATTLIST declaration", 0);
        return -1;
    }
    for (;;) {
        const char *attr_name;
        const char *attr_type;
        const char *default_value = 0;
        size_t attr_name_length;
        size_t attr_type_length;
        size_t default_value_length = 0U;
        XmlDtdDefaultKind default_kind = XML_DTD_DEFAULT_NONE;
        dtd_skip_space(text, length, &pos);
        if (pos >= length) break;
        if (dtd_parse_name(text, length, &pos, &attr_name, &attr_name_length) != 0) return -1;
        dtd_skip_space(text, length, &pos);
        attr_type = text + pos;
        if (pos < length && text[pos] == '(') {
            unsigned int depth = 0U;
            do {
                if (text[pos] == '(') depth += 1U;
                else if (text[pos] == ')' && depth > 0U) depth -= 1U;
                pos += 1U;
            } while (pos < length && depth > 0U);
        } else {
            while (pos < length && !dtd_is_space(text[pos])) pos += 1U;
        }
        attr_type_length = (size_t)(text + pos - attr_type);
        dtd_skip_space(text, length, &pos);
        if (pos >= length) return -1;
        if (text[pos] == '#') {
            pos += 1U;
            if (pos + 7U <= length && rt_strncmp(text + pos, "IMPLIED", 7U) == 0) { default_kind = XML_DTD_DEFAULT_IMPLIED; pos += 7U; }
            else if (pos + 8U <= length && rt_strncmp(text + pos, "REQUIRED", 8U) == 0) { default_kind = XML_DTD_DEFAULT_REQUIRED; pos += 8U; }
            else if (pos + 5U <= length && rt_strncmp(text + pos, "FIXED", 5U) == 0) {
                default_kind = XML_DTD_DEFAULT_FIXED;
                pos += 5U;
                if (dtd_parse_quoted(text, length, &pos, &default_value, &default_value_length) != 0) return -1;
            } else return -1;
        } else {
            default_kind = XML_DTD_DEFAULT_VALUE;
            if (dtd_parse_quoted(text, length, &pos, &default_value, &default_value_length) != 0) return -1;
        }
        if (dtd_add_attribute(dtd, element_name, element_name_length, attr_name, attr_name_length, attr_type, attr_type_length, default_kind, default_value, default_value_length) != 0) {
            tool_write_error(tool_name, "out of memory", 0);
            return -1;
        }
    }
    return 0;
}

int xml_dtd_parse_subset(XmlDtd *dtd, const char *text, size_t length, const char *tool_name) {
    size_t pos = 0U;
    while (pos < length) {
        size_t decl_start;
        size_t decl_end;
        dtd_skip_space(text, length, &pos);
        if (pos >= length) break;
        if (pos + 4U <= length && rt_strncmp(text + pos, "<!--", 4U) == 0) {
            pos += 4U;
            while (pos + 3U <= length && rt_strncmp(text + pos, "-->", 3U) != 0) pos += 1U;
            if (pos + 3U > length) return -1;
            pos += 3U;
            continue;
        }
        if (pos + 2U > length || text[pos] != '<' || text[pos + 1U] != '!') return -1;
        decl_start = pos + 2U;
        if (dtd_find_decl_end(text, length, decl_start, &decl_end) != 0) return -1;
        if (decl_start + 7U <= decl_end && rt_strncmp(text + decl_start, "ELEMENT", 7U) == 0 && dtd_is_space(text[decl_start + 7U])) {
            if (dtd_parse_element_decl(dtd, text + decl_start + 7U, decl_end - decl_start - 7U, tool_name) != 0) return -1;
        } else if (decl_start + 7U <= decl_end && rt_strncmp(text + decl_start, "ATTLIST", 7U) == 0 && dtd_is_space(text[decl_start + 7U])) {
            if (dtd_parse_attlist_decl(dtd, text + decl_start + 7U, decl_end - decl_start - 7U, tool_name) != 0) return -1;
        }
        pos = decl_end + 1U;
    }
    return 0;
}

int xml_dtd_parse_doctype(XmlDtd *dtd, const char *text, size_t length, const char *tool_name) {
    const char *root_name;
    size_t root_name_length;
    size_t pos = 0U;
    if (dtd_parse_name(text, length, &pos, &root_name, &root_name_length) != 0) return -1;
    if (dtd_set_root(dtd, root_name, root_name_length) != 0) return -1;
    while (pos < length && text[pos] != '[') pos += 1U;
    if (pos < length && text[pos] == '[') {
        size_t subset_start = pos + 1U;
        size_t subset_end = length;
        while (subset_end > subset_start && text[subset_end - 1U] != ']') subset_end -= 1U;
        if (subset_end <= subset_start) return -1;
        return xml_dtd_parse_subset(dtd, text + subset_start, subset_end - subset_start - 1U, tool_name);
    }
    return 0;
}

int xml_dtd_load(XmlDtd *dtd, const char *dtd_path, const char *document_text, size_t document_length, const char *tool_name) {
    if (dtd_path != 0 && rt_strcmp(dtd_path, "auto") != 0) {
        char *buffer;
        size_t length;
        int result;
        if (xml_read_document(dtd_path, &buffer, &length, tool_name) != 0) return -1;
        result = xml_dtd_parse_subset(dtd, buffer, length, tool_name);
        xml_free_document(buffer);
        return result;
    }
    if (document_text != 0) {
        XmlParser parser;
        XmlToken token;
        int result;
        xml_parser_init(&parser, document_text, document_length);
        while ((result = xml_next_token(&parser, &token)) > 0) {
            if (token.type == XML_TOKEN_DOCTYPE) return xml_dtd_parse_doctype(dtd, token.text, token.text_length, tool_name);
            if (token.type == XML_TOKEN_START || token.type == XML_TOKEN_EMPTY) break;
        }
    }
    return 0;
}

const XmlDtdElementDecl *xml_dtd_find_element(const XmlDtd *dtd, const XmlName *name) {
    unsigned int index;
    if (dtd == 0 || name == 0) return 0;
    for (index = 0U; index < dtd->element_count; ++index) {
        if (xml_name_equals_slice(name, dtd->elements[index].name, rt_strlen(dtd->elements[index].name))) return &dtd->elements[index];
    }
    return 0;
}

const XmlDtdAttributeDecl *xml_dtd_find_attribute(const XmlDtd *dtd, const XmlName *element_name, const XmlName *attribute_name) {
    unsigned int index;
    if (dtd == 0 || element_name == 0 || attribute_name == 0) return 0;
    for (index = 0U; index < dtd->attribute_count; ++index) {
        if (xml_name_equals_slice(element_name, dtd->attributes[index].element_name, rt_strlen(dtd->attributes[index].element_name)) &&
            xml_name_equals_slice(attribute_name, dtd->attributes[index].name, rt_strlen(dtd->attributes[index].name))) return &dtd->attributes[index];
    }
    return 0;
}

static int token_has_attr(const XmlToken *token, const char *name, const char **value_out, size_t *length_out) {
    size_t index;
    for (index = 0U; index < token->attribute_count; ++index) {
        if (xml_name_equals_slice(&token->attributes[index].name, name, rt_strlen(name))) {
            if (value_out != 0) *value_out = token->attributes[index].value;
            if (length_out != 0) *length_out = token->attributes[index].value_length;
            return 1;
        }
    }
    return 0;
}

static int string_list_add(DtdStringList *list, const char *value, size_t length) {
    const char **resized;
    char *copy;
    if (list->count == list->capacity) {
        unsigned int next_capacity = list->capacity == 0U ? 16U : list->capacity * 2U;
        resized = (const char **)rt_realloc(list->items, (size_t)next_capacity * sizeof(*resized));
        if (resized == 0) return -1;
        list->items = resized;
        list->capacity = next_capacity;
    }
    copy = dtd_slice_dup(value, length);
    if (copy == 0) return -1;
    list->items[list->count++] = copy;
    return 0;
}

static int string_list_contains(const DtdStringList *list, const char *value) {
    unsigned int index;
    for (index = 0U; index < list->count; ++index) if (rt_strcmp(list->items[index], value) == 0) return 1;
    return 0;
}

static void string_list_free(DtdStringList *list) {
    unsigned int index;
    for (index = 0U; index < list->count; ++index) rt_free((char *)list->items[index]);
    rt_free(list->items);
}

static int int_stack_push(DtdIntStack *stack, int value) {
    int *resized;
    if (stack->count == stack->capacity) {
        unsigned int next_capacity = stack->capacity == 0U ? 32U : stack->capacity * 2U;
        resized = (int *)rt_realloc(stack->items, (size_t)next_capacity * sizeof(*resized));
        if (resized == 0) return -1;
        stack->items = resized;
        stack->capacity = next_capacity;
    }
    stack->items[stack->count++] = value;
    return 0;
}

static void int_stack_pop(DtdIntStack *stack) {
    if (stack->count > 0U) stack->count -= 1U;
}

static int int_stack_top(const DtdIntStack *stack) {
    return stack->count > 0U ? stack->items[stack->count - 1U] : 0;
}

static void int_stack_free(DtdIntStack *stack) {
    rt_free(stack->items);
}

static void write_dtd_error(const char *tool_name, const char *path, const XmlToken *token, const char *message) {
    rt_write_cstr(2, tool_name);
    rt_write_cstr(2, ": ");
    if (path != 0) { rt_write_cstr(2, path); rt_write_char(2, ':'); }
    rt_write_uint(2, token->line);
    rt_write_char(2, ':');
    rt_write_uint(2, token->column);
    rt_write_cstr(2, ": ");
    rt_write_cstr(2, message);
    rt_write_char(2, '\n');
}

static int validate_token(const XmlDtd *dtd, const XmlToken *token, const char *path, const char *tool_name, DtdStringList *ids, DtdStringList *refs) {
    const XmlDtdElementDecl *element_decl;
    unsigned int index;
    element_decl = xml_dtd_find_element(dtd, &token->name);
    if (dtd->element_count > 0U && element_decl == 0) {
        write_dtd_error(tool_name, path, token, "element is not declared in DTD");
        return -1;
    }
    for (index = 0U; index < dtd->attribute_count; ++index) {
        const XmlDtdAttributeDecl *attr_decl = &dtd->attributes[index];
        const char *value = 0;
        size_t value_length = 0U;
        if (!xml_name_equals_slice(&token->name, attr_decl->element_name, rt_strlen(attr_decl->element_name))) continue;
        if (!token_has_attr(token, attr_decl->name, &value, &value_length)) {
            if (attr_decl->default_kind == XML_DTD_DEFAULT_REQUIRED) {
                write_dtd_error(tool_name, path, token, "required attribute is missing");
                return -1;
            }
            continue;
        }
        if (attr_decl->default_kind == XML_DTD_DEFAULT_FIXED && attr_decl->default_value != 0 && (value_length != rt_strlen(attr_decl->default_value) || rt_strncmp(value, attr_decl->default_value, value_length) != 0)) {
            write_dtd_error(tool_name, path, token, "fixed attribute value does not match DTD");
            return -1;
        }
        if (rt_strcmp(attr_decl->type, "ID") == 0) {
            char *id_value = dtd_slice_dup(value, value_length);
            if (id_value == 0) return -1;
            if (string_list_contains(ids, id_value)) { rt_free(id_value); write_dtd_error(tool_name, path, token, "duplicate ID value"); return -1; }
            rt_free(id_value);
            if (string_list_add(ids, value, value_length) != 0) return -1;
        } else if (rt_strcmp(attr_decl->type, "IDREF") == 0) {
            if (string_list_add(refs, value, value_length) != 0) return -1;
        } else if (rt_strcmp(attr_decl->type, "IDREFS") == 0) {
            size_t pos = 0U;
            while (pos < value_length) {
                size_t start;
                while (pos < value_length && dtd_is_space(value[pos])) pos += 1U;
                start = pos;
                while (pos < value_length && !dtd_is_space(value[pos])) pos += 1U;
                if (pos > start && string_list_add(refs, value + start, pos - start) != 0) return -1;
            }
        }
    }
    return element_decl != 0 && element_decl->is_empty ? 1 : 0;
}

int xml_dtd_validate_document(const XmlDtd *dtd, const char *path, const char *input, size_t length, const char *tool_name) {
    XmlParser parser;
    XmlToken token;
    DtdIntStack empty_stack;
    DtdStringList ids;
    DtdStringList refs;
    int result;
    int saw_root = 0;
    unsigned int index;
    rt_memset(&empty_stack, 0, sizeof(empty_stack));
    rt_memset(&ids, 0, sizeof(ids));
    rt_memset(&refs, 0, sizeof(refs));
    xml_parser_init(&parser, input, length);
    while ((result = xml_next_token(&parser, &token)) > 0) {
        if (token.type == XML_TOKEN_START || token.type == XML_TOKEN_EMPTY) {
            int empty_decl;
            if (int_stack_top(&empty_stack)) { write_dtd_error(tool_name, path, &token, "EMPTY element contains child element"); goto fail; }
            if (token.depth == 0U) {
                saw_root = 1;
                if (dtd->root_name != 0 && !xml_name_equals_slice(&token.name, dtd->root_name, rt_strlen(dtd->root_name))) { write_dtd_error(tool_name, path, &token, "document root does not match DOCTYPE"); goto fail; }
            }
            empty_decl = validate_token(dtd, &token, path, tool_name, &ids, &refs);
            if (empty_decl < 0) goto fail;
            if (token.type == XML_TOKEN_START && int_stack_push(&empty_stack, empty_decl) != 0) goto fail;
        } else if (token.type == XML_TOKEN_END) {
            int_stack_pop(&empty_stack);
        } else if ((token.type == XML_TOKEN_TEXT || token.type == XML_TOKEN_CDATA) && int_stack_top(&empty_stack) && !token.text_is_blank) {
            write_dtd_error(tool_name, path, &token, "EMPTY element contains text");
            goto fail;
        }
    }
    if (result < 0 || xml_parse_complete(&parser) != 0) { xml_report_error(tool_name, path, &parser); goto fail; }
    if (!saw_root) { tool_write_error(tool_name, "missing root element: ", path == 0 ? "-" : path); goto fail; }
    for (index = 0U; index < refs.count; ++index) {
        if (!string_list_contains(&ids, refs.items[index])) {
            tool_write_error(tool_name, "unresolved IDREF: ", refs.items[index]);
            goto fail;
        }
    }
    int_stack_free(&empty_stack);
    string_list_free(&ids);
    string_list_free(&refs);
    return 0;
fail:
    int_stack_free(&empty_stack);
    string_list_free(&ids);
    string_list_free(&refs);
    return -1;
}

void xml_dtd_write_defaulted_start(int fd, const XmlDtd *dtd, const XmlToken *token) {
    unsigned int decl_index;
    size_t attr_index;
    rt_write_char(fd, '<');
    xml_write_raw(fd, token->name.start, token->name.length);
    for (attr_index = 0U; attr_index < token->attribute_count; ++attr_index) {
        rt_write_char(fd, ' ');
        xml_write_raw(fd, token->attributes[attr_index].name.start, token->attributes[attr_index].name.length);
        rt_write_cstr(fd, "=\"");
        xml_write_raw(fd, token->attributes[attr_index].value, token->attributes[attr_index].value_length);
        rt_write_char(fd, '"');
    }
    for (decl_index = 0U; decl_index < dtd->attribute_count; ++decl_index) {
        const XmlDtdAttributeDecl *decl = &dtd->attributes[decl_index];
        if (!xml_name_equals_slice(&token->name, decl->element_name, rt_strlen(decl->element_name))) continue;
        if ((decl->default_kind == XML_DTD_DEFAULT_VALUE || decl->default_kind == XML_DTD_DEFAULT_FIXED) && decl->default_value != 0 && !token_has_attr(token, decl->name, 0, 0)) {
            rt_write_char(fd, ' ');
            rt_write_cstr(fd, decl->name);
            rt_write_cstr(fd, "=\"");
            xml_write_escaped_attr(fd, decl->default_value, rt_strlen(decl->default_value));
            rt_write_char(fd, '"');
        }
    }
    rt_write_cstr(fd, token->type == XML_TOKEN_EMPTY ? "/>" : ">");
}

static const char *default_kind_name(XmlDtdDefaultKind kind) {
    switch (kind) {
        case XML_DTD_DEFAULT_IMPLIED: return "IMPLIED";
        case XML_DTD_DEFAULT_REQUIRED: return "REQUIRED";
        case XML_DTD_DEFAULT_VALUE: return "DEFAULT";
        case XML_DTD_DEFAULT_FIXED: return "FIXED";
        default: return "NONE";
    }
}

void xml_dtd_write_info(int fd, const XmlDtd *dtd) {
    unsigned int index;
    rt_write_cstr(fd, "root ");
    rt_write_cstr(fd, dtd->root_name == 0 ? "-" : dtd->root_name);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "elements "); rt_write_uint(fd, dtd->element_count); rt_write_char(fd, '\n');
    for (index = 0U; index < dtd->element_count; ++index) {
        rt_write_cstr(fd, "    "); rt_write_cstr(fd, dtd->elements[index].name); rt_write_char(fd, ' '); rt_write_cstr(fd, dtd->elements[index].content); rt_write_char(fd, '\n');
    }
    rt_write_cstr(fd, "attributes "); rt_write_uint(fd, dtd->attribute_count); rt_write_char(fd, '\n');
    for (index = 0U; index < dtd->attribute_count; ++index) {
        rt_write_cstr(fd, "    "); rt_write_cstr(fd, dtd->attributes[index].element_name); rt_write_char(fd, ' ');
        rt_write_cstr(fd, dtd->attributes[index].name); rt_write_char(fd, ' '); rt_write_cstr(fd, dtd->attributes[index].type); rt_write_char(fd, ' ');
        rt_write_cstr(fd, default_kind_name(dtd->attributes[index].default_kind));
        if (dtd->attributes[index].default_value != 0) { rt_write_cstr(fd, " \""); rt_write_cstr(fd, dtd->attributes[index].default_value); rt_write_char(fd, '"'); }
        rt_write_char(fd, '\n');
    }
}