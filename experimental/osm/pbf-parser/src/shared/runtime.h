#ifndef NEWOS_RUNTIME_H
#define NEWOS_RUNTIME_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t count);
int memcmp(const void *left, const void *right, size_t count);
void *memmove(void *dst, const void *src, size_t count);
void *memset(void *buffer, int byte_value, size_t count);

void *rt_malloc(size_t size);
void *rt_realloc(void *ptr, size_t size);
void rt_free(void *ptr);
void rt_sort(void *base, size_t count, size_t item_size, int (*compare)(const void *, const void *));

size_t rt_strlen(const char *text);
int rt_strcmp(const char *lhs, const char *rhs);
int rt_strncmp(const char *lhs, const char *rhs, size_t count);
void rt_copy_string(char *dst, size_t dst_size, const char *src);
int rt_join_path(const char *dir_path, const char *name, char *buffer, size_t buffer_size);
void rt_unsigned_to_string(unsigned long long value, char *buffer, size_t buffer_size);
void rt_memset(void *buffer, int byte_value, size_t count);
int rt_is_space(char ch);
int rt_is_digit_string(const char *text);
int rt_parse_pid_value(const char *text);
void rt_trim_newline(char *text);
int rt_parse_uint(const char *text, unsigned long long *value_out);
int rt_utf8_decode(const char *text, size_t text_length, size_t *index_io, unsigned int *codepoint_out);
int rt_utf8_validate(const char *text, size_t text_length);
unsigned long long rt_utf8_codepoint_count(const char *text, size_t text_length);
int rt_utf8_encode(unsigned int codepoint, char *buffer, size_t buffer_size, size_t *length_out);
unsigned int rt_unicode_simple_fold(unsigned int codepoint);
int rt_unicode_is_space(unsigned int codepoint);
int rt_unicode_is_word(unsigned int codepoint);
unsigned int rt_unicode_display_width(unsigned int codepoint);

#define RT_TEXT_SEGMENT_ANSI            (1U << 0)
#define RT_TEXT_SEGMENT_INCOMPLETE      (1U << 1)
#define RT_TEXT_SEGMENT_INVALID         (1U << 2)
#define RT_TEXT_SEGMENT_BACKSPACE       (1U << 3)
#define RT_TEXT_SEGMENT_CARRIAGE_RETURN (1U << 4)

typedef struct {
	size_t start;
	size_t end;
	unsigned int codepoint;
	unsigned int display_width;
	unsigned int flags;
} RtTextSegment;

int rt_text_next_segment(const char *text, size_t text_length, size_t start, RtTextSegment *segment_out);
int rt_text_has_incomplete_tail(const char *text, size_t text_length);
unsigned long long rt_text_apply_segment_width(unsigned long long current_width, const RtTextSegment *segment);
unsigned long long rt_text_apply_segment_width_tabstop(unsigned long long current_width, const RtTextSegment *segment, unsigned int tab_width);
unsigned long long rt_text_display_width_n(const char *text, size_t text_length, unsigned long long initial_width);
unsigned long long rt_text_display_width_n_tabstop(const char *text, size_t text_length, unsigned long long initial_width, unsigned int tab_width);
size_t rt_text_prefix_bytes_for_width(const char *text, size_t text_length, unsigned long long max_width, unsigned long long initial_width);
int rt_text_segment_is_space(const char *text, size_t text_length, const RtTextSegment *segment);

int rt_write_all(int fd, const void *data, size_t count);
int rt_write_cstr(int fd, const char *text);
int rt_write_line(int fd, const char *text);
int rt_write_char(int fd, char ch);
int rt_write_uint(int fd, unsigned long long value);
int rt_write_int(int fd, long long value);

#endif
