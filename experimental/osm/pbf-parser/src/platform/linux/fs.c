#include "platform.h"
#include "common.h"

struct linux_statfs {
    long f_type;
    long f_bsize;
    unsigned long long f_blocks;
    unsigned long long f_bfree;
    unsigned long long f_bavail;
    unsigned long long f_files;
    unsigned long long f_ffree;
    struct {
        int val[2];
    } f_fsid;
    long f_namelen;
    long f_frsize;
    long f_flags;
    long f_spare[4];
};

#define LINUX_MS_RDONLY      0x00000001UL
#define LINUX_MS_NOSUID      0x00000002UL
#define LINUX_MS_NODEV       0x00000004UL
#define LINUX_MS_NOEXEC      0x00000008UL
#define LINUX_MS_SYNCHRONOUS 0x00000010UL
#define LINUX_MS_REMOUNT     0x00000020UL
#define LINUX_MS_MANDLOCK    0x00000040UL
#define LINUX_MS_DIRSYNC     0x00000080UL
#define LINUX_MS_NOATIME     0x00000400UL
#define LINUX_MS_NODIRATIME  0x00000800UL
#define LINUX_MS_BIND        0x00001000UL
#define LINUX_MS_REC         0x00004000UL
#define LINUX_MS_SILENT      0x00008000UL
#define LINUX_MS_RELATIME    0x00200000UL
#define LINUX_MS_STRICTATIME 0x01000000UL
#define LINUX_MS_LAZYTIME    0x02000000UL

#define LINUX_MNT_FORCE  0x00000001
#define LINUX_MNT_DETACH 0x00000002

static void fill_entry_from_stat(const char *display_name, const struct linux_stat *st, PlatformDirEntry *entry) {
    copy_string(entry->name, sizeof(entry->name), display_name != 0 ? display_name : "");
    entry->device = (unsigned long long)st->st_dev;
    entry->mode = st->st_mode;
    entry->uid = (unsigned int)st->st_uid;
    entry->gid = (unsigned int)st->st_gid;
    entry->size = (unsigned long long)st->st_size;
    entry->inode = (unsigned long long)st->st_ino;
    entry->nlink = (unsigned long)st->st_nlink;
    entry->atime = (long long)st->st_atime;
    entry->mtime = (long long)st->st_mtime;
    entry->ctime = (long long)st->st_ctime;
    entry->is_dir = ((st->st_mode & LINUX_S_IFMT) == LINUX_S_IFDIR) ? 1 : 0;
    entry->is_hidden = (display_name != 0 && display_name[0] == '.') ? 1 : 0;
    unsigned_to_string((unsigned long long)st->st_uid, entry->owner, sizeof(entry->owner));
    unsigned_to_string((unsigned long long)st->st_gid, entry->group, sizeof(entry->group));
}

static int fill_entry_mode(const char *display_name, const char *full_path, PlatformDirEntry *entry, int follow_symlinks) {
    struct linux_stat st;
    long result;

    result = linux_syscall4(
        LINUX_SYS_NEWFSTATAT,
        LINUX_AT_FDCWD,
        (long)full_path,
        (long)&st,
        follow_symlinks ? 0 : LINUX_AT_SYMLINK_NOFOLLOW
    );
    if (result < 0) {
        return -1;
    }

    fill_entry_from_stat(display_name, &st, entry);
    return 0;
}

static int fill_entry(const char *display_name, const char *full_path, PlatformDirEntry *entry) {
    return fill_entry_mode(display_name, full_path, entry, 0);
}

static unsigned int linux_permission_bits_from_mode(unsigned int mode, unsigned int read_bit, unsigned int write_bit, unsigned int exec_bit) {
    unsigned int permissions = 0;

    if ((mode & read_bit) != 0U) {
        permissions |= PLATFORM_ACCESS_READ;
    }
    if ((mode & write_bit) != 0U) {
        permissions |= PLATFORM_ACCESS_WRITE;
    }
    if ((mode & exec_bit) != 0U) {
        permissions |= PLATFORM_ACCESS_EXECUTE;
    }

    return permissions;
}

static unsigned long long next_temp_path_id = 1;

static unsigned long long linux_make_device(unsigned int major, unsigned int minor) {
    unsigned long long major_value = (unsigned long long)major;
    unsigned long long minor_value = (unsigned long long)minor;

    return (minor_value & 0xffULL) |
           ((major_value & 0xfffULL) << 8) |
           ((minor_value & ~0xffULL) << 12) |
           ((major_value & ~0xfffULL) << 32);
}

int platform_open_read_secure(const char *path, PlatformDirEntry *entry_out) {
    struct linux_stat st;
    long fd;
    long rc;

    if (path == 0 || entry_out == 0) {
        return -1;
    }

    fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)path, LINUX_O_RDONLY | LINUX_O_CLOEXEC | LINUX_O_NOFOLLOW, 0);
    if (fd < 0) {
        return -1;
    }
    rc = linux_syscall4(LINUX_SYS_NEWFSTATAT, fd, (long)"", (long)&st, LINUX_AT_EMPTY_PATH);
    if (rc < 0) {
        linux_syscall1(LINUX_SYS_CLOSE, fd);
        return -1;
    }
    fill_entry_from_stat(path, &st, entry_out);
    return (int)fd;
}

int platform_create_temp_file(char *path_buffer, size_t buffer_size, const char *prefix, unsigned int mode) {
    static const char hex_digits[] = "0123456789abcdef";
    const char *base = (prefix != 0 && prefix[0] != '\0') ? prefix : "/tmp/newos-tmp-";
    unsigned int attempt;

    if (path_buffer == 0 || buffer_size == 0) {
        return -1;
    }

    for (attempt = 0; attempt < 32; ++attempt) {
        char suffix[32];
        size_t base_len = string_length(base);
        size_t suffix_len;
        long fd;
        unsigned char random_bytes[6];

        size_t i;

        if (platform_random_bytes(random_bytes, sizeof(random_bytes)) != 0) {
            return -1;
        }
        for (i = 0U; i < sizeof(random_bytes); ++i) {
            suffix[i * 2U] = hex_digits[random_bytes[i] >> 4];
            suffix[i * 2U + 1U] = hex_digits[random_bytes[i] & 0x0fU];
        }
        suffix[sizeof(random_bytes) * 2U] = '\0';

        suffix_len = string_length(suffix);
        if (base_len + suffix_len + 1 > buffer_size) {
            return -1;
        }

        copy_string(path_buffer, buffer_size, base);
        copy_string(path_buffer + base_len, buffer_size - base_len, suffix);

        fd = linux_syscall4(
            LINUX_SYS_OPENAT,
            LINUX_AT_FDCWD,
            (long)path_buffer,
            LINUX_O_WRONLY | LINUX_O_CREAT | LINUX_O_EXCL | LINUX_O_TRUNC | LINUX_O_CLOEXEC,
            (long)mode
        );
        if (fd >= 0) {
            next_temp_path_id += (unsigned long long)attempt + 1ULL;
            return (int)fd;
        }
    }

    return -1;
}

int platform_make_directory(const char *path, unsigned int mode) {
    return linux_syscall3(LINUX_SYS_MKDIRAT, LINUX_AT_FDCWD, (long)path, (long)mode) < 0 ? -1 : 0;
}

int platform_remove_file(const char *path) {
    return linux_syscall3(LINUX_SYS_UNLINKAT, LINUX_AT_FDCWD, (long)path, 0) < 0 ? -1 : 0;
}

int platform_remove_directory(const char *path) {
    return linux_syscall3(LINUX_SYS_UNLINKAT, LINUX_AT_FDCWD, (long)path, LINUX_AT_REMOVEDIR) < 0 ? -1 : 0;
}

int platform_create_node(const char *path, unsigned int node_type, unsigned int mode, unsigned int major, unsigned int minor) {
    unsigned int native_mode = mode & 07777U;
    unsigned long long device = 0ULL;

    if (path == 0 || path[0] == '\0') {
        return -1;
    }

    if (node_type == PLATFORM_NODE_FIFO) {
        native_mode |= LINUX_S_IFIFO;
    } else if (node_type == PLATFORM_NODE_CHAR) {
        native_mode |= LINUX_S_IFCHR;
        device = linux_make_device(major, minor);
    } else if (node_type == PLATFORM_NODE_BLOCK) {
        native_mode |= LINUX_S_IFBLK;
        device = linux_make_device(major, minor);
    } else {
        return -1;
    }

    return linux_syscall4(LINUX_SYS_MKNODAT,
                          LINUX_AT_FDCWD,
                          (long)path,
                          (long)native_mode,
                          (long)device) < 0 ? -1 : 0;
}

static unsigned long linux_mount_flags_from_platform(unsigned long long flags) {
    unsigned long native_flags = 0UL;

    if ((flags & PLATFORM_MOUNT_RDONLY) != 0ULL) {
        native_flags |= LINUX_MS_RDONLY;
    }
    if ((flags & PLATFORM_MOUNT_NOSUID) != 0ULL) {
        native_flags |= LINUX_MS_NOSUID;
    }
    if ((flags & PLATFORM_MOUNT_NODEV) != 0ULL) {
        native_flags |= LINUX_MS_NODEV;
    }
    if ((flags & PLATFORM_MOUNT_NOEXEC) != 0ULL) {
        native_flags |= LINUX_MS_NOEXEC;
    }
    if ((flags & PLATFORM_MOUNT_SYNC) != 0ULL) {
        native_flags |= LINUX_MS_SYNCHRONOUS;
    }
    if ((flags & PLATFORM_MOUNT_REMOUNT) != 0ULL) {
        native_flags |= LINUX_MS_REMOUNT;
    }
    if ((flags & PLATFORM_MOUNT_MANDLOCK) != 0ULL) {
        native_flags |= LINUX_MS_MANDLOCK;
    }
    if ((flags & PLATFORM_MOUNT_DIRSYNC) != 0ULL) {
        native_flags |= LINUX_MS_DIRSYNC;
    }
    if ((flags & PLATFORM_MOUNT_NOATIME) != 0ULL) {
        native_flags |= LINUX_MS_NOATIME;
    }
    if ((flags & PLATFORM_MOUNT_NODIRATIME) != 0ULL) {
        native_flags |= LINUX_MS_NODIRATIME;
    }
    if ((flags & PLATFORM_MOUNT_BIND) != 0ULL) {
        native_flags |= LINUX_MS_BIND;
    }
    if ((flags & PLATFORM_MOUNT_REC) != 0ULL) {
        native_flags |= LINUX_MS_REC;
    }
    if ((flags & PLATFORM_MOUNT_SILENT) != 0ULL) {
        native_flags |= LINUX_MS_SILENT;
    }
    if ((flags & PLATFORM_MOUNT_RELATIME) != 0ULL) {
        native_flags |= LINUX_MS_RELATIME;
    }
    if ((flags & PLATFORM_MOUNT_STRICTATIME) != 0ULL) {
        native_flags |= LINUX_MS_STRICTATIME;
    }
    if ((flags & PLATFORM_MOUNT_LAZYTIME) != 0ULL) {
        native_flags |= LINUX_MS_LAZYTIME;
    }

    return native_flags;
}

int platform_mount_filesystem(
    const char *source,
    const char *target,
    const char *filesystem_type,
    unsigned long long flags,
    const char *data
) {
    long result;

    if (target == 0 || target[0] == '\0') {
        return -1;
    }

    result = linux_syscall5(
        LINUX_SYS_MOUNT,
        (long)((source != 0 && source[0] != '\0' && rt_strcmp(source, "none") != 0) ? source : 0),
        (long)target,
        (long)((filesystem_type != 0 && filesystem_type[0] != '\0') ? filesystem_type : 0),
        (long)linux_mount_flags_from_platform(flags),
        (long)((data != 0 && data[0] != '\0') ? data : 0)
    );
    return result < 0 ? -1 : 0;
}

int platform_unmount_filesystem(const char *target, int force, int lazy) {
    long flags = 0;

    if (target == 0 || target[0] == '\0') {
        return -1;
    }
    if (force) {
        flags |= LINUX_MNT_FORCE;
    }
    if (lazy) {
        flags |= LINUX_MNT_DETACH;
    }

    return linux_syscall2(LINUX_SYS_UMOUNT2, (long)target, flags) < 0 ? -1 : 0;
}

int platform_rename_path(const char *old_path, const char *new_path) {
    return linux_syscall4(LINUX_SYS_RENAMEAT, LINUX_AT_FDCWD, (long)old_path, LINUX_AT_FDCWD, (long)new_path) < 0 ? -1 : 0;
}

int platform_create_hard_link(const char *target_path, const char *link_path) {
    return linux_syscall5(LINUX_SYS_LINKAT, LINUX_AT_FDCWD, (long)target_path, LINUX_AT_FDCWD, (long)link_path, 0) < 0 ? -1 : 0;
}

int platform_create_symbolic_link(const char *target_path, const char *link_path) {
    return linux_syscall3(LINUX_SYS_SYMLINKAT, (long)target_path, LINUX_AT_FDCWD, (long)link_path) < 0 ? -1 : 0;
}

int platform_change_mode(const char *path, unsigned int mode) {
    return linux_syscall4(LINUX_SYS_FCHMODAT, LINUX_AT_FDCWD, (long)path, (long)mode, 0) < 0 ? -1 : 0;
}

int platform_change_owner_ex(const char *path, unsigned int uid, unsigned int gid, int follow_symlinks) {
    return linux_syscall5(
               LINUX_SYS_FCHOWNAT,
               LINUX_AT_FDCWD,
               (long)path,
               (long)uid,
               (long)gid,
               follow_symlinks ? 0 : LINUX_AT_SYMLINK_NOFOLLOW
           ) < 0
               ? -1
               : 0;
}

int platform_change_owner(const char *path, unsigned int uid, unsigned int gid) {
    return platform_change_owner_ex(path, uid, gid, 1);
}

int platform_touch_path(const char *path) {
    long long now = platform_get_epoch_time();
    if (now < 0) {
        return -1;
    }
    return platform_set_path_times(path, now, now, 1, 1, 1);
}

int platform_set_path_times(
    const char *path,
    long long atime,
    long long mtime,
    int create_if_missing,
    int update_access,
    int update_modify
) {
    long fd;
    struct linux_timespec times[2];
    PlatformDirEntry entry;
    int have_entry = 0;

    if (path == 0) {
        return -1;
    }

    if (!update_access && !update_modify) {
        return 0;
    }

    have_entry = (platform_get_path_info(path, &entry) == 0);

    if (create_if_missing && !have_entry) {
        fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)path, LINUX_O_WRONLY | LINUX_O_CREAT, 0644);
        if (fd < 0) {
            return -1;
        }
        linux_syscall1(LINUX_SYS_CLOSE, fd);
        have_entry = (platform_get_path_info(path, &entry) == 0);
    }

    if ((!update_access || !update_modify) && have_entry) {
        if (!update_access) {
            atime = entry.atime;
        }
        if (!update_modify) {
            mtime = entry.mtime;
        }
    }

    times[0].tv_sec = (long)atime;
    times[0].tv_nsec = 0L;
    times[1].tv_sec = (long)mtime;
    times[1].tv_nsec = 0L;
    return linux_syscall4(LINUX_SYS_UTIMENSAT, LINUX_AT_FDCWD, (long)path, (long)times, 0) < 0 ? -1 : 0;
}

int platform_path_access(const char *path, int mode) {
    struct linux_stat st;
    PlatformIdentity identity;
    unsigned int requested;
    unsigned int granted;
    long stat_result;

    if (path == 0) {
        return -1;
    }

    stat_result = linux_syscall4(
        LINUX_SYS_NEWFSTATAT,
        LINUX_AT_FDCWD,
        (long)path,
        (long)&st,
        LINUX_AT_SYMLINK_NOFOLLOW
    );
    if (stat_result < 0) {
        return -1;
    }

    requested = (unsigned int)mode & (PLATFORM_ACCESS_READ | PLATFORM_ACCESS_WRITE | PLATFORM_ACCESS_EXECUTE);
    if (requested == 0U) {
        return 0;
    }

    if (platform_get_identity(&identity) != 0) {
        return -1;
    }

    if (identity.uid == 0U) {
        if ((requested & PLATFORM_ACCESS_EXECUTE) != 0U &&
            (st.st_mode & (LINUX_S_IXUSR | LINUX_S_IXGRP | LINUX_S_IXOTH)) == 0U &&
            (st.st_mode & LINUX_S_IFMT) != LINUX_S_IFDIR) {
            return -1;
        }
        return 0;
    }

    if (identity.uid == st.st_uid) {
        granted = linux_permission_bits_from_mode(st.st_mode, LINUX_S_IRUSR, LINUX_S_IWUSR, LINUX_S_IXUSR);
    } else if (identity.gid == st.st_gid) {
        granted = linux_permission_bits_from_mode(st.st_mode, LINUX_S_IRGRP, LINUX_S_IWGRP, LINUX_S_IXGRP);
    } else {
        granted = linux_permission_bits_from_mode(st.st_mode, LINUX_S_IROTH, LINUX_S_IWOTH, LINUX_S_IXOTH);
    }

    return ((granted & requested) == requested) ? 0 : -1;
}

int platform_change_directory(const char *path) {
    return linux_syscall1(LINUX_SYS_CHDIR, (long)path) < 0 ? -1 : 0;
}

struct linux_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

int platform_get_uname(
    char *sysname,
    size_t sysname_size,
    char *nodename,
    size_t nodename_size,
    char *release,
    size_t release_size,
    char *version,
    size_t version_size,
    char *machine,
    size_t machine_size
) {
    struct linux_utsname info;

    if (linux_syscall1(LINUX_SYS_UNAME, (long)&info) < 0) {
        return -1;
    }

    linux_copy_string(sysname, (unsigned long)sysname_size, info.sysname);
    linux_copy_string(nodename, (unsigned long)nodename_size, info.nodename);
    linux_copy_string(release, (unsigned long)release_size, info.release);
    linux_copy_string(version, (unsigned long)version_size, info.version);
    linux_copy_string(machine, (unsigned long)machine_size, info.machine);
    return 0;
}

int platform_path_is_directory(const char *path, int *is_directory_out) {
    struct linux_stat st;
    long stat_result;

    if (path == 0 || is_directory_out == 0) {
        return -1;
    }

    stat_result = linux_syscall4(
        LINUX_SYS_NEWFSTATAT,
        LINUX_AT_FDCWD,
        (long)path,
        (long)&st,
        LINUX_AT_SYMLINK_NOFOLLOW
    );
    if (stat_result < 0) {
        return -1;
    }

    *is_directory_out = ((st.st_mode & LINUX_S_IFMT) == LINUX_S_IFDIR) ? 1 : 0;
    return 0;
}

int platform_collect_entries(
    const char *path,
    int include_hidden,
    PlatformDirEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int *path_is_directory
) {
    struct linux_stat st;
    size_t count = 0;
    long stat_result;

    if (path == 0 || entries_out == 0 || count_out == 0 || path_is_directory == 0) {
        return -1;
    }

    *count_out = 0;
    *path_is_directory = 0;

    stat_result = linux_syscall4(
        LINUX_SYS_NEWFSTATAT,
        LINUX_AT_FDCWD,
        (long)path,
        (long)&st,
        LINUX_AT_SYMLINK_NOFOLLOW
    );
    if (stat_result < 0) {
        return -1;
    }

    if ((st.st_mode & LINUX_S_IFMT) != LINUX_S_IFDIR) {
        if (entry_capacity == 0) {
            return -1;
        }

        if (fill_entry(path, path, &entries_out[0]) != 0) {
            return -1;
        }

        *count_out = 1;
        return 0;
    }

    {
        long fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)path, LINUX_O_RDONLY | LINUX_O_DIRECTORY, 0);
        char buffer[4096];

        if (fd < 0) {
            return -1;
        }

        for (;;) {
            long bytes = linux_syscall3(LINUX_SYS_GETDENTS64, fd, (long)buffer, sizeof(buffer));
            unsigned long offset = 0;

            if (bytes == 0) {
                break;
            }

            if (bytes < 0) {
                linux_syscall1(LINUX_SYS_CLOSE, fd);
                return -1;
            }

            while (offset < (unsigned long)bytes) {
                struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + offset);
                const char *name = entry->d_name;
                char full_path[1024];

                if (include_hidden || name[0] != '.') {
                    if (count >= entry_capacity) {
                        linux_syscall1(LINUX_SYS_CLOSE, fd);
                        return -1;
                    }

                    if (join_path(path, name, full_path, sizeof(full_path)) != 0) {
                        linux_syscall1(LINUX_SYS_CLOSE, fd);
                        return -1;
                    }

                    if (fill_entry(name, full_path, &entries_out[count]) != 0) {
                        linux_syscall1(LINUX_SYS_CLOSE, fd);
                        return -1;
                    }

                    count += 1;
                }

                offset += entry->d_reclen;
            }
        }

        linux_syscall1(LINUX_SYS_CLOSE, fd);
    }

    *count_out = count;
    *path_is_directory = 1;
    return 0;
}

int platform_stream_file_to_stdout(const char *path) {
    long fd;
    int should_close;
    char buffer[4096];

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        fd = 0;
        should_close = 0;
    } else {
        fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)path, LINUX_O_RDONLY, 0);
        if (fd < 0) {
            return -1;
        }
        should_close = 1;
    }

    for (;;) {
        long bytes = linux_syscall3(LINUX_SYS_READ, fd, (long)buffer, sizeof(buffer));
        long offset = 0;

        if (bytes == 0) {
            break;
        }

        if (bytes < 0) {
            if (should_close) {
                linux_syscall1(LINUX_SYS_CLOSE, fd);
            }
            return -1;
        }

        while (offset < bytes) {
            long written = linux_syscall3(LINUX_SYS_WRITE, 1, (long)(buffer + offset), (unsigned long)(bytes - offset));
            if (written < 0) {
                if (should_close) {
                    linux_syscall1(LINUX_SYS_CLOSE, fd);
                }
                return -1;
            }
            offset += written;
        }
    }

    if (should_close) {
        linux_syscall1(LINUX_SYS_CLOSE, fd);
    }

    return 0;
}

int platform_get_current_directory(char *buffer, size_t buffer_size) {
    long result = linux_syscall2(LINUX_SYS_GETCWD, (long)buffer, (long)buffer_size);
    return (result < 0) ? -1 : 0;
}

int platform_get_path_info(const char *path, PlatformDirEntry *entry_out) {
    if (path == 0 || entry_out == 0) {
        return -1;
    }

    return fill_entry(path, path, entry_out);
}

int platform_get_path_info_follow(const char *path, PlatformDirEntry *entry_out) {
    if (path == 0 || entry_out == 0) {
        return -1;
    }

    return fill_entry_mode(path, path, entry_out, 1);
}

int platform_read_symlink(const char *path, char *buffer, size_t buffer_size) {
    long length;

    if (path == 0 || buffer == 0 || buffer_size == 0) {
        return -1;
    }

    length = linux_syscall4(LINUX_SYS_READLINKAT, LINUX_AT_FDCWD, (long)path, (long)buffer, (long)(buffer_size - 1));
    if (length < 0) {
        return -1;
    }

    buffer[length] = '\0';
    return 0;
}

int platform_get_filesystem_info(const char *path, PlatformFilesystemInfo *info_out) {
    struct linux_statfs info;
    unsigned long long fragment_size;
    long result;

    if (path == 0 || info_out == 0) {
        return -1;
    }

    rt_memset(info_out, 0, sizeof(*info_out));
    result = linux_syscall2(LINUX_SYS_STATFS, (long)path, (long)&info);
    if (result < 0) {
        return -1;
    }

    fragment_size = (info.f_frsize > 0) ? (unsigned long long)info.f_frsize : (unsigned long long)info.f_bsize;
    info_out->total_bytes = info.f_blocks * fragment_size;
    info_out->free_bytes = info.f_bfree * fragment_size;
    info_out->available_bytes = info.f_bavail * fragment_size;
    info_out->total_inodes = info.f_files;
    info_out->free_inodes = info.f_ffree;
    info_out->available_inodes = info.f_ffree;
    rt_copy_string(info_out->type_name, sizeof(info_out->type_name), "linuxfs");
    return 0;
}

int platform_get_filesystem_usage(const char *path, unsigned long long *total_bytes_out, unsigned long long *free_bytes_out, unsigned long long *available_bytes_out) {
    PlatformFilesystemInfo info;

    if (path == 0 || total_bytes_out == 0 || free_bytes_out == 0 || available_bytes_out == 0) {
        return -1;
    }

    if (platform_get_filesystem_info(path, &info) != 0) {
        return -1;
    }

    *total_bytes_out = info.total_bytes;
    *free_bytes_out = info.free_bytes;
    *available_bytes_out = info.available_bytes;
    return 0;
}

int platform_truncate_path(const char *path, unsigned long long size) {
    long fd;
    long result;

    if (path == 0) {
        return -1;
    }

    fd = linux_syscall4(
        LINUX_SYS_OPENAT,
        LINUX_AT_FDCWD,
        (long)path,
        LINUX_O_WRONLY | LINUX_O_CREAT,
        0644
    );
    if (fd < 0) {
        return -1;
    }

    result = linux_syscall2(LINUX_SYS_FTRUNCATE, fd, (long)size);
    linux_syscall1(LINUX_SYS_CLOSE, fd);
    return result < 0 ? -1 : 0;
}

int platform_sync_all(void) {
    return linux_syscall0(LINUX_SYS_SYNC) < 0 ? -1 : 0;
}

static long open_sync_fd(const char *path) {
    long fd;

    if (path == 0) {
        return -1;
    }

    fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)path, LINUX_O_RDONLY, 0);
    if (fd < 0) {
        fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)path, LINUX_O_WRONLY, 0);
    }

    return fd;
}

int platform_sync_path(const char *path) {
    long fd;
    long result;

    fd = open_sync_fd(path);
    if (fd < 0) {
        return -1;
    }

    result = linux_syscall1(LINUX_SYS_FSYNC, fd);
    linux_syscall1(LINUX_SYS_CLOSE, fd);
    return result < 0 ? -1 : 0;
}

int platform_sync_path_data(const char *path) {
    long fd;
    long result;

    fd = open_sync_fd(path);
    if (fd < 0) {
        return -1;
    }

    result = linux_syscall1(LINUX_SYS_FDATASYNC, fd);
    if (result < 0) {
        result = linux_syscall1(LINUX_SYS_FSYNC, fd);
    }
    linux_syscall1(LINUX_SYS_CLOSE, fd);
    return result < 0 ? -1 : 0;
}

void platform_free_entries(PlatformDirEntry *entries, size_t count) {
    (void)entries;
    (void)count;
}

void platform_format_mode(unsigned int mode, char out[11]) {
    unsigned int type = mode & LINUX_S_IFMT;

    out[0] = (type == LINUX_S_IFDIR) ? 'd' :
             (type == LINUX_S_IFLNK) ? 'l' :
             (type == LINUX_S_IFCHR) ? 'c' :
             (type == LINUX_S_IFBLK) ? 'b' :
             (type == LINUX_S_IFIFO) ? 'p' :
             (type == LINUX_S_IFSOCK) ? 's' : '-';

    out[1] = (mode & LINUX_S_IRUSR) ? 'r' : '-';
    out[2] = (mode & LINUX_S_IWUSR) ? 'w' : '-';
    out[3] = (mode & LINUX_S_IXUSR) ? 'x' : '-';
    out[4] = (mode & LINUX_S_IRGRP) ? 'r' : '-';
    out[5] = (mode & LINUX_S_IWGRP) ? 'w' : '-';
    out[6] = (mode & LINUX_S_IXGRP) ? 'x' : '-';
    out[7] = (mode & LINUX_S_IROTH) ? 'r' : '-';
    out[8] = (mode & LINUX_S_IWOTH) ? 'w' : '-';
    out[9] = (mode & LINUX_S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}
