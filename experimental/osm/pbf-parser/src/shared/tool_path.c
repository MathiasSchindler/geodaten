#include "platform.h"
#include "runtime.h"
#include "tool_util.h"

const char *tool_base_name(const char *path) {
    const char *last = path;
    size_t i = 0;

    if (path == 0) {
        return "";
    }

    while (path[i] != '\0') {
        if (path[i] == '/') {
            last = path + i + 1;
        }
        i += 1;
    }

    return last;
}

int tool_join_path(const char *dir_path, const char *name, char *buffer, size_t buffer_size) {
    return rt_join_path(dir_path, name, buffer, buffer_size);
}

void tool_resolve_host_program_path(char **argv_exec, char *buffer, size_t buffer_size) {
    PlatformDirEntry entry;
    const char *base_name;

    if (argv_exec == 0 || argv_exec[0] == 0 || buffer == 0 || buffer_size == 0U) {
        return;
    }
    if (argv_exec[0][0] != '/' || !tool_starts_with(argv_exec[0], "/bin/")) {
        return;
    }
    if (platform_get_path_info(argv_exec[0], &entry) == 0) {
        return;
    }

    base_name = tool_base_name(argv_exec[0]);
    if (tool_join_path("/usr/bin", base_name, buffer, buffer_size) != 0) {
        return;
    }
    if (platform_get_path_info(buffer, &entry) != 0 || entry.is_dir) {
        return;
    }

    argv_exec[0] = buffer;
}

int tool_resolve_destination(const char *source_path, const char *dest_path, char *buffer, size_t buffer_size) {
    int is_directory = 0;
    size_t path_len;

    if (platform_path_is_directory(dest_path, &is_directory) == 0 && is_directory) {
        return tool_join_path(dest_path, tool_base_name(source_path), buffer, buffer_size);
    }

    path_len = rt_strlen(dest_path);
    if (path_len + 1 > buffer_size) {
        return -1;
    }

    memcpy(buffer, dest_path, path_len + 1);
    return 0;
}

static void tool_pop_path_component(char *path) {
    size_t len;

    if (path == 0) {
        return;
    }

    len = rt_strlen(path);
    while (len > 1U && path[len - 1U] == '/') {
        path[len - 1U] = '\0';
        len -= 1U;
    }

    while (len > 1U && path[len - 1U] != '/') {
        path[len - 1U] = '\0';
        len -= 1U;
    }

    while (len > 1U && path[len - 1U] == '/') {
        path[len - 1U] = '\0';
        len -= 1U;
    }

    if (len == 0U) {
        rt_copy_string(path, 2U, "/");
    }
}

static int tool_append_path_component(char *path, size_t buffer_size, const char *component) {
    size_t len;
    size_t component_len;

    if (path == 0 || component == 0) {
        return -1;
    }

    len = rt_strlen(path);
    component_len = rt_strlen(component);
    if (len == 0U) {
        if (buffer_size < 2U) {
            return -1;
        }
        path[0] = '/';
        path[1] = '\0';
        len = 1U;
    }

    if (!(len == 1U && path[0] == '/')) {
        if (len + 1U >= buffer_size) {
            return -1;
        }
        path[len++] = '/';
        path[len] = '\0';
    }

    if (len + component_len + 1U > buffer_size) {
        return -1;
    }

    memcpy(path + len, component, component_len + 1U);
    return 0;
}

static int tool_build_absolute_path(const char *path, char *buffer, size_t buffer_size) {
    char cwd[2048];

    if (path == 0 || buffer == 0 || buffer_size == 0U || path[0] == '\0') {
        return -1;
    }

    if (path[0] == '/') {
        if (rt_strlen(path) + 1U > buffer_size) {
            return -1;
        }
        rt_copy_string(buffer, buffer_size, path);
        return 0;
    }

    if (platform_get_current_directory(cwd, sizeof(cwd)) != 0) {
        return -1;
    }

    return tool_join_path(cwd, path, buffer, buffer_size);
}

static int tool_concat_path_suffix(const char *prefix, const char *suffix, char *buffer, size_t buffer_size) {
    size_t prefix_len;
    size_t suffix_index = 0;

    if (prefix == 0 || buffer == 0 || buffer_size == 0U) {
        return -1;
    }

    prefix_len = rt_strlen(prefix);
    if (prefix_len + 1U > buffer_size) {
        return -1;
    }

    memcpy(buffer, prefix, prefix_len + 1U);
    while (prefix_len > 1U && buffer[prefix_len - 1U] == '/' && suffix != 0 && suffix[suffix_index] == '/') {
        buffer[prefix_len - 1U] = '\0';
        prefix_len -= 1U;
    }

    while (suffix != 0 && suffix[suffix_index] != '\0') {
        if (prefix_len + 1U >= buffer_size) {
            return -1;
        }
        buffer[prefix_len++] = suffix[suffix_index++];
    }
    buffer[prefix_len] = '\0';
    return 0;
}

int tool_canonicalize_path_policy(
    const char *path,
    int resolve_symlinks,
    int allow_missing,
    int logical_policy,
    char *buffer,
    size_t buffer_size
) {
    char pending[2048];
    char resolved[2048];
    size_t index = 0;
    unsigned int symlink_count = 0U;

    if (tool_build_absolute_path(path, pending, sizeof(pending)) != 0) {
        return -1;
    }

    rt_copy_string(resolved, sizeof(resolved), "/");

    while (pending[index] != '\0') {
        char component[256];
        size_t component_len = 0U;
        char candidate[2048];

        while (pending[index] == '/') {
            index += 1U;
        }
        if (pending[index] == '\0') {
            break;
        }

        while (pending[index] != '\0' && pending[index] != '/') {
            if (component_len + 1U >= sizeof(component)) {
                return -1;
            }
            component[component_len++] = pending[index++];
        }
        component[component_len] = '\0';

        if (rt_strcmp(component, ".") == 0) {
            continue;
        }

        if (rt_strcmp(component, "..") == 0) {
            tool_pop_path_component(resolved);
            continue;
        }

        rt_copy_string(candidate, sizeof(candidate), resolved);
        if (tool_append_path_component(candidate, sizeof(candidate), component) != 0) {
            return -1;
        }

        if (resolve_symlinks && !logical_policy) {
            char target[2048];

            if (platform_read_symlink(candidate, target, sizeof(target)) == 0) {
                char replacement[2048];
                char remainder[2048];

                if (symlink_count >= 64U) {
                    return -1;
                }
                symlink_count += 1U;

                if (target[0] == '/') {
                    rt_copy_string(replacement, sizeof(replacement), target);
                } else {
                    if (tool_join_path(resolved, target, replacement, sizeof(replacement)) != 0) {
                        return -1;
                    }
                }

                rt_copy_string(remainder, sizeof(remainder), pending + index);
                if (tool_concat_path_suffix(replacement, remainder, pending, sizeof(pending)) != 0) {
                    return -1;
                }

                rt_copy_string(resolved, sizeof(resolved), "/");
                index = 0U;
                continue;
            }
        }

        if (!allow_missing) {
            PlatformDirEntry entry;
            if (platform_get_path_info(candidate, &entry) != 0) {
                return -1;
            }
        }

        rt_copy_string(resolved, sizeof(resolved), candidate);
    }

    if (resolved[0] == '\0') {
        rt_copy_string(resolved, sizeof(resolved), "/");
    }

    if (resolve_symlinks && logical_policy) {
        return tool_canonicalize_path_policy(resolved, 1, allow_missing, 0, buffer, buffer_size);
    }

    if (rt_strlen(resolved) + 1U > buffer_size) {
        return -1;
    }
    rt_copy_string(buffer, buffer_size, resolved);
    return 0;
}

int tool_canonicalize_path(const char *path, int resolve_symlinks, int allow_missing, char *buffer, size_t buffer_size) {
    return tool_canonicalize_path_policy(path, resolve_symlinks, allow_missing, 0, buffer, buffer_size);
}

int tool_path_exists(const char *path) {
    PlatformDirEntry entry;
    return path != 0 && platform_get_path_info(path, &entry) == 0;
}

static void tool_normalize_for_compare(const char *path, char *buffer, size_t buffer_size) {
    size_t len;

    if (buffer_size == 0U) {
        return;
    }

    if (path == 0 || path[0] == '\0') {
        buffer[0] = '\0';
        return;
    }

    rt_copy_string(buffer, buffer_size, path);
    len = rt_strlen(buffer);
    while (len > 1U && buffer[len - 1U] == '/') {
        buffer[len - 1U] = '\0';
        len -= 1U;
    }
}

static int tool_paths_reference_same_entry(const char *left_path, const char *right_path) {
    PlatformDirEntry left_entry;
    PlatformDirEntry right_entry;

    if (left_path == 0 || right_path == 0) {
        return 0;
    }

    if (platform_get_path_info_follow(left_path, &left_entry) != 0 ||
        platform_get_path_info_follow(right_path, &right_entry) != 0) {
        return 0;
    }

    return left_entry.device == right_entry.device && left_entry.inode == right_entry.inode;
}

int tool_paths_equal(const char *left_path, const char *right_path) {
    char left[2048];
    char right[2048];

    if (left_path == 0 || right_path == 0) {
        return 0;
    }

    if (tool_paths_reference_same_entry(left_path, right_path)) {
        return 1;
    }

    if (tool_canonicalize_path(left_path, 0, 1, left, sizeof(left)) == 0 &&
        tool_canonicalize_path(right_path, 0, 1, right, sizeof(right)) == 0) {
        return rt_strcmp(left, right) == 0;
    }

    tool_normalize_for_compare(left_path, left, sizeof(left));
    tool_normalize_for_compare(right_path, right, sizeof(right));
    return rt_strcmp(left, right) == 0;
}

int tool_path_is_root(const char *path) {
    char normalized[2048];

    if (path == 0) {
        return 0;
    }

    if (tool_canonicalize_path(path, 0, 1, normalized, sizeof(normalized)) == 0) {
        return rt_strcmp(normalized, "/") == 0;
    }

    tool_normalize_for_compare(path, normalized, sizeof(normalized));
    return rt_strcmp(normalized, "/") == 0;
}
