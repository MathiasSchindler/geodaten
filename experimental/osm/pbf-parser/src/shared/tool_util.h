#ifndef NEWOS_TOOL_UTIL_H
#define NEWOS_TOOL_UTIL_H

#include "platform.h"
#include "xml.h"

#include <stddef.h>

int tool_open_input(const char *path, int *fd_out, int *should_close_out);
void tool_close_input(int fd, int should_close);
void tool_write_usage(const char *program_name, const char *usage_suffix);
void tool_write_error(const char *tool_name, const char *message, const char *detail);
int tool_write_visible(int fd, const char *text, size_t length);
int tool_write_visible_line(int fd, const char *text);
int tool_xml_name_stack_push(XmlNameStack *stack, XmlName name, const char *tool_name);

void tool_json_set_enabled(int enabled);
int tool_json_is_enabled(void);
unsigned long long tool_json_next_seq(void);
int tool_json_write_string(int fd, const char *text);
int tool_json_write_string_n(int fd, const char *text, size_t length);
int tool_json_write_base64(int fd, const unsigned char *data, size_t length);
int tool_json_begin_event(int fd, const char *tool_name, const char *stream_name, const char *event_name);
int tool_json_end_event(int fd);
int tool_json_write_diagnostic(const char *tool_name, const char *level, const char *message, const char *detail);
int tool_json_write_usage(const char *tool_name, const char *usage_suffix);

#define TOOL_OUTPUT_BUFFER_SIZE 16384U

typedef struct {
    int fd;
    size_t length;
    char buffer[TOOL_OUTPUT_BUFFER_SIZE];
} ToolOutputBuffer;

void tool_output_buffer_init(ToolOutputBuffer *output, int fd);
int tool_output_buffer_flush(ToolOutputBuffer *output);
int tool_output_buffer_write(ToolOutputBuffer *output, const char *text, size_t length);
int tool_output_buffer_write_char(ToolOutputBuffer *output, char ch);
int tool_output_buffer_write_cstr(ToolOutputBuffer *output, const char *text);

#define TOOL_RECORD_READER_BUFFER_SIZE 4096U

typedef struct {
    int fd;
    char delimiter;
    long chunk_len;
    long chunk_pos;
    int eof;
    char chunk[TOOL_RECORD_READER_BUFFER_SIZE];
} ToolRecordReader;

void tool_record_reader_init(ToolRecordReader *reader, int fd, char delimiter);
int tool_record_reader_next(ToolRecordReader *reader, char *record, size_t record_size, int *has_record_out);

typedef enum {
    TOOL_XML_KEY_ATTR = 1,
    TOOL_XML_KEY_TEXT,
    TOOL_XML_KEY_CHILD
} ToolXmlKeyKind;

typedef struct {
    ToolXmlKeyKind kind;
    const char *name;
} ToolXmlKeySpec;

typedef struct {
    const char *key;
    size_t key_length;
    unsigned int active_child_depth;
    int found;
} ToolXmlKeyState;

int tool_xml_key_parse(const char *text, ToolXmlKeySpec *spec, const char *tool_name);
int tool_xml_selector_compile(XmlSelector *selector_out, const char *selector, const char *tool_name);
void tool_xml_key_state_init(ToolXmlKeyState *state);
void tool_xml_key_start(const ToolXmlKeySpec *spec, const XmlToken *token, unsigned int depth, unsigned int capture_depth, ToolXmlKeyState *state);
void tool_xml_key_text(const ToolXmlKeySpec *spec, const XmlToken *token, unsigned int depth, unsigned int capture_depth, ToolXmlKeyState *state);
void tool_xml_key_end(const ToolXmlKeySpec *spec, unsigned int depth, ToolXmlKeyState *state);

typedef enum {
    TOOL_COLOR_NEVER = 0,
    TOOL_COLOR_AUTO = 1,
    TOOL_COLOR_ALWAYS = 2
} ToolColorMode;

typedef enum {
    TOOL_STYLE_PLAIN = 0,
    TOOL_STYLE_BOLD,
    TOOL_STYLE_RED,
    TOOL_STYLE_GREEN,
    TOOL_STYLE_YELLOW,
    TOOL_STYLE_BLUE,
    TOOL_STYLE_MAGENTA,
    TOOL_STYLE_CYAN,
    TOOL_STYLE_BOLD_RED,
    TOOL_STYLE_BOLD_GREEN,
    TOOL_STYLE_BOLD_YELLOW,
    TOOL_STYLE_BOLD_BLUE,
    TOOL_STYLE_BOLD_MAGENTA,
    TOOL_STYLE_BOLD_CYAN
} ToolTextStyle;

int tool_parse_color_mode(const char *text, int *mode_out);
void tool_set_global_color_mode(int mode);
int tool_get_global_color_mode(void);
int tool_should_use_color_fd(int fd, int mode);
void tool_style_begin(int fd, int mode, int style);
void tool_style_end(int fd, int mode);
void tool_write_styled(int fd, int mode, int style, const char *text);

/*
 * Lightweight iterative option parser.
 *
 * Pattern:
 *   ToolOptState s;
 *   int r;
 *   tool_opt_init(&s, argc, argv, "prog", "[-x] [-f FILE] ...");
 *   while ((r = tool_opt_next(&s)) == TOOL_OPT_FLAG) {
 *       if (rt_strcmp(s.flag, "-f") == 0) {
 *           if (tool_opt_require_value(&s) != 0) return 1;
 *           path = s.value;
 *       } else if (rt_strcmp(s.flag, "-x") == 0) {
 *           opt_x = 1;
 *       } else {
 *           tool_write_error(s.prog, "unknown option: ", s.flag);
 *           tool_write_usage(s.prog, s.usage_suffix);
 *           return 1;
 *       }
 *   }
 *   if (r == TOOL_OPT_HELP) { ... print help ...; return 0; }
 *   if (r == TOOL_OPT_ERROR) return 1;
 *   // s.argi is index of first non-option argument
 */

typedef struct {
    int          argc;
    char       **argv;
    const char  *prog;
    const char  *usage_suffix;
    int          argi;   /* index of next argv entry to examine */
    const char  *flag;   /* current option string set by tool_opt_next */
    const char  *value;  /* option value set by tool_opt_require_value */
} ToolOptState;

#define TOOL_OPT_END   0  /* no more options; s.argi = first positional index */
#define TOOL_OPT_FLAG  1  /* s.flag holds the current option string */
#define TOOL_OPT_HELP  2  /* -h or --help was seen */
#define TOOL_OPT_ERROR 3  /* invalid option; error already written to stderr */

void tool_opt_init(ToolOptState *s, int argc, char **argv,
                   const char *prog, const char *usage_suffix);
int  tool_opt_next(ToolOptState *s);
int  tool_opt_require_value(ToolOptState *s);
int tool_parse_uint_arg(const char *text, unsigned long long *value_out, const char *tool_name, const char *what);
int tool_parse_int_arg(const char *text, long long *value_out, const char *tool_name, const char *what);
int tool_parse_duration_ms(const char *text, unsigned long long *milliseconds_out);
int tool_parse_escaped_string(const char *text, char *buffer, size_t buffer_size, size_t *length_out);
int tool_parse_signal_name(const char *text, int *signal_out);
const char *tool_signal_name(int signal_number);
void tool_write_signal_list(int fd);
int tool_starts_with(const char *text, const char *prefix);
void tool_trim_whitespace(char *text);
int tool_prompt_yes_no(const char *message, const char *path);
const char *tool_base_name(const char *path);
void tool_format_size(unsigned long long value, int human_readable, char *buffer, size_t buffer_size);
int tool_join_path(const char *dir_path, const char *name, char *buffer, size_t buffer_size);
void tool_resolve_host_program_path(char **argv_exec, char *buffer, size_t buffer_size);
int tool_wildcard_match(const char *pattern, const char *text);
int tool_regex_search(const char *pattern, const char *text, int ignore_case, size_t search_start, size_t *start_out, size_t *end_out);
int tool_regex_replace(const char *pattern, const char *replacement, const char *input, int ignore_case, int global, char *output, size_t output_size, int *changed_out);
int tool_regex_search_ex(const char *pattern, const char *text, int ignore_case, int extended, size_t search_start, size_t *start_out, size_t *end_out);
int tool_regex_replace_ex(const char *pattern, const char *replacement, const char *input, int ignore_case, int extended, int global, char *output, size_t output_size, int *changed_out);
int tool_resolve_destination(const char *source_path, const char *dest_path, char *buffer, size_t buffer_size);
int tool_canonicalize_path_policy(const char *path, int resolve_symlinks, int allow_missing, int logical_policy, char *buffer, size_t buffer_size);
int tool_canonicalize_path(const char *path, int resolve_symlinks, int allow_missing, char *buffer, size_t buffer_size);
int tool_path_exists(const char *path);
int tool_paths_equal(const char *left_path, const char *right_path);
int tool_path_is_root(const char *path);
int tool_copy_file(const char *source_path, const char *dest_path);
int tool_copy_path(const char *source_path, const char *dest_path, int recursive, int preserve_mode, int preserve_symlinks);
int tool_remove_path(const char *path, int recursive);

typedef struct {
    int min_depth;
    int max_depth;
} ToolWalkOptions;

typedef struct {
    int prune;
} ToolWalkControl;

typedef int (*ToolWalkCallback)(const char *path, const PlatformDirEntry *entry, int depth, ToolWalkControl *control, void *user_data);

int tool_walk_path(const char *path, const ToolWalkOptions *options, ToolWalkCallback callback, void *user_data);

typedef struct {
    int exact;
    int ignore_case;
    int has_uid;
    unsigned int uid;
    int has_parent;
    int parent_pid;
    int skip_pid;
    const char *pattern;
} ToolProcessMatchOptions;

int tool_resolve_user_id(const char *text, unsigned int *uid_out);
int tool_resolve_group_id(const char *text, unsigned int *gid_out);
int tool_parse_pid(const char *text, int *pid_out);
int tool_process_matches(const PlatformProcessEntry *entry, const ToolProcessMatchOptions *options);

#endif
