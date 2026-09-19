#ifndef NEWOS_XML_H
#define NEWOS_XML_H

#include <stddef.h>

#define XML_INITIAL_ATTRIBUTES 64
#define XML_INITIAL_DEPTH 128
#define XML_MAX_ATTRIBUTES XML_INITIAL_ATTRIBUTES
#define XML_MAX_DEPTH XML_INITIAL_DEPTH
#define XML_NAME_BUFFER_SIZE 128

typedef struct {
    const char *start;
    size_t length;
} XmlName;

typedef struct {
    XmlName name;
    const char *value;
    size_t value_length;
} XmlAttribute;

typedef enum {
    XML_TOKEN_NONE = 0,
    XML_TOKEN_START,
    XML_TOKEN_END,
    XML_TOKEN_EMPTY,
    XML_TOKEN_TEXT,
    XML_TOKEN_CDATA,
    XML_TOKEN_COMMENT,
    XML_TOKEN_PI,
    XML_TOKEN_DOCTYPE
} XmlTokenType;

typedef struct {
    XmlTokenType type;
    const char *raw;
    size_t raw_length;
    XmlName name;
    const char *text;
    size_t text_length;
    XmlAttribute *attributes;
    size_t attribute_count;
    unsigned long long line;
    unsigned long long column;
    unsigned int depth;
    int text_is_blank;
} XmlToken;

typedef struct {
    const char *start;
    size_t length;
    int descendant;
    unsigned int predicate_index;
    unsigned int predicate_count;
} XmlSelectorComponent;

typedef enum {
    XML_SELECTOR_PREDICATE_ATTR_EXISTS = 1,
    XML_SELECTOR_PREDICATE_ATTR_EQUALS,
    XML_SELECTOR_PREDICATE_POSITION
} XmlSelectorPredicateType;

typedef struct {
    XmlSelectorPredicateType type;
    const char *name;
    size_t name_length;
    const char *value;
    size_t value_length;
    unsigned int position;
} XmlSelectorPredicate;

typedef struct {
    XmlSelectorComponent *components;
    XmlSelectorPredicate *predicates;
    unsigned int component_capacity;
    unsigned int component_count;
    unsigned int predicate_capacity;
    unsigned int predicate_count;
    int absolute;
    int has_descendant;
    int has_predicates;
} XmlSelector;

typedef struct {
    XmlName name;
    unsigned int count;
} XmlNameCount;

typedef struct {
    XmlNameCount *counts;
    unsigned int count;
    unsigned int capacity;
} XmlNameCountFrame;

typedef struct {
    XmlName inline_items[XML_INITIAL_DEPTH];
    unsigned int inline_positions[XML_INITIAL_DEPTH];
    XmlName *items;
    unsigned int *positions;
    XmlNameCountFrame *frames;
    unsigned int count;
    unsigned int capacity;
    unsigned int frame_capacity;
    unsigned int position_capacity;
} XmlNameStack;

typedef struct {
    const char *input;
    size_t length;
    size_t pos;
    unsigned long long line;
    unsigned long long column;
    XmlName inline_stack[XML_INITIAL_DEPTH];
    XmlAttribute inline_attributes[XML_INITIAL_ATTRIBUTES];
    XmlName *stack;
    unsigned int stack_capacity;
    XmlAttribute *attributes;
    size_t attribute_capacity;
    unsigned int depth;
    unsigned int root_count;
    char error[160];
    unsigned long long error_line;
    unsigned long long error_column;
} XmlParser;

typedef struct {
    int allow_doctype;
    int allow_pi;
    int allow_comments;
    unsigned int max_depth;
    const char *root_name;
} XmlStreamOptions;

void xml_parser_init(XmlParser *parser, const char *input, size_t length);
void xml_parser_free(XmlParser *parser);
int xml_next_token(XmlParser *parser, XmlToken *token);
int xml_parse_complete(XmlParser *parser);
const char *xml_token_type_name(XmlTokenType type);

int xml_read_document(const char *path, char **buffer_out, size_t *length_out, const char *tool_name);
void xml_free_document(char *buffer);
void xml_report_error(const char *tool_name, const char *path, const XmlParser *parser);
int xml_stream_validate_document(const char *path, const char *tool_name);
int xml_stream_validate_document_with_options(const char *path, const char *tool_name, const XmlStreamOptions *options);

int xml_name_equals(const XmlName *name, const char *text);
int xml_name_equals_slice(const XmlName *name, const char *text, size_t length);
int xml_names_equal(const XmlName *left, const XmlName *right);
int xml_is_name(const char *text);
void xml_copy_name(const XmlName *name, char *buffer, size_t buffer_size);
int xml_copy_slice(const char *text, size_t length, char *buffer, size_t buffer_size);
char *xml_slice_dup(const char *text, size_t length);
void xml_name_stack_init(XmlNameStack *stack);
void xml_name_stack_free(XmlNameStack *stack);
int xml_name_stack_push(XmlNameStack *stack, XmlName name);
void xml_name_stack_pop(XmlNameStack *stack);
void xml_stack_push(XmlName *stack, unsigned int *depth, XmlName name);
void xml_stack_pop(unsigned int *depth);
int xml_token_attr_value(const XmlToken *token, const char *attr_name, char *buffer, size_t buffer_size);
int xml_token_attr_slice(const XmlToken *token, const char *attr_name, const char **value_out, size_t *length_out);
int xml_text_is_blank(const char *text, size_t length);
int xml_write_escaped_text(int fd, const char *text, size_t length);
int xml_write_escaped_attr(int fd, const char *text, size_t length);
int xml_write_raw(int fd, const char *text, size_t length);
int xml_write_indent(int fd, unsigned int depth, unsigned int indent_width);
int xml_selector_attribute(const char *selector, char *attr_name, size_t attr_name_size, char *element_selector, size_t element_selector_size);
int xml_selector_attribute_dup(const char *selector, char **attr_name_out, char **element_selector_out);
int xml_selector_compile(XmlSelector *compiled, const char *selector);
void xml_selector_free(XmlSelector *compiled);
int xml_compiled_path_matches(XmlName *stack, unsigned int depth, const XmlSelector *selector);
int xml_path_matches(XmlName *stack, unsigned int depth, const char *selector);
int xml_name_stack_matches(const XmlNameStack *stack, const char *selector);
int xml_name_stack_matches_compiled(const XmlNameStack *stack, const XmlSelector *selector);
int xml_name_stack_matches_token(const XmlNameStack *stack, const XmlToken *token, const XmlSelector *selector);
void xml_write_path(int fd, XmlName *stack, unsigned int depth);
void xml_write_name_stack_path(int fd, const XmlNameStack *stack);

#endif
