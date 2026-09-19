/*
 * tool_cli.c - lightweight iterative option parser for newos tools.
 *
 * See tool_util.h (ToolOptState) for the API and usage pattern.
 */

#include "runtime.h"
#include "tool_util.h"

void tool_opt_init(ToolOptState *s, int argc, char **argv,
                   const char *prog, const char *usage_suffix) {
    s->argc = argc;
    s->argv = argv;
    s->prog = prog;
    s->usage_suffix = usage_suffix;
    s->argi = 1;
    s->flag = 0;
    s->value = 0;
}

/*
 * Advance to the next option.  Returns one of:
 *   TOOL_OPT_END   - no more options; s->argi = first positional index
 *   TOOL_OPT_FLAG  - s->flag = current option string; argi advanced past it
 *   TOOL_OPT_HELP  - -h / --help was seen
 *   TOOL_OPT_ERROR - reserved for future use (currently never returned here)
 *
 * Stops at the first argument that does not begin with '-', at a bare '-'
 * (stdin placeholder), or at the "--" end-of-options marker.
 */
int tool_opt_next(ToolOptState *s) {
    const char *arg;

    while (1) {
        if (s->argi >= s->argc) {
            return TOOL_OPT_END;
        }

        arg = s->argv[s->argi];

        /* bare "--" ends option scanning */
        if (arg[0] == '-' && arg[1] == '-' && arg[2] == '\0') {
            s->argi += 1;
            return TOOL_OPT_END;
        }

        /* not a flag: single '-' is a stdin placeholder, not an option */
        if (arg[0] != '-' || arg[1] == '\0') {
            return TOOL_OPT_END;
        }

        s->flag = arg;
        s->argi += 1;

        if (rt_strcmp(arg, "--json") == 0) {
            tool_json_set_enabled(1);
            continue;
        }

        /* unified help detection */
        if ((arg[1] == 'h' && arg[2] == '\0') || rt_strcmp(arg, "--help") == 0) {
            return TOOL_OPT_HELP;
        }

        return TOOL_OPT_FLAG;
    }
}

/*
 * Consume the next argv entry as the value for the current option.
 * Writes an error and returns -1 if no argument is available.
 */
int tool_opt_require_value(ToolOptState *s) {
    if (s->argi >= s->argc) {
        tool_write_error(s->prog, "option requires an argument: ", s->flag);
        tool_write_usage(s->prog, s->usage_suffix);
        return -1;
    }
    s->value = s->argv[s->argi];
    s->argi += 1;
    return 0;
}

int tool_starts_with(const char *text, const char *prefix) {
    size_t index = 0U;

    if (text == 0 || prefix == 0) {
        return 0;
    }

    while (prefix[index] != '\0') {
        if (text[index] != prefix[index]) {
            return 0;
        }
        index += 1U;
    }

    return 1;
}

void tool_trim_whitespace(char *text) {
    size_t start = 0U;
    size_t end;
    size_t out = 0U;

    if (text == 0) {
        return;
    }

    end = rt_strlen(text);
    while (text[start] != '\0' && rt_is_space(text[start])) {
        start += 1U;
    }
    while (end > start && rt_is_space(text[end - 1U])) {
        end -= 1U;
    }

    while (start + out < end) {
        text[out] = text[start + out];
        out += 1U;
    }
    text[out] = '\0';
}
