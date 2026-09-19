#include "runtime.h"
#include "tool_util.h"

typedef struct {
    const char *starts[10];
    const char *ends[10];
} ToolRegexCaptures;

#define TOOL_REGEX_REPEAT_UNBOUNDED (~0ULL)

static int tool_regex_is_utf8_continuation(unsigned char ch) {
    return (ch & 0xc0U) == 0x80U;
}

static size_t tool_regex_decode_codepoint(const char *text, unsigned int *codepoint_out) {
    size_t index = 0U;
    size_t length;
    unsigned int local_codepoint = 0U;
    unsigned int *target = codepoint_out != 0 ? codepoint_out : &local_codepoint;

    if (text == 0 || text[0] == '\0') {
        if (codepoint_out != 0) {
            *codepoint_out = 0U;
        }
        return 0U;
    }

    length = rt_strlen(text);
    if (rt_utf8_decode(text, length, &index, target) != 0 || index == 0U) {
        if (codepoint_out != 0) {
            *codepoint_out = (unsigned char)text[0];
        }
        return 1U;
    }

    return index;
}

static char tool_regex_to_lower_ascii(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int tool_regex_codepoints_equal(unsigned int lhs, unsigned int rhs, int ignore_case) {
    if (ignore_case) {
        lhs = rt_unicode_simple_fold(lhs);
        rhs = rt_unicode_simple_fold(rhs);
    }
    return lhs == rhs;
}

static int tool_regex_chars_equal(char lhs, char rhs, int ignore_case) {
    return tool_regex_codepoints_equal((unsigned char)lhs, (unsigned char)rhs, ignore_case);
}

static int tool_regex_is_word_char(unsigned int ch) {
    return rt_unicode_is_word(ch);
}

static void tool_regex_copy_captures(ToolRegexCaptures *dst, const ToolRegexCaptures *src) {
    size_t i;
    for (i = 0; i < sizeof(dst->starts) / sizeof(dst->starts[0]); ++i) {
        dst->starts[i] = src->starts[i];
        dst->ends[i] = src->ends[i];
    }
}

static void tool_regex_clear_captures(ToolRegexCaptures *captures) {
    rt_memset(captures, 0, sizeof(*captures));
}

static int tool_regex_decode_escape(char code, char *out) {
    if (out == 0) {
        return -1;
    }

    if (code == 'n') {
        *out = '\n';
    } else if (code == 'r') {
        *out = '\r';
    } else if (code == 't') {
        *out = '\t';
    } else if (code == 'f') {
        *out = '\f';
    } else if (code == 'v') {
        *out = '\v';
    } else {
        *out = code;
    }
    return 0;
}

static int tool_regex_escape_matches(char code, unsigned int ch, int ignore_case) {
    char decoded = '\0';

    if (code == 'd') {
        return ch >= '0' && ch <= '9';
    }
    if (code == 'D') {
        return !(ch >= '0' && ch <= '9');
    }
    if (code == 'w') {
        return tool_regex_is_word_char(ch);
    }
    if (code == 'W') {
        return !tool_regex_is_word_char(ch);
    }
    if (code == 's') {
        return rt_unicode_is_space(ch);
    }
    if (code == 'S') {
        return !rt_unicode_is_space(ch);
    }

    (void)tool_regex_decode_escape(code, &decoded);
    return tool_regex_codepoints_equal((unsigned char)decoded, ch, ignore_case);
}

static size_t tool_regex_skip_class(const char *pattern, size_t pos, size_t end) {
    size_t i = pos + 1U;

    if (i < end && pattern[i] == '^') {
        i += 1U;
    }
    if (i < end && pattern[i] == ']') {
        i += 1U;
    }

    while (i < end) {
        if (pattern[i] == '\\' && i + 1U < end) {
            i += 2U;
            continue;
        }
        if (pattern[i] == ']') {
            return i + 1U;
        }
        i += 1U;
    }

    return pos + 1U;
}

static size_t tool_regex_find_group_end(const char *pattern, size_t pos, size_t end) {
    size_t i = pos + 1U;
    unsigned long long depth = 1ULL;

    while (i < end) {
        if (pattern[i] == '\\' && i + 1U < end) {
            i += 2U;
            continue;
        }
        if (pattern[i] == '[') {
            i = tool_regex_skip_class(pattern, i, end);
            continue;
        }
        if (pattern[i] == '(') {
            depth += 1ULL;
        } else if (pattern[i] == ')') {
            depth -= 1ULL;
            if (depth == 0ULL) {
                return i;
            }
        }
        i += 1U;
    }

    return end;
}

static size_t tool_regex_group_index(const char *pattern, size_t group_pos) {
    size_t i = 0U;
    size_t count = 0U;
    size_t end = rt_strlen(pattern);

    while (i < end && i <= group_pos) {
        if (pattern[i] == '\\' && i + 1U < end) {
            i += 2U;
            continue;
        }
        if (pattern[i] == '[') {
            i = tool_regex_skip_class(pattern, i, end);
            continue;
        }
        if (pattern[i] == '(') {
            count += 1U;
            if (i == group_pos) {
                return count;
            }
        }
        i += 1U;
    }

    return 0U;
}

static size_t tool_regex_atom_span(const char *pattern, size_t pos, size_t end) {
    if (pos >= end) {
        return 0U;
    }
    if (pattern[pos] == '\\' && pos + 1U < end) {
        return 2U;
    }
    if (pattern[pos] == '[') {
        return tool_regex_skip_class(pattern, pos, end) - pos;
    }
    if (pattern[pos] == '(') {
        size_t close = tool_regex_find_group_end(pattern, pos, end);
        if (close < end) {
            return close - pos + 1U;
        }
    }
    if (((unsigned char)pattern[pos] & 0x80U) != 0U) {
        size_t i = pos + 1U;
        while (i < end && tool_regex_is_utf8_continuation((unsigned char)pattern[i])) {
            i += 1U;
        }
        return i - pos;
    }
    return 1U;
}

static int tool_regex_class_matches(const char *pattern, size_t atom_len, unsigned int ch, int ignore_case) {
    size_t i = 1U;
    int negate = 0;
    int matched = 0;
    char compare_ch = (char)(ch <= 0x7fU ? ch : 0);

    if (ignore_case && ch <= 0x7fU) {
        compare_ch = tool_regex_to_lower_ascii(compare_ch);
    }

    if (pattern == 0 || atom_len < 2U || pattern[0] != '[') {
        return 0;
    }

    if (pattern[i] == '^') {
        negate = 1;
        i += 1U;
    }

    while (i + 1U < atom_len) {
        int current_is_special = 0;
        char current = '\0';

        if (pattern[i] == '\\' && i + 2U < atom_len) {
            char code = pattern[i + 1U];
            int special = tool_regex_escape_matches(code, ch, ignore_case);
            if (code == 'd' || code == 'D' || code == 'w' || code == 'W' || code == 's' || code == 'S') {
                if (special) {
                    matched = 1;
                }
                current_is_special = 1;
            } else {
                (void)tool_regex_decode_escape(code, &current);
            }
            i += 2U;
        } else {
            current = pattern[i];
            i += 1U;
        }

        if (current_is_special) {
            continue;
        }

        if (ignore_case) {
            current = tool_regex_to_lower_ascii(current);
        }

        if (i + 1U < atom_len && pattern[i] == '-' && pattern[i + 1U] != ']') {
            char range_end = '\0';

            i += 1U;
            if (pattern[i] == '\\' && i + 1U < atom_len) {
                (void)tool_regex_decode_escape(pattern[i + 1U], &range_end);
                i += 2U;
            } else {
                range_end = pattern[i];
                i += 1U;
            }

            if (ignore_case) {
                range_end = tool_regex_to_lower_ascii(range_end);
            }

            if (current <= compare_ch && compare_ch <= range_end) {
                matched = 1;
            }
        } else if (current == compare_ch) {
            matched = 1;
        }
    }

    return negate ? !matched : matched;
}

static int tool_regex_match_expression(const char *pattern,
                                       size_t pos,
                                       size_t end,
                                       const char *text,
                                       const char *origin,
                                       int ignore_case,
                                       ToolRegexCaptures *captures,
                                       const char **end_out);

static int tool_regex_match_sequence_with_continuation(const char *pattern,
                                                       size_t pos,
                                                       size_t end,
                                                       size_t continuation_pos,
                                                       size_t continuation_end,
                                                       const char *text,
                                                       const char *origin,
                                                       int ignore_case,
                                                       ToolRegexCaptures *captures,
                                                       size_t capture_index,
                                                       const char *capture_start,
                                                       const char **segment_end_out,
                                                       const char **end_out);

static int tool_regex_match_group_with_continuation(const char *pattern,
                                                    size_t pos,
                                                    size_t atom_end,
                                                    size_t rest_pos,
                                                    size_t end,
                                                    const char *text,
                                                    const char *origin,
                                                    int ignore_case,
                                                    ToolRegexCaptures *captures,
                                                    const char **end_out);

static int tool_regex_match_atom(const char *pattern,
                                 size_t pos,
                                 size_t atom_end,
                                 const char *text,
                                 const char *origin,
                                 int ignore_case,
                                 ToolRegexCaptures *captures,
                                 const char **next_text_out) {
    unsigned int text_codepoint = 0U;
    size_t text_advance = 0U;
    if (pos >= atom_end) {
        return 0;
    }

    if (pattern[pos] == '^' && atom_end == pos + 1U) {
        if (text == origin) {
            *next_text_out = text;
            return 1;
        }
        return 0;
    }

    if (pattern[pos] == '$' && atom_end == pos + 1U) {
        if (*text == '\0') {
            *next_text_out = text;
            return 1;
        }
        return 0;
    }

    if (pattern[pos] == '(' && atom_end > pos + 1U && pattern[atom_end - 1U] == ')') {
        ToolRegexCaptures local;
        const char *group_end = 0;
        size_t group_index = tool_regex_group_index(pattern, pos);

        tool_regex_copy_captures(&local, captures);
        if (!tool_regex_match_expression(pattern, pos + 1U, atom_end - 1U, text, origin, ignore_case, &local, &group_end)) {
            return 0;
        }
        if (group_index < sizeof(local.starts) / sizeof(local.starts[0])) {
            local.starts[group_index] = text;
            local.ends[group_index] = group_end;
        }
        *captures = local;
        *next_text_out = group_end;
        return 1;
    }

    if (pattern[pos] == '\\' && pos + 1U < atom_end && pattern[pos + 1U] >= '0' && pattern[pos + 1U] <= '9') {
        size_t capture_index = (size_t)(pattern[pos + 1U] - '0');
        const char *capture_start = captures->starts[capture_index];
        const char *capture_end = captures->ends[capture_index];
        size_t i = 0U;
        size_t length;

        if (capture_start == 0 || capture_end == 0 || capture_end < capture_start) {
            return 0;
        }
        length = (size_t)(capture_end - capture_start);
        while (i < length) {
            if (text[i] == '\0' || !tool_regex_chars_equal(capture_start[i], text[i], ignore_case)) {
                return 0;
            }
            i += 1U;
        }
        *next_text_out = text + length;
        return 1;
    }

    if (*text == '\0') {
        return 0;
    }

    text_advance = tool_regex_decode_codepoint(text, &text_codepoint);
    if (text_advance == 0U) {
        return 0;
    }

    if (pattern[pos] == '.' && atom_end == pos + 1U) {
        *next_text_out = text + text_advance;
        return 1;
    }
    if (pattern[pos] == '[') {
        if (tool_regex_class_matches(pattern + pos, atom_end - pos, text_codepoint, ignore_case)) {
            *next_text_out = text + text_advance;
            return 1;
        }
        return 0;
    }
    if (pattern[pos] == '\\' && pos + 1U < atom_end) {
        if (tool_regex_escape_matches(pattern[pos + 1U], text_codepoint, ignore_case)) {
            *next_text_out = text + text_advance;
            return 1;
        }
        return 0;
    }

    {
        unsigned int pattern_codepoint = 0U;
        size_t pattern_advance = tool_regex_decode_codepoint(pattern + pos, &pattern_codepoint);

        if (pattern_advance > 0U && tool_regex_codepoints_equal(pattern_codepoint, text_codepoint, ignore_case)) {
            *next_text_out = text + text_advance;
            return 1;
        }
    }

    return 0;
}

static int tool_regex_parse_repeat_count(const char *pattern, size_t *pos, size_t end, unsigned long long *value_out) {
    unsigned long long value = 0ULL;
    size_t index = *pos;
    int saw_digit = 0;

    while (index < end && pattern[index] >= '0' && pattern[index] <= '9') {
        saw_digit = 1;
        value = (value * 10ULL) + (unsigned long long)(pattern[index] - '0');
        index += 1U;
    }

    if (!saw_digit) {
        return 0;
    }

    *pos = index;
    *value_out = value;
    return 1;
}

static int tool_regex_parse_quantifier(const char *pattern,
                                       size_t pos,
                                       size_t end,
                                       unsigned long long *min_out,
                                       unsigned long long *max_out,
                                       size_t *quantifier_end_out) {
    if (pos >= end) {
        return 0;
    }

    if (pattern[pos] == '*') {
        *min_out = 0ULL;
        *max_out = TOOL_REGEX_REPEAT_UNBOUNDED;
        *quantifier_end_out = pos + 1U;
        return 1;
    }
    if (pattern[pos] == '+') {
        *min_out = 1ULL;
        *max_out = TOOL_REGEX_REPEAT_UNBOUNDED;
        *quantifier_end_out = pos + 1U;
        return 1;
    }
    if (pattern[pos] == '?') {
        *min_out = 0ULL;
        *max_out = 1ULL;
        *quantifier_end_out = pos + 1U;
        return 1;
    }
    if (pattern[pos] == '{') {
        size_t index = pos + 1U;
        unsigned long long minimum = 0ULL;
        unsigned long long maximum = 0ULL;
        int has_minimum = 0;
        int has_maximum = 0;

        has_minimum = tool_regex_parse_repeat_count(pattern, &index, end, &minimum);
        if (!has_minimum) {
            return 0;
        }

        if (index < end && pattern[index] == '}') {
            *min_out = minimum;
            *max_out = minimum;
            *quantifier_end_out = index + 1U;
            return 1;
        }

        if (index >= end || pattern[index] != ',') {
            return 0;
        }
        index += 1U;

        has_maximum = tool_regex_parse_repeat_count(pattern, &index, end, &maximum);
        if (index >= end || pattern[index] != '}') {
            return 0;
        }

        *min_out = minimum;
        *max_out = has_maximum ? maximum : TOOL_REGEX_REPEAT_UNBOUNDED;
        *quantifier_end_out = index + 1U;
        return *max_out == TOOL_REGEX_REPEAT_UNBOUNDED || *max_out >= *min_out;
    }

    return 0;
}

static int tool_regex_match_quantified(const char *pattern,
                                       size_t pos,
                                       size_t atom_end,
                                       size_t rest_pos,
                                       size_t end,
                                       const char *text,
                                       const char *origin,
                                       int ignore_case,
                                       unsigned long long min_count,
                                       unsigned long long max_count,
                                       unsigned long long count,
                                       ToolRegexCaptures *captures,
                                       const char **end_out);

static int tool_regex_match_sequence(const char *pattern,
                                     size_t pos,
                                     size_t end,
                                     const char *text,
                                     const char *origin,
                                     int ignore_case,
                                     ToolRegexCaptures *captures,
                                     const char **end_out) {
    size_t atom_len;
    size_t atom_end;
    size_t rest_pos;
    unsigned long long min_count = 1ULL;
    unsigned long long max_count = 1ULL;
    size_t quantifier_end = 0U;

    if (pos >= end) {
        *end_out = text;
        return 1;
    }

    atom_len = tool_regex_atom_span(pattern, pos, end);
    if (atom_len == 0U) {
        *end_out = text;
        return 1;
    }

    atom_end = pos + atom_len;
    rest_pos = atom_end;
    if (tool_regex_parse_quantifier(pattern, atom_end, end, &min_count, &max_count, &quantifier_end)) {
        rest_pos = quantifier_end;
        return tool_regex_match_quantified(pattern,
                                           pos,
                                           atom_end,
                                           rest_pos,
                                           end,
                                           text,
                                           origin,
                                           ignore_case,
                                           min_count,
                                           max_count,
                                           0ULL,
                                           captures,
                                           end_out);
    }

    if (pattern[pos] == '(' && atom_end > pos + 1U && pattern[atom_end - 1U] == ')' && rest_pos < end) {
        return tool_regex_match_group_with_continuation(pattern,
                                                        pos,
                                                        atom_end,
                                                        rest_pos,
                                                        end,
                                                        text,
                                                        origin,
                                                        ignore_case,
                                                        captures,
                                                        end_out);
    }

    {
        ToolRegexCaptures local;
        const char *next = 0;

        tool_regex_copy_captures(&local, captures);
        if (!tool_regex_match_atom(pattern, pos, atom_end, text, origin, ignore_case, &local, &next)) {
            return 0;
        }
        if (tool_regex_match_sequence(pattern, rest_pos, end, next, origin, ignore_case, &local, end_out)) {
            *captures = local;
            return 1;
        }
        return 0;
    }
}

static int tool_regex_match_quantified(const char *pattern,
                                       size_t pos,
                                       size_t atom_end,
                                       size_t rest_pos,
                                       size_t end,
                                       const char *text,
                                       const char *origin,
                                       int ignore_case,
                                       unsigned long long min_count,
                                       unsigned long long max_count,
                                       unsigned long long count,
                                       ToolRegexCaptures *captures,
                                       const char **end_out) {
    if (max_count == TOOL_REGEX_REPEAT_UNBOUNDED || count < max_count) {
        ToolRegexCaptures local;
        const char *next = 0;

        tool_regex_copy_captures(&local, captures);
        if (tool_regex_match_atom(pattern, pos, atom_end, text, origin, ignore_case, &local, &next) &&
            next != text &&
            tool_regex_match_quantified(pattern,
                                        pos,
                                        atom_end,
                                        rest_pos,
                                        end,
                                        next,
                                        origin,
                                        ignore_case,
                                        min_count,
                                        max_count,
                                        count + 1ULL,
                                        &local,
                                        end_out)) {
            *captures = local;
            return 1;
        }
    }

    if (count >= min_count) {
        return tool_regex_match_sequence(pattern, rest_pos, end, text, origin, ignore_case, captures, end_out);
    }

    return 0;
}

static int tool_regex_match_quantified_with_continuation(const char *pattern,
                                                         size_t pos,
                                                         size_t atom_end,
                                                         size_t rest_pos,
                                                         size_t end,
                                                         size_t continuation_pos,
                                                         size_t continuation_end,
                                                         const char *text,
                                                         const char *origin,
                                                         int ignore_case,
                                                         unsigned long long min_count,
                                                         unsigned long long max_count,
                                                         unsigned long long count,
                                                         ToolRegexCaptures *captures,
                                                         size_t capture_index,
                                                         const char *capture_start,
                                                         const char **segment_end_out,
                                                         const char **end_out) {
    if (max_count == TOOL_REGEX_REPEAT_UNBOUNDED || count < max_count) {
        ToolRegexCaptures local;
        const char *next = 0;
        const char *segment_end = 0;

        tool_regex_copy_captures(&local, captures);
            if (tool_regex_match_atom(pattern, pos, atom_end, text, origin, ignore_case, &local, &next) &&
            next != text &&
            tool_regex_match_quantified_with_continuation(pattern,
                                                          pos,
                                                          atom_end,
                                                          rest_pos,
                                                          end,
                                                          continuation_pos,
                                                          continuation_end,
                                                          next,
                                                          origin,
                                                          ignore_case,
                                                          min_count,
                                                          max_count,
                                                          count + 1ULL,
                                                          &local,
                                                          capture_index,
                                                          capture_start,
                                                          &segment_end,
                                                          end_out)) {
            *captures = local;
            if (segment_end_out != 0) {
                *segment_end_out = segment_end;
            }
            return 1;
        }
    }

    if (count >= min_count) {
        if (capture_start != 0 && capture_index < sizeof(captures->starts) / sizeof(captures->starts[0])) {
            captures->starts[capture_index] = capture_start;
            captures->ends[capture_index] = text;
        }
        return tool_regex_match_sequence_with_continuation(pattern,
                                                           rest_pos,
                                                           end,
                                                           continuation_pos,
                                                           continuation_end,
                                                           text,
                                                           origin,
                                                           ignore_case,
                                                           captures,
                                                           capture_index,
                                                           capture_start,
                                                           segment_end_out,
                                                           end_out);
    }

    return 0;
}

static int tool_regex_match_group_with_continuation(const char *pattern,
                                                    size_t pos,
                                                    size_t atom_end,
                                                    size_t rest_pos,
                                                    size_t end,
                                                    const char *text,
                                                    const char *origin,
                                                    int ignore_case,
                                                    ToolRegexCaptures *captures,
                                                    const char **end_out) {
    size_t branch_start = pos + 1U;
    size_t i = pos + 1U;
    size_t group_index = tool_regex_group_index(pattern, pos);
    unsigned long long depth = 0ULL;
    size_t group_end = atom_end - 1U;

    while (i < group_end) {
        if (pattern[i] == '\\' && i + 1U < group_end) {
            i += 2U;
            continue;
        }
        if (pattern[i] == '[') {
            i = tool_regex_skip_class(pattern, i, group_end);
            continue;
        }
        if (pattern[i] == '(') {
            depth += 1ULL;
            i += 1U;
            continue;
        }
        if (pattern[i] == ')') {
            if (depth > 0ULL) {
                depth -= 1ULL;
            }
            i += 1U;
            continue;
        }
        if (pattern[i] == '|' && depth == 0ULL) {
            ToolRegexCaptures local;
            const char *group_match_end = 0;
            const char *full_end = 0;

            tool_regex_copy_captures(&local, captures);
            if (tool_regex_match_sequence_with_continuation(pattern,
                                                            branch_start,
                                                            i,
                                                            rest_pos,
                                                            end,
                                                            text,
                                                            origin,
                                                            ignore_case,
                                                            &local,
                                                            group_index,
                                                            text,
                                                            &group_match_end,
                                                            &full_end)) {
                if (group_index < sizeof(local.starts) / sizeof(local.starts[0])) {
                    local.starts[group_index] = text;
                    local.ends[group_index] = group_match_end;
                }
                *captures = local;
                *end_out = full_end;
                return 1;
            }
            branch_start = i + 1U;
        }
        i += 1U;
    }

    {
        ToolRegexCaptures local;
        const char *group_match_end = 0;
        const char *full_end = 0;

        tool_regex_copy_captures(&local, captures);
        if (tool_regex_match_sequence_with_continuation(pattern,
                                                        branch_start,
                                                        group_end,
                                                        rest_pos,
                                                        end,
                                                        text,
                                                        origin,
                                                        ignore_case,
                                                        &local,
                                                        group_index,
                                                        text,
                                                        &group_match_end,
                                                        &full_end)) {
            if (group_index < sizeof(local.starts) / sizeof(local.starts[0])) {
                local.starts[group_index] = text;
                local.ends[group_index] = group_match_end;
            }
            *captures = local;
            *end_out = full_end;
            return 1;
        }
    }

    return 0;
}

static int tool_regex_match_sequence_with_continuation(const char *pattern,
                                                       size_t pos,
                                                       size_t end,
                                                       size_t continuation_pos,
                                                       size_t continuation_end,
                                                       const char *text,
                                                       const char *origin,
                                                       int ignore_case,
                                                       ToolRegexCaptures *captures,
                                                       size_t capture_index,
                                                       const char *capture_start,
                                                       const char **segment_end_out,
                                                       const char **end_out) {
    size_t atom_len;
    size_t atom_end;
    size_t rest_pos;
    unsigned long long min_count = 1ULL;
    unsigned long long max_count = 1ULL;
    size_t quantifier_end = 0U;

    if (pos >= end) {
        if (segment_end_out != 0) {
            *segment_end_out = text;
        }
        if (capture_start != 0 && capture_index < sizeof(captures->starts) / sizeof(captures->starts[0])) {
            captures->starts[capture_index] = capture_start;
            captures->ends[capture_index] = text;
        }
        return tool_regex_match_sequence(pattern,
                                         continuation_pos,
                                         continuation_end,
                                         text,
                                         origin,
                                         ignore_case,
                                         captures,
                                         end_out);
    }

    atom_len = tool_regex_atom_span(pattern, pos, end);
    if (atom_len == 0U) {
        if (segment_end_out != 0) {
            *segment_end_out = text;
        }
        if (capture_start != 0 && capture_index < sizeof(captures->starts) / sizeof(captures->starts[0])) {
            captures->starts[capture_index] = capture_start;
            captures->ends[capture_index] = text;
        }
        return tool_regex_match_sequence(pattern,
                                         continuation_pos,
                                         continuation_end,
                                         text,
                                         origin,
                                         ignore_case,
                                         captures,
                                         end_out);
    }

    atom_end = pos + atom_len;
    rest_pos = atom_end;
    if (tool_regex_parse_quantifier(pattern, atom_end, end, &min_count, &max_count, &quantifier_end)) {
        rest_pos = quantifier_end;
        return tool_regex_match_quantified_with_continuation(pattern,
                                                             pos,
                                                             atom_end,
                                                             rest_pos,
                                                             end,
                                                             continuation_pos,
                                                             continuation_end,
                                                             text,
                                                             origin,
                                                             ignore_case,
                                                              min_count,
                                                              max_count,
                                                              0ULL,
                                                              captures,
                                                              capture_index,
                                                              capture_start,
                                                              segment_end_out,
                                                              end_out);
    }

    {
        ToolRegexCaptures local;
        const char *next = 0;
        const char *segment_end = 0;

        tool_regex_copy_captures(&local, captures);
        if (!tool_regex_match_atom(pattern, pos, atom_end, text, origin, ignore_case, &local, &next)) {
            return 0;
        }
        if (tool_regex_match_sequence_with_continuation(pattern,
                                                        rest_pos,
                                                        end,
                                                        continuation_pos,
                                                        continuation_end,
                                                        next,
                                                        origin,
                                                        ignore_case,
                                                        &local,
                                                        capture_index,
                                                        capture_start,
                                                        &segment_end,
                                                        end_out)) {
            *captures = local;
            if (segment_end_out != 0) {
                *segment_end_out = segment_end;
            }
            return 1;
        }
        return 0;
    }
}

static int tool_regex_match_expression(const char *pattern,
                                       size_t pos,
                                       size_t end,
                                       const char *text,
                                       const char *origin,
                                       int ignore_case,
                                       ToolRegexCaptures *captures,
                                       const char **end_out) {
    size_t branch_start = pos;
    size_t i = pos;
    unsigned long long depth = 0ULL;

    while (i < end) {
        if (pattern[i] == '\\' && i + 1U < end) {
            i += 2U;
            continue;
        }
        if (pattern[i] == '[') {
            i = tool_regex_skip_class(pattern, i, end);
            continue;
        }
        if (pattern[i] == '(') {
            depth += 1ULL;
            i += 1U;
            continue;
        }
        if (pattern[i] == ')') {
            if (depth > 0ULL) {
                depth -= 1ULL;
            }
            i += 1U;
            continue;
        }
        if (pattern[i] == '|' && depth == 0ULL) {
            ToolRegexCaptures local;

            tool_regex_copy_captures(&local, captures);
            if (tool_regex_match_sequence(pattern, branch_start, i, text, origin, ignore_case, &local, end_out)) {
                *captures = local;
                return 1;
            }
            branch_start = i + 1U;
        }
        i += 1U;
    }

    {
        ToolRegexCaptures local;

        tool_regex_copy_captures(&local, captures);
        if (tool_regex_match_sequence(pattern, branch_start, end, text, origin, ignore_case, &local, end_out)) {
            *captures = local;
            return 1;
        }
    }

    return 0;
}

static int tool_regex_search_internal(const char *pattern,
                                      const char *text,
                                      int ignore_case,
                                      size_t search_start,
                                      size_t *start_out,
                                      size_t *end_out,
                                      ToolRegexCaptures *captures_out) {
    size_t pos = search_start;
    size_t pattern_end = rt_strlen(pattern);

    if (pattern == 0 || text == 0 || start_out == 0 || end_out == 0) {
        return 0;
    }

    while (1) {
        ToolRegexCaptures captures;
        const char *end_ptr = 0;

        tool_regex_clear_captures(&captures);
        if (tool_regex_match_expression(pattern, 0U, pattern_end, text + pos, text, ignore_case, &captures, &end_ptr)) {
            captures.starts[0] = text + pos;
            captures.ends[0] = end_ptr;
            *start_out = pos;
            *end_out = (size_t)(end_ptr - text);
            if (captures_out != 0) {
                *captures_out = captures;
            }
            return 1;
        }

        if (text[pos] == '\0') {
            break;
        }
        if (tool_regex_is_utf8_continuation((unsigned char)text[pos])) {
            pos += 1U;
        } else {
            size_t advance = tool_regex_decode_codepoint(text + pos, 0);
            pos += advance > 0U ? advance : 1U;
        }
    }

    return 0;
}

int tool_regex_search(const char *pattern, const char *text, int ignore_case, size_t search_start, size_t *start_out, size_t *end_out) {
    return tool_regex_search_internal(pattern, text, ignore_case, search_start, start_out, end_out, 0);
}

int tool_regex_search_ex(const char *pattern,
                         const char *text,
                         int ignore_case,
                         int extended,
                         size_t search_start,
                         size_t *start_out,
                         size_t *end_out) {
    (void)extended;
    return tool_regex_search_internal(pattern, text, ignore_case, search_start, start_out, end_out, 0);
}

static int tool_regex_append_text(char *buffer, size_t buffer_size, size_t *used, const char *text, size_t length) {
    if (*used + length + 1U > buffer_size) {
        return -1;
    }
    if (length > 0U) {
        memcpy(buffer + *used, text, length);
        *used += length;
    }
    buffer[*used] = '\0';
    return 0;
}

static int tool_regex_append_replacement(char *buffer,
                                         size_t buffer_size,
                                         size_t *used,
                                         const char *replacement,
                                         const ToolRegexCaptures *captures) {
    size_t i = 0U;

    while (replacement != 0 && replacement[i] != '\0') {
        if (replacement[i] == '&') {
            if (captures != 0 && captures->starts[0] != 0 && captures->ends[0] != 0 &&
                tool_regex_append_text(buffer, buffer_size, used, captures->starts[0], (size_t)(captures->ends[0] - captures->starts[0])) != 0) {
                return -1;
            }
            i += 1U;
            continue;
        }

        if (replacement[i] == '\\' && replacement[i + 1U] != '\0') {
            char code = replacement[i + 1U];
            char decoded = '\0';

            if (code >= '0' && code <= '9') {
                size_t capture_index = (size_t)(code - '0');
                if (captures != 0 && captures->starts[capture_index] != 0 && captures->ends[capture_index] != 0 &&
                    tool_regex_append_text(buffer,
                                           buffer_size,
                                           used,
                                           captures->starts[capture_index],
                                           (size_t)(captures->ends[capture_index] - captures->starts[capture_index])) != 0) {
                    return -1;
                }
            } else {
                if (code == '&' || code == '\\') {
                    decoded = code;
                } else {
                    (void)tool_regex_decode_escape(code, &decoded);
                }
                if (tool_regex_append_text(buffer, buffer_size, used, &decoded, 1U) != 0) {
                    return -1;
                }
            }
            i += 2U;
            continue;
        }

        if (tool_regex_append_text(buffer, buffer_size, used, replacement + i, 1U) != 0) {
            return -1;
        }
        i += 1U;
    }

    return 0;
}

int tool_regex_replace(const char *pattern,
                       const char *replacement,
                       const char *input,
                       int ignore_case,
                       int global,
                       char *output,
                       size_t output_size,
                       int *changed_out) {
    size_t in_pos = 0U;
    size_t out_pos = 0U;
    int changed = 0;

    if (pattern == 0 || replacement == 0 || input == 0 || output == 0 || output_size == 0U) {
        return -1;
    }

    if (changed_out != 0) {
        *changed_out = 0;
    }

    output[0] = '\0';
    if (pattern[0] == '\0') {
        return tool_regex_append_text(output, output_size, &out_pos, input, rt_strlen(input));
    }

    while (1) {
        size_t match_start = 0U;
        size_t match_end = 0U;
        ToolRegexCaptures captures;

        if (!tool_regex_search_internal(pattern, input, ignore_case, in_pos, &match_start, &match_end, &captures)) {
            break;
        }

        if (tool_regex_append_text(output, output_size, &out_pos, input + in_pos, match_start - in_pos) != 0 ||
            tool_regex_append_replacement(output, output_size, &out_pos, replacement, &captures) != 0) {
            return -1;
        }

        changed = 1;
        if (!global) {
            in_pos = match_end;
            break;
        }

        if (match_end > match_start) {
            in_pos = match_end;
        } else {
            if (input[in_pos] == '\0') {
                break;
            }
            if (tool_regex_append_text(output, output_size, &out_pos, input + in_pos, 1U) != 0) {
                return -1;
            }
            in_pos += 1U;
        }
    }

    if (tool_regex_append_text(output, output_size, &out_pos, input + in_pos, rt_strlen(input + in_pos)) != 0) {
        return -1;
    }

    if (changed_out != 0) {
        *changed_out = changed;
    }
    return 0;
}

int tool_regex_replace_ex(const char *pattern,
                          const char *replacement,
                          const char *input,
                          int ignore_case,
                          int extended,
                          int global,
                          char *output,
                          size_t output_size,
                          int *changed_out) {
    (void)extended;
    return tool_regex_replace(pattern, replacement, input, ignore_case, global, output, output_size, changed_out);
}

int tool_wildcard_match(const char *pattern, const char *text) {
    if (pattern[0] == '\0') {
        return text[0] == '\0';
    }

    if (pattern[0] == '*') {
        return tool_wildcard_match(pattern + 1, text) || (text[0] != '\0' && tool_wildcard_match(pattern, text + 1));
    }

    if (pattern[0] == '?') {
        return text[0] != '\0' && tool_wildcard_match(pattern + 1, text + 1);
    }

    return pattern[0] == text[0] && tool_wildcard_match(pattern + 1, text + 1);
}
