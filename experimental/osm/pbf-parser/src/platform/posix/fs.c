#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#if defined(__linux__)
#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "platform.h"
#include "common.h"

#include <limits.h>
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <unistd.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

#if defined(__linux__)
#include <sys/sysmacros.h>
#endif
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/mount.h>
#endif

void *platform_allocate_pages(size_t size) {
    void *mapped = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return mapped == MAP_FAILED ? 0 : mapped;
}

static int posix_mark_fd_cloexec(int fd) {
    int flags;

    if (fd < 0) {
        errno = EINVAL;
        return -1;
    }

    flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        return -1;
    }
    if ((flags & FD_CLOEXEC) != 0) {
        return 0;
    }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static void copy_identity_names(uid_t uid, gid_t gid, PlatformDirEntry *entry) {
    struct passwd *pw = getpwuid(uid);
    struct group *gr = getgrgid(gid);

    if (pw != NULL) {
        posix_copy_string(entry->owner, sizeof(entry->owner), pw->pw_name);
    } else {
        snprintf(entry->owner, sizeof(entry->owner), "%u", (unsigned)uid);
    }

    if (gr != NULL) {
        posix_copy_string(entry->group, sizeof(entry->group), gr->gr_name);
    } else {
        snprintf(entry->group, sizeof(entry->group), "%u", (unsigned)gid);
    }
}

static void fill_entry_from_stat(const char *display_name, const struct stat *st, PlatformDirEntry *entry) {
    memset(entry, 0, sizeof(*entry));
    posix_copy_string(entry->name, sizeof(entry->name), display_name != NULL ? display_name : "");
    entry->device = (unsigned long long)st->st_dev;
    entry->mode = (unsigned int)st->st_mode;
    entry->uid = (unsigned int)st->st_uid;
    entry->gid = (unsigned int)st->st_gid;
    entry->size = (unsigned long long)st->st_size;
    entry->inode = (unsigned long long)st->st_ino;
    entry->nlink = (unsigned long)st->st_nlink;
    entry->atime = (long long)st->st_atime;
    entry->mtime = (long long)st->st_mtime;
    entry->ctime = (long long)st->st_ctime;
    entry->is_dir = S_ISDIR(st->st_mode) ? 1 : 0;
    entry->is_hidden = (display_name != NULL && display_name[0] == '.') ? 1 : 0;
    copy_identity_names(st->st_uid, st->st_gid, entry);
}

static int fill_entry_mode(const char *display_name, const char *full_path, PlatformDirEntry *entry, int follow_symlinks) {
    struct stat st;

    if ((follow_symlinks ? stat(full_path, &st) : lstat(full_path, &st)) != 0) {
        return -1;
    }

    fill_entry_from_stat(display_name, &st, entry);
    return 0;
}

static int fill_entry(const char *display_name, const char *full_path, PlatformDirEntry *entry) {
    return fill_entry_mode(display_name, full_path, entry, 0);
}

long platform_write(int fd, const void *buffer, size_t count) {
    return (long)write(fd, buffer, count);
}

long platform_read(int fd, void *buffer, size_t count) {
    return (long)read(fd, buffer, count);
}

int platform_open_read(const char *path) {
    int fd;

    if (path == NULL || strcmp(path, "-") == 0) {
        return STDIN_FILENO;
    }

#ifdef O_CLOEXEC
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0 || errno != EINVAL) {
        return fd;
    }
#endif
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        (void)posix_mark_fd_cloexec(fd);
    }
    return fd;
}

int platform_open_read_secure(const char *path, PlatformDirEntry *entry_out) {
    struct stat st;
    int fd;

    if (path == NULL || entry_out == NULL) {
        errno = EINVAL;
        return -1;
    }

#ifdef O_NOFOLLOW
#ifdef O_CLOEXEC
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
#else
    fd = open(path, O_RDONLY | O_NOFOLLOW);
#endif
    if (fd < 0) {
        return -1;
    }
#else
    fd = platform_open_read(path);
    if (fd < 0) {
        return -1;
    }
#endif

    if (fstat(fd, &st) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    fill_entry_from_stat(path, &st, entry_out);
    return fd;
}

int platform_open_write_mode(const char *path, unsigned int mode, int truncate_existing) {
    int flags = O_WRONLY | O_CREAT;

    if (path == NULL || strcmp(path, "-") == 0) {
        return STDOUT_FILENO;
    }

    if (truncate_existing) {
        flags |= O_TRUNC;
    }

#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    return open(path, flags, (mode_t)mode);
}

int platform_open_write(const char *path, unsigned int mode) {
    return platform_open_write_mode(path, mode, 1);
}

int platform_open_create_exclusive(const char *path, unsigned int mode) {
    if (path == NULL || strcmp(path, "-") == 0) {
        errno = EINVAL;
        return -1;
    }

#ifdef O_CLOEXEC
    return open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, (mode_t)mode);
#else
    {
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, (mode_t)mode);
        if (fd >= 0) {
            (void)posix_mark_fd_cloexec(fd);
        }
        return fd;
    }
#endif
}

int platform_open_append(const char *path, unsigned int mode) {
    if (path == NULL || strcmp(path, "-") == 0) {
        return STDOUT_FILENO;
    }

#ifdef O_CLOEXEC
    return open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, (mode_t)mode);
#else
    {
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, (mode_t)mode);
        if (fd >= 0) {
            (void)posix_mark_fd_cloexec(fd);
        }
        return fd;
    }
#endif
}

int platform_open_append_existing(const char *path) {
    if (path == NULL || strcmp(path, "-") == 0) {
        return STDOUT_FILENO;
    }

#ifdef O_CLOEXEC
    return open(path, O_WRONLY | O_APPEND | O_CLOEXEC);
#else
    {
        int fd = open(path, O_WRONLY | O_APPEND);
        if (fd >= 0) {
            (void)posix_mark_fd_cloexec(fd);
        }
        return fd;
    }
#endif
}

int platform_create_temp_file(char *path_buffer, size_t buffer_size, const char *prefix, unsigned int mode) {
    char templ[1024];
    const char *base = (prefix != NULL && prefix[0] != '\0') ? prefix : "/tmp/newos-tmp-";
    const char *tmpdir = getenv("TMPDIR");
    size_t base_len = strlen(base);
    size_t offset = 0;
    int fd;

    if (path_buffer == NULL || buffer_size == 0) {
        errno = EINVAL;
        return -1;
    }

    if (tmpdir != NULL && tmpdir[0] != '\0' && strncmp(base, "/tmp/", 5U) == 0) {
        size_t tmpdir_len = strlen(tmpdir);
        const char *name = base + 5;
        size_t name_len = strlen(name);

        if (tmpdir_len + 1U + name_len + 7U > sizeof(templ) || tmpdir_len + 1U + name_len + 7U > buffer_size) {
            errno = EINVAL;
            return -1;
        }
        memcpy(templ, tmpdir, tmpdir_len);
        offset = tmpdir_len;
        if (offset > 0U && templ[offset - 1U] != '/') {
            templ[offset++] = '/';
        }
        memcpy(templ + offset, name, name_len);
        offset += name_len;
    } else {
        if (base_len + 7U > sizeof(templ) || base_len + 7U > buffer_size) {
            errno = EINVAL;
            return -1;
        }
        memcpy(templ, base, base_len);
        offset = base_len;
    }
    memcpy(templ + offset, "XXXXXX", 7);

    fd = mkstemp(templ);
    if (fd < 0) {
        return -1;
    }

    if (posix_mark_fd_cloexec(fd) != 0) {
        int saved_errno = errno;
        close(fd);
        unlink(templ);
        errno = saved_errno;
        return -1;
    }

    if (fchmod(fd, (mode_t)mode) != 0) {
        int saved_errno = errno;
        close(fd);
        unlink(templ);
        errno = saved_errno;
        return -1;
    }

    posix_copy_string(path_buffer, buffer_size, templ);
    return fd;
}

long long platform_seek(int fd, long long offset, int whence) {
    off_t result = lseek(fd, (off_t)offset, whence);
    return result < 0 ? -1 : (long long)result;
}

int platform_close(int fd) {
    if (fd == STDIN_FILENO || fd == STDOUT_FILENO) {
        return 0;
    }

    return close(fd);
}

int platform_make_directory(const char *path, unsigned int mode) {
    return mkdir(path, (mode_t)mode);
}

int platform_remove_file(const char *path) {
    return unlink(path);
}

int platform_remove_directory(const char *path) {
    return rmdir(path);
}

int platform_create_node(const char *path, unsigned int node_type, unsigned int mode, unsigned int major, unsigned int minor) {
    mode_t native_mode = (mode_t)(mode & 07777U);
    dev_t device = 0;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (node_type == PLATFORM_NODE_FIFO) {
        return mkfifo(path, native_mode);
    }
    if (node_type == PLATFORM_NODE_CHAR) {
        native_mode |= S_IFCHR;
    } else if (node_type == PLATFORM_NODE_BLOCK) {
        native_mode |= S_IFBLK;
    } else {
        errno = EINVAL;
        return -1;
    }

#if defined(makedev) || defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
    device = makedev((unsigned int)major, (unsigned int)minor);
#else
    device = (dev_t)(((major & 0xffU) << 8) | (minor & 0xffU));
#endif
    return mknod(path, native_mode, device);
}

#if defined(__linux__)
static unsigned long posix_mount_flags_from_platform(unsigned long long flags) {
    unsigned long native_flags = 0UL;

#ifdef MS_RDONLY
    if ((flags & PLATFORM_MOUNT_RDONLY) != 0ULL) {
        native_flags |= MS_RDONLY;
    }
#endif
#ifdef MS_NOSUID
    if ((flags & PLATFORM_MOUNT_NOSUID) != 0ULL) {
        native_flags |= MS_NOSUID;
    }
#endif
#ifdef MS_NODEV
    if ((flags & PLATFORM_MOUNT_NODEV) != 0ULL) {
        native_flags |= MS_NODEV;
    }
#endif
#ifdef MS_NOEXEC
    if ((flags & PLATFORM_MOUNT_NOEXEC) != 0ULL) {
        native_flags |= MS_NOEXEC;
    }
#endif
#ifdef MS_SYNCHRONOUS
    if ((flags & PLATFORM_MOUNT_SYNC) != 0ULL) {
        native_flags |= MS_SYNCHRONOUS;
    }
#endif
#ifdef MS_REMOUNT
    if ((flags & PLATFORM_MOUNT_REMOUNT) != 0ULL) {
        native_flags |= MS_REMOUNT;
    }
#endif
#ifdef MS_MANDLOCK
    if ((flags & PLATFORM_MOUNT_MANDLOCK) != 0ULL) {
        native_flags |= MS_MANDLOCK;
    }
#endif
#ifdef MS_DIRSYNC
    if ((flags & PLATFORM_MOUNT_DIRSYNC) != 0ULL) {
        native_flags |= MS_DIRSYNC;
    }
#endif
#ifdef MS_NOATIME
    if ((flags & PLATFORM_MOUNT_NOATIME) != 0ULL) {
        native_flags |= MS_NOATIME;
    }
#endif
#ifdef MS_NODIRATIME
    if ((flags & PLATFORM_MOUNT_NODIRATIME) != 0ULL) {
        native_flags |= MS_NODIRATIME;
    }
#endif
#ifdef MS_BIND
    if ((flags & PLATFORM_MOUNT_BIND) != 0ULL) {
        native_flags |= MS_BIND;
    }
#endif
#ifdef MS_REC
    if ((flags & PLATFORM_MOUNT_REC) != 0ULL) {
        native_flags |= MS_REC;
    }
#endif
#ifdef MS_SILENT
    if ((flags & PLATFORM_MOUNT_SILENT) != 0ULL) {
        native_flags |= MS_SILENT;
    }
#endif
#ifdef MS_RELATIME
    if ((flags & PLATFORM_MOUNT_RELATIME) != 0ULL) {
        native_flags |= MS_RELATIME;
    }
#endif
#ifdef MS_STRICTATIME
    if ((flags & PLATFORM_MOUNT_STRICTATIME) != 0ULL) {
        native_flags |= MS_STRICTATIME;
    }
#endif
#ifdef MS_LAZYTIME
    if ((flags & PLATFORM_MOUNT_LAZYTIME) != 0ULL) {
        native_flags |= MS_LAZYTIME;
    }
#endif

    return native_flags;
}
#endif

int platform_mount_filesystem(
    const char *source,
    const char *target,
    const char *filesystem_type,
    unsigned long long flags,
    const char *data
) {
    if (target == NULL || target[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

#if defined(__linux__)
    return mount(
        (source != NULL && source[0] != '\0' && strcmp(source, "none") != 0) ? source : NULL,
        target,
        (filesystem_type != NULL && filesystem_type[0] != '\0') ? filesystem_type : NULL,
        posix_mount_flags_from_platform(flags),
        (void *)((data != NULL && data[0] != '\0') ? data : NULL)
    );
#else
    (void)source;
    (void)filesystem_type;
    (void)flags;
    (void)data;
    errno = ENOSYS;
    return -1;
#endif
}

int platform_unmount_filesystem(const char *target, int force, int lazy) {
    if (target == NULL || target[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

#if defined(__linux__)
    {
        int flags = 0;
#ifdef MNT_FORCE
        if (force) {
            flags |= MNT_FORCE;
        }
#endif
#ifdef MNT_DETACH
        if (lazy) {
            flags |= MNT_DETACH;
        }
#endif
        return umount2(target, flags);
    }
#elif defined(__APPLE__) || defined(__FreeBSD__)
    if (lazy) {
        errno = ENOSYS;
        return -1;
    }
    return unmount(target, force ? MNT_FORCE : 0);
#else
    (void)force;
    (void)lazy;
    errno = ENOSYS;
    return -1;
#endif
}

int platform_rename_path(const char *old_path, const char *new_path) {
    return rename(old_path, new_path);
}

int platform_create_hard_link(const char *target_path, const char *link_path) {
    return link(target_path, link_path);
}

int platform_create_symbolic_link(const char *target_path, const char *link_path) {
    return symlink(target_path, link_path);
}

int platform_change_mode(const char *path, unsigned int mode) {
    return chmod(path, (mode_t)mode);
}

int platform_change_owner_ex(const char *path, unsigned int uid, unsigned int gid, int follow_symlinks) {
    if (follow_symlinks) {
        return chown(path, (uid_t)uid, (gid_t)gid);
    }
    return lchown(path, (uid_t)uid, (gid_t)gid);
}

int platform_change_owner(const char *path, unsigned int uid, unsigned int gid) {
    return platform_change_owner_ex(path, uid, gid, 1);
}

int platform_touch_path(const char *path) {
    long long now = platform_get_epoch_time();

    if (now < 0) {
        errno = EINVAL;
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
    struct timespec times[2];
    int fd = -1;
    PlatformDirEntry entry;
    int have_entry = 0;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (!update_access && !update_modify) {
        return 0;
    }

    have_entry = (platform_get_path_info(path, &entry) == 0);

    if (create_if_missing && !have_entry) {
        fd = open(path, O_WRONLY | O_CREAT, 0644);
        if (fd < 0) {
            return -1;
        }
        close(fd);
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

    times[0].tv_sec = (time_t)atime;
    times[0].tv_nsec = 0L;
    times[1].tv_sec = (time_t)mtime;
    times[1].tv_nsec = 0L;
    return utimensat(AT_FDCWD, path, times, 0);
}

int platform_path_access(const char *path, int mode) {
    int access_mode = F_OK;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if ((mode & PLATFORM_ACCESS_READ) != 0) {
        access_mode |= R_OK;
    }
    if ((mode & PLATFORM_ACCESS_WRITE) != 0) {
        access_mode |= W_OK;
    }
    if ((mode & PLATFORM_ACCESS_EXECUTE) != 0) {
        access_mode |= X_OK;
    }

    return access(path, access_mode);
}

int platform_change_directory(const char *path) {
    return chdir(path);
}

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
    struct utsname info;

    if (uname(&info) != 0) {
        return -1;
    }

    posix_copy_string(sysname, sysname_size, info.sysname);
    posix_copy_string(nodename, nodename_size, info.nodename);
    posix_copy_string(release, release_size, info.release);
    posix_copy_string(version, version_size, info.version);
    posix_copy_string(machine, machine_size, info.machine);
    return 0;
}

int platform_path_is_directory(const char *path, int *is_directory_out) {
    struct stat st;

    if (path == NULL || is_directory_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (lstat(path, &st) != 0) {
        return -1;
    }

    *is_directory_out = S_ISDIR(st.st_mode) ? 1 : 0;
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
    struct stat st;
    size_t count = 0;

    if (path == NULL || entries_out == NULL || count_out == NULL || path_is_directory == NULL) {
        errno = EINVAL;
        return -1;
    }

    *count_out = 0;
    *path_is_directory = 0;

    if (lstat(path, &st) != 0) {
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        if (entry_capacity == 0) {
            errno = ENOSPC;
            return -1;
        }

        if (fill_entry(path, path, &entries_out[0]) != 0) {
            return -1;
        }

        *count_out = 1;
        return 0;
    }

    {
        DIR *dir = opendir(path);
        struct dirent *de;

        if (dir == NULL) {
            return -1;
        }

        while ((de = readdir(dir)) != NULL) {
            char full_path[1024];

            if (!include_hidden && de->d_name[0] == '.') {
                continue;
            }

            if (count >= entry_capacity) {
                closedir(dir);
                errno = ENOSPC;
                return -1;
            }

            if (posix_join_path(path, de->d_name, full_path, sizeof(full_path)) != 0) {
                closedir(dir);
                return -1;
            }

            if (fill_entry(de->d_name, full_path, &entries_out[count]) != 0) {
                closedir(dir);
                return -1;
            }

            count += 1;
        }

        closedir(dir);
    }

    *count_out = count;
    *path_is_directory = 1;
    return 0;
}

void platform_free_entries(PlatformDirEntry *entries, size_t count) {
    (void)entries;
    (void)count;
}

int platform_stream_file_to_stdout(const char *path) {
    int fd;
    int should_close;
    char buffer[4096];
    ssize_t bytes_read;

    if (path == NULL || strcmp(path, "-") == 0) {
        fd = STDIN_FILENO;
        should_close = 0;
    } else {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            return -1;
        }
        should_close = 1;
    }

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        ssize_t offset = 0;

        while (offset < bytes_read) {
            ssize_t bytes_written = write(STDOUT_FILENO, buffer + offset, (size_t)(bytes_read - offset));
            if (bytes_written < 0) {
                int saved_errno = errno;
                if (should_close) {
                    close(fd);
                }
                errno = saved_errno;
                return -1;
            }
            offset += bytes_written;
        }
    }

    if (should_close) {
        close(fd);
    }

    if (bytes_read < 0) {
        return -1;
    }

    return 0;
}

int platform_get_current_directory(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        errno = EINVAL;
        return -1;
    }

    if (getcwd(buffer, buffer_size) == NULL) {
        return -1;
    }

    return 0;
}

int platform_get_path_info(const char *path, PlatformDirEntry *entry_out) {
    if (path == NULL || entry_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    return fill_entry(path, path, entry_out);
}

int platform_get_path_info_follow(const char *path, PlatformDirEntry *entry_out) {
    if (path == NULL || entry_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    return fill_entry_mode(path, path, entry_out, 1);
}

int platform_read_symlink(const char *path, char *buffer, size_t buffer_size) {
    ssize_t length;

    if (path == NULL || buffer == NULL || buffer_size == 0) {
        errno = EINVAL;
        return -1;
    }

    length = readlink(path, buffer, buffer_size - 1);
    if (length < 0) {
        return -1;
    }

    buffer[length] = '\0';
    return 0;
}

int platform_get_filesystem_info(const char *path, PlatformFilesystemInfo *info_out) {
    struct statvfs info;

    if (path == NULL || info_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(info_out, 0, sizeof(*info_out));
    if (statvfs(path, &info) != 0) {
        return -1;
    }

    info_out->total_bytes = (unsigned long long)info.f_blocks * (unsigned long long)info.f_frsize;
    info_out->free_bytes = (unsigned long long)info.f_bfree * (unsigned long long)info.f_frsize;
    info_out->available_bytes = (unsigned long long)info.f_bavail * (unsigned long long)info.f_frsize;
    info_out->total_inodes = (unsigned long long)info.f_files;
    info_out->free_inodes = (unsigned long long)info.f_ffree;
    info_out->available_inodes = (unsigned long long)info.f_favail;

#if defined(__APPLE__) || defined(__FreeBSD__)
    {
        struct statfs mount_info;
        if (statfs(path, &mount_info) == 0) {
            posix_copy_string(info_out->type_name, sizeof(info_out->type_name), mount_info.f_fstypename);
        }
    }
#endif

    if (info_out->type_name[0] == '\0') {
        posix_copy_string(info_out->type_name, sizeof(info_out->type_name), "posix");
    }

    return 0;
}

int platform_get_filesystem_usage(const char *path, unsigned long long *total_bytes_out, unsigned long long *free_bytes_out, unsigned long long *available_bytes_out) {
    PlatformFilesystemInfo info;

    if (path == NULL || total_bytes_out == NULL || free_bytes_out == NULL || available_bytes_out == NULL) {
        errno = EINVAL;
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
    int fd;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        return -1;
    }

    if (ftruncate(fd, (off_t)size) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    return close(fd);
}

int platform_sync_all(void) {
#if defined(__MSYS__)
    errno = ENOSYS;
    return -1;
#else
    sync();
    return 0;
#endif
}

static int open_sync_fd(const char *path) {
    int fd;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fd = open(path, O_WRONLY);
    }

    return fd;
}

int platform_sync_path(const char *path) {
    int fd;
    int result;
    int saved_errno;

    fd = open_sync_fd(path);
    if (fd < 0) {
        return -1;
    }

    result = fsync(fd);
    saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return result;
}

int platform_sync_path_data(const char *path) {
    int fd;
    int result;
    int saved_errno;

    fd = open_sync_fd(path);
    if (fd < 0) {
        return -1;
    }

#if defined(__APPLE__)
    result = fsync(fd);
#elif defined(_POSIX_SYNCHRONIZED_IO) && (_POSIX_SYNCHRONIZED_IO > 0)
    result = fdatasync(fd);
#else
    result = fsync(fd);
#endif
    saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return result;
}

void platform_format_mode(unsigned int mode, char out[11]) {
    out[0] = S_ISDIR(mode) ? 'd' :
             S_ISLNK(mode) ? 'l' :
             S_ISCHR(mode) ? 'c' :
             S_ISBLK(mode) ? 'b' :
             S_ISFIFO(mode) ? 'p' :
             S_ISSOCK(mode) ? 's' : '-';

    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? 'x' : '-';
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? 'x' : '-';
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}
