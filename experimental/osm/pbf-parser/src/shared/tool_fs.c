#include "platform.h"
#include "runtime.h"
#include "tool_util.h"

int tool_copy_file(const char *source_path, const char *dest_path) {
    int src_fd = platform_open_read(source_path);
    int dst_fd;
    char buffer[4096];

    if (tool_paths_equal(source_path, dest_path)) {
        return -1;
    }

    if (src_fd < 0) {
        return -1;
    }

    dst_fd = platform_open_write(dest_path, 0644U);
    if (dst_fd < 0) {
        platform_close(src_fd);
        return -1;
    }

    for (;;) {
        long bytes_read = platform_read(src_fd, buffer, sizeof(buffer));
        long offset = 0;

        if (bytes_read == 0) {
            break;
        }

        if (bytes_read < 0) {
            platform_close(src_fd);
            platform_close(dst_fd);
            return -1;
        }

        while (offset < bytes_read) {
            long bytes_written = platform_write(dst_fd, buffer + offset, (size_t)(bytes_read - offset));
            if (bytes_written <= 0) {
                platform_close(src_fd);
                platform_close(dst_fd);
                return -1;
            }
            offset += bytes_written;
        }
    }

    platform_close(src_fd);
    platform_close(dst_fd);
    return 0;
}

int tool_copy_path(const char *source_path, const char *dest_path, int recursive, int preserve_mode, int preserve_symlinks) {
    PlatformDirEntry source_info;
    char link_target[2048];

    if (source_path == 0 || dest_path == 0 || platform_get_path_info(source_path, &source_info) != 0) {
        return -1;
    }

    if (preserve_symlinks && platform_read_symlink(source_path, link_target, sizeof(link_target)) == 0) {
        if (tool_paths_equal(source_path, dest_path)) {
            return 0;
        }
        (void)platform_remove_file(dest_path);
        if (platform_create_symbolic_link(link_target, dest_path) != 0) {
            return -1;
        }
        return 0;
    }

    if (!source_info.is_dir) {
        if (tool_copy_file(source_path, dest_path) != 0) {
            return -1;
        }
        if (preserve_mode) {
            (void)platform_change_mode(dest_path, source_info.mode & 07777U);
        }
        return 0;
    }

    if (!recursive) {
        return -2;
    }

    {
        int dest_is_directory = 0;
        unsigned int dir_mode = preserve_mode ? (source_info.mode & 07777U) : 0755U;

        if (platform_path_is_directory(dest_path, &dest_is_directory) != 0) {
            if (platform_make_directory(dest_path, dir_mode) != 0) {
                return -1;
            }
        } else if (!dest_is_directory) {
            return -1;
        }
    }

    {
        enum { TOOL_COPY_ENTRY_CAPACITY = 1024, TOOL_COPY_PATH_CAPACITY = 2048 };
        PlatformDirEntry entries[TOOL_COPY_ENTRY_CAPACITY];
        size_t count = 0;
        size_t i;
        int path_is_directory = 0;

        if (platform_collect_entries(source_path, 1, entries, TOOL_COPY_ENTRY_CAPACITY, &count, &path_is_directory) != 0 ||
            !path_is_directory) {
            return -1;
        }

        for (i = 0; i < count; ++i) {
            char child_source[TOOL_COPY_PATH_CAPACITY];
            char child_dest[TOOL_COPY_PATH_CAPACITY];

            if (rt_strcmp(entries[i].name, ".") == 0 || rt_strcmp(entries[i].name, "..") == 0) {
                continue;
            }

            if (tool_join_path(source_path, entries[i].name, child_source, sizeof(child_source)) != 0 ||
                tool_join_path(dest_path, entries[i].name, child_dest, sizeof(child_dest)) != 0 ||
                tool_copy_path(child_source, child_dest, recursive, preserve_mode, preserve_symlinks) != 0) {
                platform_free_entries(entries, count);
                return -1;
            }
        }

        platform_free_entries(entries, count);
    }

    if (preserve_mode) {
        (void)platform_change_mode(dest_path, source_info.mode & 07777U);
    }

    return 0;
}

int tool_remove_path(const char *path, int recursive) {
    enum { TOOL_REMOVE_ENTRY_CAPACITY = 1024, TOOL_REMOVE_PATH_CAPACITY = 2048 };
    PlatformDirEntry entries[TOOL_REMOVE_ENTRY_CAPACITY];
    size_t count = 0;
    size_t i;
    int is_directory = 0;

    if (path == 0 || platform_collect_entries(path, 1, entries, TOOL_REMOVE_ENTRY_CAPACITY, &count, &is_directory) != 0) {
        return -1;
    }

    if (!is_directory) {
        platform_free_entries(entries, count);
        return platform_remove_file(path) == 0 ? 0 : -1;
    }

    if (!recursive) {
        platform_free_entries(entries, count);
        return -2;
    }

    for (i = 0; i < count; ++i) {
        char child_path[TOOL_REMOVE_PATH_CAPACITY];

        if (rt_strcmp(entries[i].name, ".") == 0 || rt_strcmp(entries[i].name, "..") == 0) {
            continue;
        }

        if (tool_join_path(path, entries[i].name, child_path, sizeof(child_path)) != 0 ||
            tool_remove_path(child_path, 1) != 0) {
            platform_free_entries(entries, count);
            return -1;
        }
    }

    platform_free_entries(entries, count);
    return platform_remove_directory(path) == 0 ? 0 : -1;
}

static int tool_walk_path_impl(const char *path,
                               const ToolWalkOptions *options,
                               ToolWalkCallback callback,
                               void *user_data,
                               int depth) {
    enum { TOOL_WALK_ENTRY_CAPACITY = 1024, TOOL_WALK_PATH_CAPACITY = 2048 };
    PlatformDirEntry current;
    ToolWalkControl control;
    PlatformDirEntry entries[TOOL_WALK_ENTRY_CAPACITY];
    size_t count = 0U;
    size_t i;
    int is_directory = 0;

    if (platform_get_path_info(path, &current) != 0) {
        return -1;
    }

    control.prune = 0;
    if (depth >= options->min_depth && callback(path, &current, depth, &control, user_data) != 0) {
        return -1;
    }

    if (!current.is_dir || control.prune || (options->max_depth >= 0 && depth >= options->max_depth)) {
        return 0;
    }

    if (platform_collect_entries(path, 1, entries, TOOL_WALK_ENTRY_CAPACITY, &count, &is_directory) != 0 || !is_directory) {
        return -1;
    }

    for (i = 0U; i < count; ++i) {
        char child_path[TOOL_WALK_PATH_CAPACITY];

        if (rt_strcmp(entries[i].name, ".") == 0 || rt_strcmp(entries[i].name, "..") == 0) {
            continue;
        }
        if (tool_join_path(path, entries[i].name, child_path, sizeof(child_path)) != 0 ||
            tool_walk_path_impl(child_path, options, callback, user_data, depth + 1) != 0) {
            platform_free_entries(entries, count);
            return -1;
        }
    }

    platform_free_entries(entries, count);
    return 0;
}

int tool_walk_path(const char *path, const ToolWalkOptions *options, ToolWalkCallback callback, void *user_data) {
    ToolWalkOptions default_options;

    if (path == 0 || callback == 0) {
        return -1;
    }
    if (options == 0) {
        default_options.min_depth = 0;
        default_options.max_depth = -1;
        options = &default_options;
    }
    return tool_walk_path_impl(path, options, callback, user_data, 0);
}
