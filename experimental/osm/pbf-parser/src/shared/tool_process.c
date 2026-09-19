#include "runtime.h"
#include "tool_util.h"

static char tool_ascii_fold(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

static int tool_strings_equal_ignore_case(const char *left, const char *right) {
    size_t i = 0U;

    while (left[i] != '\0' && right[i] != '\0') {
        if (tool_ascii_fold(left[i]) != tool_ascii_fold(right[i])) {
            return 0;
        }
        i += 1U;
    }
    return left[i] == '\0' && right[i] == '\0';
}

int tool_resolve_user_id(const char *text, unsigned int *uid_out) {
    unsigned long long value = 0ULL;
    PlatformIdentity identity;

    if (text == 0 || uid_out == 0) {
        return -1;
    }
    if (rt_parse_uint(text, &value) == 0) {
        *uid_out = (unsigned int)value;
        return 0;
    }
    if (platform_lookup_identity(text, &identity) != 0) {
        return -1;
    }
    *uid_out = identity.uid;
    return 0;
}

int tool_resolve_group_id(const char *text, unsigned int *gid_out) {
    unsigned long long value = 0ULL;

    if (text == 0 || gid_out == 0) {
        return -1;
    }
    if (rt_parse_uint(text, &value) == 0) {
        *gid_out = (unsigned int)value;
        return 0;
    }
    return platform_lookup_group(text, gid_out);
}

int tool_parse_pid(const char *text, int *pid_out) {
    unsigned long long value = 0ULL;

    if (text == 0 || pid_out == 0 || rt_parse_uint(text, &value) != 0 || value > 2147483647ULL) {
        return -1;
    }
    *pid_out = (int)value;
    return 0;
}

int tool_process_matches(const PlatformProcessEntry *entry, const ToolProcessMatchOptions *options) {
    const char *name;
    const char *base;
    size_t start = 0U;
    size_t end = 0U;

    if (entry == 0 || options == 0 || options->pattern == 0) {
        return 0;
    }
    if (options->skip_pid > 0 && entry->pid == options->skip_pid) {
        return 0;
    }
    if (options->has_uid && entry->uid != options->uid) {
        return 0;
    }
    if (options->has_parent && entry->ppid != options->parent_pid) {
        return 0;
    }

    name = entry->name;
    base = tool_base_name(entry->name);

    if (options->exact) {
        if (options->ignore_case) {
            return tool_strings_equal_ignore_case(options->pattern, name) ||
                   tool_strings_equal_ignore_case(options->pattern, base);
        }
        return rt_strcmp(options->pattern, name) == 0 || rt_strcmp(options->pattern, base) == 0;
    }

        return tool_regex_search(options->pattern, name, options->ignore_case, 0U, &start, &end) != 0 ||
            tool_regex_search(options->pattern, base, options->ignore_case, 0U, &start, &end) != 0;
}
