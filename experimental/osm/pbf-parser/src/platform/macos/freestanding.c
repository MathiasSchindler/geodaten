#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "platform.h"
#include "runtime.h"
#include "syscall.h"

#define DARWIN_PROT_READ 1
#define DARWIN_PROT_WRITE 2
#define DARWIN_MAP_PRIVATE 2
#define DARWIN_MAP_ANONYMOUS 0x1000
#define DARWIN_O_WRONLY 0x0001
#define DARWIN_O_APPEND 0x0008
#define DARWIN_O_CREAT 0x0200
#define DARWIN_O_TRUNC 0x0400
#define DARWIN_O_EXCL 0x0800
#define DARWIN_O_NOFOLLOW 0x0100
#define DARWIN_ACCESS_EXECUTE 1
#define DARWIN_ACCESS_WRITE 2
#define DARWIN_ACCESS_READ 4
#define DARWIN_S_IFMT 0170000U
#define DARWIN_S_IFDIR 0040000U
#define MACOS_ICMP_ECHO_REPLY 0
#define MACOS_ICMP_ECHO 8
#define MACOS_ICMP_TIME_EXCEEDED 11
#define MACOS_ICMPV6_ECHO_REQUEST 128
#define MACOS_ICMPV6_ECHO_REPLY 129
#define MACOS_ICMPV6_TIME_EXCEEDED 3

typedef struct {
    long tv_sec;
    int tv_usec;
} DarwinTimeval;

typedef struct {
    const char *name;
    int value;
} DarwinSignalEntry;

typedef struct {
    unsigned char type;
    unsigned char code;
    unsigned short checksum;
    unsigned short identifier;
    unsigned short sequence;
} MacosIcmpPacket;

typedef struct {
    unsigned char type;
    unsigned char code;
    unsigned short checksum;
    unsigned short identifier;
    unsigned short sequence;
} MacosIcmpv6Packet;

static const DarwinSignalEntry DARWIN_SIGNAL_TABLE[] = {
    { "HUP", SIGHUP },
    { "INT", SIGINT },
    { "QUIT", SIGQUIT },
    { "ILL", SIGILL },
    { "TRAP", SIGTRAP },
    { "ABRT", SIGABRT },
    { "BUS", SIGBUS },
    { "FPE", SIGFPE },
    { "KILL", SIGKILL },
    { "USR1", SIGUSR1 },
    { "SEGV", SIGSEGV },
    { "USR2", SIGUSR2 },
    { "PIPE", SIGPIPE },
    { "ALRM", SIGALRM },
    { "TERM", SIGTERM },
    { "CHLD", SIGCHLD },
    { "CONT", SIGCONT },
    { "STOP", SIGSTOP },
    { "TSTP", SIGTSTP },
    { "TTIN", SIGTTIN },
    { "TTOU", SIGTTOU }
};

static unsigned long long temp_path_counter;

extern char **environ;
int rename(const char *old_path, const char *new_path);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int getentropy(void *buffer, size_t size);

#if defined(__APPLE__)
int sethostname(const char *name, int namelen);
#endif

static int env_name_matches(const char *entry, const char *name) {
    size_t index = 0;

    if (entry == 0 || name == 0 || name[0] == '\0') {
        return 0;
    }

    while (name[index] != '\0') {
        if (entry[index] != name[index]) {
            return 0;
        }
        index += 1U;
    }

    return entry[index] == '=';
}

static int is_valid_env_name(const char *name) {
    size_t index = 0;

    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    while (name[index] != '\0') {
        if (name[index] == '=') {
            return 0;
        }
        index += 1U;
    }
    return 1;
}

static void process_state_string(int state, char *buffer, size_t buffer_size) {
    const char *text = "?";

    switch (state) {
        case SIDL: text = "I"; break;
        case SRUN: text = "R"; break;
        case SSLEEP: text = "S"; break;
        case SSTOP: text = "T"; break;
        case SZOMB: text = "Z"; break;
        default: break;
    }
    rt_copy_string(buffer, buffer_size, text);
}

static void fill_process_user(char *buffer, size_t buffer_size, unsigned int uid) {
    struct passwd *entry = getpwuid((uid_t)uid);

    if (entry != 0 && entry->pw_name != 0 && entry->pw_name[0] != '\0') {
        rt_copy_string(buffer, buffer_size, entry->pw_name);
    } else {
        rt_unsigned_to_string((unsigned long long)uid, buffer, buffer_size);
    }
}

static void make_raw_termios(struct termios *raw) {
    if (raw == 0) {
        return;
    }
    raw->c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw->c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | IEXTEN);
    raw->c_cflag &= ~(tcflag_t)(CSIZE | PARENB);
    raw->c_cflag |= CS8;
    raw->c_cc[VMIN] = 1;
    raw->c_cc[VTIME] = 0;
}

static void fill_entry_from_stat(const char *display_name, const struct stat *stat_info, PlatformDirEntry *entry) {
    memset(entry, 0, sizeof(*entry));
    rt_copy_string(entry->name, sizeof(entry->name), display_name != 0 ? display_name : "");
    entry->device = (unsigned long long)stat_info->st_dev;
    entry->mode = (unsigned int)stat_info->st_mode;
    entry->uid = (unsigned int)stat_info->st_uid;
    entry->gid = (unsigned int)stat_info->st_gid;
    entry->size = (unsigned long long)stat_info->st_size;
    entry->inode = (unsigned long long)stat_info->st_ino;
    entry->nlink = (unsigned long)stat_info->st_nlink;
    entry->atime = (long long)stat_info->st_atime;
    entry->mtime = (long long)stat_info->st_mtime;
    entry->ctime = (long long)stat_info->st_ctime;
    entry->is_dir = ((unsigned int)stat_info->st_mode & DARWIN_S_IFMT) == DARWIN_S_IFDIR;
    entry->is_hidden = display_name != 0 && display_name[0] == '.';
    rt_unsigned_to_string((unsigned long long)stat_info->st_uid, entry->owner, sizeof(entry->owner));
    rt_unsigned_to_string((unsigned long long)stat_info->st_gid, entry->group, sizeof(entry->group));
}

static int fill_entry_mode(const char *display_name, const char *path, PlatformDirEntry *entry, int follow_symlinks) {
    struct stat stat_info;
    long result;

    if (path == 0 || entry == 0) {
        return -1;
    }

    result = darwin_syscall2(follow_symlinks ? DARWIN_SYS_STAT64 : DARWIN_SYS_LSTAT64, (long)path, (long)&stat_info);
    if (result < 0) {
        return -1;
    }

    fill_entry_from_stat(display_name, &stat_info, entry);
    return 0;
}

static int access_mode_to_darwin(int mode) {
    int darwin_mode = 0;

    if ((mode & PLATFORM_ACCESS_READ) != 0) {
        darwin_mode |= DARWIN_ACCESS_READ;
    }
    if ((mode & PLATFORM_ACCESS_WRITE) != 0) {
        darwin_mode |= DARWIN_ACCESS_WRITE;
    }
    if ((mode & PLATFORM_ACCESS_EXECUTE) != 0) {
        darwin_mode |= DARWIN_ACCESS_EXECUTE;
    }

    return darwin_mode;
}

static int signal_name_matches(const char *text, const char *name) {
    size_t offset = 0;

    if (text[0] == 'S' && text[1] == 'I' && text[2] == 'G') {
        text += 3;
    }

    while (text[offset] != '\0' && name[offset] != '\0') {
        char left = text[offset];
        char right = name[offset];
        if (left >= 'a' && left <= 'z') {
            left = (char)(left - 'a' + 'A');
        }
        if (right >= 'a' && right <= 'z') {
            right = (char)(right - 'a' + 'A');
        }
        if (left != right) {
            return 0;
        }
        offset += 1U;
    }

    return text[offset] == '\0' && name[offset] == '\0';
}

static void fill_group_name(char *buffer, size_t buffer_size, unsigned int gid) {
    struct group *group_info;

    group_info = getgrgid((gid_t)gid);
    if (group_info != 0 && group_info->gr_name != 0 && group_info->gr_name[0] != '\0') {
        rt_copy_string(buffer, buffer_size, group_info->gr_name);
    } else {
        rt_unsigned_to_string((unsigned long long)gid, buffer, buffer_size);
    }
}

static void fill_identity_from_passwd(struct passwd *passwd_info, PlatformIdentity *identity_out) {
    identity_out->uid = (unsigned int)passwd_info->pw_uid;
    identity_out->gid = (unsigned int)passwd_info->pw_gid;

    if (passwd_info->pw_name != 0 && passwd_info->pw_name[0] != '\0') {
        rt_copy_string(identity_out->username, sizeof(identity_out->username), passwd_info->pw_name);
    } else {
        rt_unsigned_to_string((unsigned long long)identity_out->uid, identity_out->username, sizeof(identity_out->username));
    }
    fill_group_name(identity_out->groupname, sizeof(identity_out->groupname), identity_out->gid);
}

static int join_path_local(const char *base, const char *name, char *buffer, size_t buffer_size) {
    size_t base_len;
    size_t name_len;
    int need_slash;

    if (base == 0 || name == 0 || buffer == 0 || buffer_size == 0) {
        return -1;
    }

    base_len = rt_strlen(base);
    name_len = rt_strlen(name);
    need_slash = base_len > 0U && base[base_len - 1U] != '/';

    if (base_len + (need_slash ? 1U : 0U) + name_len + 1U > buffer_size) {
        return -1;
    }

    rt_copy_string(buffer, buffer_size, base);
    if (need_slash) {
        buffer[base_len] = '/';
        buffer[base_len + 1U] = '\0';
    }
    rt_copy_string(buffer + rt_strlen(buffer), buffer_size - rt_strlen(buffer), name);
    return 0;
}

long platform_write(int fd, const void *buffer, size_t count) {
    return darwin_syscall3(DARWIN_SYS_WRITE, (long)fd, (long)buffer, (long)count);
}

long platform_read(int fd, void *buffer, size_t count) {
    return darwin_syscall3(DARWIN_SYS_READ, (long)fd, (long)buffer, (long)count);
}

void *platform_allocate_pages(size_t size) {
    long mapped = darwin_syscall6(
        DARWIN_SYS_MMAP,
        0,
        (long)size,
        DARWIN_PROT_READ | DARWIN_PROT_WRITE,
        DARWIN_MAP_PRIVATE | DARWIN_MAP_ANONYMOUS,
        -1,
        0
    );
    return mapped < 0 ? 0 : (void *)mapped;
}

int platform_close(int fd) {
    if (fd >= 0 && fd <= 2) {
        return 0;
    }
    return darwin_syscall1(DARWIN_SYS_CLOSE, (long)fd) < 0 ? -1 : 0;
}

int platform_open_read(const char *path) {
    long fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return 0;
    }

    fd = darwin_syscall3(DARWIN_SYS_OPEN, (long)path, 0, 0);
    return fd < 0 ? -1 : (int)fd;
}

int platform_open_read_secure(const char *path, PlatformDirEntry *entry_out) {
    struct stat stat_info;
    long fd;

    if (path == 0 || entry_out == 0) {
        return -1;
    }

    fd = darwin_syscall3(DARWIN_SYS_OPEN, (long)path, DARWIN_O_NOFOLLOW, 0);
    if (fd < 0) {
        return -1;
    }
    if (fstat((int)fd, &stat_info) != 0) {
        (void)platform_close((int)fd);
        return -1;
    }

    fill_entry_from_stat(path, &stat_info, entry_out);
    return (int)fd;
}

int platform_open_write_mode(const char *path, unsigned int mode, int truncate_existing) {
    long flags = DARWIN_O_WRONLY | DARWIN_O_CREAT;
    long fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return 1;
    }

    if (truncate_existing) {
        flags |= DARWIN_O_TRUNC;
    }

    fd = darwin_syscall3(DARWIN_SYS_OPEN, (long)path, flags, (long)mode);
    return fd < 0 ? -1 : (int)fd;
}

int platform_open_write(const char *path, unsigned int mode) {
    return platform_open_write_mode(path, mode, 1);
}

int platform_open_create_exclusive(const char *path, unsigned int mode) {
    long fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return -1;
    }

    fd = darwin_syscall3(DARWIN_SYS_OPEN, (long)path, DARWIN_O_WRONLY | DARWIN_O_CREAT | DARWIN_O_EXCL, (long)mode);
    return fd < 0 ? -1 : (int)fd;
}

int platform_create_temp_file(char *path_buffer, size_t buffer_size, const char *prefix, unsigned int mode) {
    static const char hex_digits[] = "0123456789abcdef";
    const char *base = (prefix != 0 && prefix[0] != '\0') ? prefix : "/tmp/newos-tmp-";
    const char *tmpdir = platform_getenv("TMPDIR");
    char local_base[512];
    size_t base_len;
    unsigned int attempt;

    if (path_buffer == 0 || buffer_size == 0) {
        return -1;
    }

    if (tmpdir != 0 && tmpdir[0] != '\0' && base[0] == '/' && base[1] == 't' && base[2] == 'm' && base[3] == 'p' && base[4] == '/') {
        size_t tmpdir_len = rt_strlen(tmpdir);
        const char *name = base + 5;
        size_t name_len = rt_strlen(name);
        size_t offset = 0;

        if (tmpdir_len + 1U + name_len + 1U > sizeof(local_base)) {
            return -1;
        }
        rt_copy_string(local_base, sizeof(local_base), tmpdir);
        offset = tmpdir_len;
        if (offset > 0U && local_base[offset - 1U] != '/') {
            local_base[offset++] = '/';
            local_base[offset] = '\0';
        }
        rt_copy_string(local_base + offset, sizeof(local_base) - offset, name);
        base = local_base;
    }

    base_len = rt_strlen(base);
    for (attempt = 0; attempt < 64U; ++attempt) {
        unsigned long long value = ((unsigned long long)platform_get_process_id() << 32) ^
                                   ((unsigned long long)platform_get_epoch_time() << 12) ^
                                   temp_path_counter ^ (unsigned long long)attempt;
        char suffix[17];
        size_t index;
        long fd;

        temp_path_counter += 0x9e3779b97f4a7c15ULL;
        for (index = 0U; index < 16U; ++index) {
            suffix[index] = hex_digits[(value >> ((15U - index) * 4U)) & 0x0fU];
        }
        suffix[16] = '\0';

        if (base_len + 17U > buffer_size) {
            return -1;
        }
        rt_copy_string(path_buffer, buffer_size, base);
        rt_copy_string(path_buffer + base_len, buffer_size - base_len, suffix);

        fd = darwin_syscall3(
            DARWIN_SYS_OPEN,
            (long)path_buffer,
            DARWIN_O_WRONLY | DARWIN_O_CREAT | DARWIN_O_EXCL | DARWIN_O_TRUNC,
            (long)mode
        );
        if (fd >= 0) {
            return (int)fd;
        }
    }

    return -1;
}

int platform_open_append(const char *path, unsigned int mode) {
    long fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return 1;
    }

    fd = darwin_syscall3(DARWIN_SYS_OPEN, (long)path, DARWIN_O_WRONLY | DARWIN_O_CREAT | DARWIN_O_APPEND, (long)mode);
    return fd < 0 ? -1 : (int)fd;
}

int platform_open_append_existing(const char *path) {
    long fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return 1;
    }

    fd = darwin_syscall3(DARWIN_SYS_OPEN, (long)path, DARWIN_O_WRONLY | DARWIN_O_APPEND, 0);
    return fd < 0 ? -1 : (int)fd;
}

long long platform_seek(int fd, long long offset, int whence) {
    long result = darwin_syscall3(DARWIN_SYS_LSEEK, (long)fd, (long)offset, (long)whence);
    return result < 0 ? -1 : (long long)result;
}

const char *platform_getenv(const char *name) {
    size_t index = 0;

    if (environ == 0) {
        return 0;
    }

    while (environ[index] != 0) {
        if (env_name_matches(environ[index], name)) {
            return environ[index] + rt_strlen(name) + 1U;
        }
        index += 1U;
    }

    return 0;
}

const char *platform_getenv_entry(size_t index) {
    if (environ == 0) {
        return 0;
    }

    return environ[index];
}

int platform_setenv(const char *name, const char *value, int overwrite) {
    if (!is_valid_env_name(name)) {
        return -1;
    }
    return setenv(name, value != 0 ? value : "", overwrite);
}

int platform_unsetenv(const char *name) {
    if (!is_valid_env_name(name)) {
        return -1;
    }
    return unsetenv(name);
}

int platform_clearenv(void) {
    static char *empty_environment[] = { 0 };

    environ = empty_environment;
    return 0;
}

int platform_isatty(int fd) {
    struct termios term;
    long result;

    if (fd < 0) {
        return 0;
    }
    result = darwin_syscall3(DARWIN_SYS_IOCTL, (long)fd, (long)TIOCGETA, (long)&term);
    return result >= 0;
}

int platform_make_directory(const char *path, unsigned int mode) {
    return darwin_syscall2(DARWIN_SYS_MKDIR, (long)path, (long)mode) < 0 ? -1 : 0;
}

int platform_remove_directory(const char *path) {
    return darwin_syscall1(DARWIN_SYS_RMDIR, (long)path) < 0 ? -1 : 0;
}

int platform_remove_file(const char *path) {
    return darwin_syscall1(DARWIN_SYS_UNLINK, (long)path) < 0 ? -1 : 0;
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

int platform_create_node(const char *path, unsigned int node_type, unsigned int mode, unsigned int major, unsigned int minor) {
    mode_t node_mode = (mode_t)(mode & 07777U);

    if (path == 0) {
        return -1;
    }
    if (node_type == PLATFORM_NODE_FIFO) {
        return mkfifo(path, node_mode);
    }
    if (node_type == PLATFORM_NODE_CHAR) {
        node_mode |= S_IFCHR;
    } else if (node_type == PLATFORM_NODE_BLOCK) {
        node_mode |= S_IFBLK;
    } else {
        return -1;
    }
    return mknod(path, node_mode, makedev((int)major, (int)minor));
}

int platform_mount_filesystem(
    const char *source,
    const char *target,
    const char *filesystem_type,
    unsigned long long flags,
    const char *data
) {
    (void)source;
    (void)target;
    (void)filesystem_type;
    (void)flags;
    (void)data;
    return -1;
}

int platform_unmount_filesystem(const char *target, int force, int lazy) {
    int flags = 0;

    if (target == 0 || target[0] == '\0' || lazy) {
        return -1;
    }
#ifdef MNT_FORCE
    if (force) {
        flags |= MNT_FORCE;
    }
#else
    (void)force;
#endif
    return unmount(target, flags);
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

int platform_path_is_directory(const char *path, int *is_directory_out) {
    PlatformDirEntry entry;

    if (platform_get_path_info_follow(path, &entry) != 0) {
        return -1;
    }

    if (is_directory_out != 0) {
        *is_directory_out = entry.is_dir;
    }
    return 0;
}

int platform_get_current_directory(char *buffer, size_t buffer_size) {
    return getcwd(buffer, buffer_size) == 0 ? -1 : 0;
}

int platform_get_path_info(const char *path, PlatformDirEntry *entry_out) {
    return fill_entry_mode(path, path, entry_out, 0);
}

int platform_get_path_info_follow(const char *path, PlatformDirEntry *entry_out) {
    return fill_entry_mode(path, path, entry_out, 1);
}

int platform_read_symlink(const char *path, char *buffer, size_t buffer_size) {
    long result;

    if (buffer == 0 || buffer_size == 0) {
        return -1;
    }

    result = readlink(path, buffer, buffer_size - 1U);
    if (result < 0) {
        return -1;
    }

    buffer[result] = '\0';
    return 0;
}

int platform_path_access(const char *path, int mode) {
    return darwin_syscall2(DARWIN_SYS_ACCESS, (long)path, (long)access_mode_to_darwin(mode)) < 0 ? -1 : 0;
}

int platform_change_directory(const char *path) {
    if (path == 0 || path[0] == '\0') {
        return -1;
    }
    return chdir(path);
}

int platform_collect_entries(
    const char *path,
    int include_hidden,
    PlatformDirEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int *path_is_directory
) {
    PlatformDirEntry current;
    size_t count = 0;
    DIR *directory;
    struct dirent *entry;

    if (path == 0 || entries_out == 0 || count_out == 0 || path_is_directory == 0) {
        return -1;
    }

    *count_out = 0;
    *path_is_directory = 0;

    if (platform_get_path_info(path, &current) != 0) {
        return -1;
    }
    if (!current.is_dir) {
        if (entry_capacity == 0) {
            return -1;
        }
        entries_out[0] = current;
        *count_out = 1;
        return 0;
    }

    directory = opendir(path);
    if (directory == 0) {
        return -1;
    }

    while ((entry = readdir(directory)) != 0) {
        char full_path[1024];

        if (!include_hidden && entry->d_name[0] == '.') {
            continue;
        }
        if (count >= entry_capacity) {
            closedir(directory);
            return -1;
        }
        if (join_path_local(path, entry->d_name, full_path, sizeof(full_path)) != 0) {
            closedir(directory);
            return -1;
        }
        if (fill_entry_mode(entry->d_name, full_path, &entries_out[count], 0) != 0) {
            closedir(directory);
            return -1;
        }
        count += 1U;
    }

    closedir(directory);
    *count_out = count;
    *path_is_directory = 1;
    return 0;
}

void platform_free_entries(PlatformDirEntry *entries, size_t count) {
    (void)entries;
    (void)count;
}

int platform_get_filesystem_info(const char *path, PlatformFilesystemInfo *info_out) {
    struct statfs info;
    unsigned long long block_size;

    if (path == 0 || info_out == 0) {
        return -1;
    }

    memset(info_out, 0, sizeof(*info_out));
    if (statfs(path, &info) != 0) {
        return -1;
    }

    block_size = info.f_bsize > 0 ? (unsigned long long)info.f_bsize : 512ULL;
    info_out->total_bytes = (unsigned long long)info.f_blocks * block_size;
    info_out->free_bytes = (unsigned long long)info.f_bfree * block_size;
    info_out->available_bytes = (unsigned long long)info.f_bavail * block_size;
    info_out->total_inodes = (unsigned long long)info.f_files;
    info_out->free_inodes = (unsigned long long)info.f_ffree;
    info_out->available_inodes = (unsigned long long)info.f_ffree;
    rt_copy_string(info_out->type_name, sizeof(info_out->type_name), info.f_fstypename[0] != '\0' ? info.f_fstypename : "darwin");
    return 0;
}

int platform_get_filesystem_usage(
    const char *path,
    unsigned long long *total_bytes_out,
    unsigned long long *free_bytes_out,
    unsigned long long *available_bytes_out
) {
    PlatformFilesystemInfo info;

    if (total_bytes_out == 0 || free_bytes_out == 0 || available_bytes_out == 0) {
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

int platform_ignore_signal(int signal_number) {
    (void)signal_number;
    return -1;
}

int platform_send_signal(int pid, int signal_number) {
    return kill((pid_t)pid, signal_number);
}

int platform_parse_signal_name(const char *text, int *signal_out) {
    unsigned long long numeric;
    size_t index;

    if (text == 0 || text[0] == '\0' || signal_out == 0) {
        return -1;
    }
    if (rt_parse_uint(text, &numeric) == 0) {
        *signal_out = (int)numeric;
        return 0;
    }

    for (index = 0; index < sizeof(DARWIN_SIGNAL_TABLE) / sizeof(DARWIN_SIGNAL_TABLE[0]); ++index) {
        if (signal_name_matches(text, DARWIN_SIGNAL_TABLE[index].name)) {
            *signal_out = DARWIN_SIGNAL_TABLE[index].value;
            return 0;
        }
    }

    return -1;
}

const char *platform_signal_name(int signal_number) {
    size_t index;

    for (index = 0; index < sizeof(DARWIN_SIGNAL_TABLE) / sizeof(DARWIN_SIGNAL_TABLE[0]); ++index) {
        if (DARWIN_SIGNAL_TABLE[index].value == signal_number) {
            return DARWIN_SIGNAL_TABLE[index].name;
        }
    }

    return "UNKNOWN";
}

void platform_write_signal_list(int fd) {
    size_t index;

    for (index = 0; index < sizeof(DARWIN_SIGNAL_TABLE) / sizeof(DARWIN_SIGNAL_TABLE[0]); ++index) {
        if (index > 0) {
            (void)platform_write(fd, " ", 1U);
        }
        (void)platform_write(fd, DARWIN_SIGNAL_TABLE[index].name, rt_strlen(DARWIN_SIGNAL_TABLE[index].name));
    }
    (void)platform_write(fd, "\n", 1U);
}

int platform_sleep_milliseconds(unsigned long long milliseconds) {
    DarwinTimeval timeout;

    timeout.tv_sec = (long)(milliseconds / 1000ULL);
    timeout.tv_usec = (int)((milliseconds % 1000ULL) * 1000ULL);

    return darwin_syscall5(DARWIN_SYS_SELECT, 0, 0, 0, 0, (long)&timeout) < 0 ? -1 : 0;
}

int platform_sleep_seconds(unsigned int seconds) {
    return platform_sleep_milliseconds((unsigned long long)seconds * 1000ULL);
}

long long platform_get_epoch_time(void) {
    struct timeval now;

    if (gettimeofday(&now, 0) != 0) {
        return 0;
    }

    return (long long)now.tv_sec;
}

unsigned long long platform_get_monotonic_time_ns(void) {
    struct timeval now;

    if (gettimeofday(&now, 0) != 0) {
        return 0ULL;
    }

    return ((unsigned long long)now.tv_sec * 1000000000ULL) + ((unsigned long long)now.tv_usec * 1000ULL);
}

int platform_format_time(long long epoch_seconds, int use_local_time, const char *format, char *buffer, size_t buffer_size) {
    time_t when;
    struct tm time_value;
    struct tm *time_ptr;
    const char *actual_format;

    if (buffer == 0 || buffer_size == 0) {
        return -1;
    }

    when = (time_t)epoch_seconds;
    actual_format = (format != 0 && format[0] != '\0') ? format : "%Y-%m-%d %H:%M:%S";
    time_ptr = use_local_time ? localtime_r(&when, &time_value) : gmtime_r(&when, &time_value);
    if (time_ptr == 0) {
        buffer[0] = '\0';
        return -1;
    }

    return strftime(buffer, buffer_size, actual_format, time_ptr) == 0 ? -1 : 0;
}

int platform_get_hostname(char *buffer, size_t buffer_size) {
    if (buffer == 0 || buffer_size == 0) {
        return -1;
    }
    buffer[0] = '\0';
    if (gethostname(buffer, buffer_size) != 0) {
        return -1;
    }
    buffer[buffer_size - 1U] = '\0';
    return 0;
}

int platform_set_hostname(const char *name) {
    if (name == 0) {
        return -1;
    }
    return sethostname(name, (int)rt_strlen(name));
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

    rt_copy_string(sysname, sysname_size, info.sysname);
    rt_copy_string(nodename, nodename_size, info.nodename);
    rt_copy_string(release, release_size, info.release);
    rt_copy_string(version, version_size, info.version);
    rt_copy_string(machine, machine_size, info.machine);
    return 0;
}

int platform_lookup_group(const char *groupname, unsigned int *gid_out) {
    struct group *group_info;
    unsigned long long value;

    if (groupname == 0 || groupname[0] == '\0' || gid_out == 0) {
        return -1;
    }

    if (rt_parse_uint(groupname, &value) == 0) {
        *gid_out = (unsigned int)value;
        return 0;
    }

    group_info = getgrnam(groupname);
    if (group_info == 0) {
        return -1;
    }

    *gid_out = (unsigned int)group_info->gr_gid;
    return 0;
}

int platform_lookup_identity(const char *username, PlatformIdentity *identity_out) {
    struct passwd *passwd_info;
    unsigned int uid;

    if (identity_out == 0) {
        return -1;
    }

    if (username != 0 && username[0] != '\0') {
        unsigned long long value;
        if (rt_parse_uint(username, &value) == 0) {
            passwd_info = getpwuid((uid_t)value);
        } else {
            passwd_info = getpwnam(username);
        }
        if (passwd_info == 0) {
            return -1;
        }
        fill_identity_from_passwd(passwd_info, identity_out);
        return 0;
    }

    uid = (unsigned int)darwin_syscall0(DARWIN_SYS_GETUID);
    passwd_info = getpwuid((uid_t)uid);
    if (passwd_info != 0) {
        fill_identity_from_passwd(passwd_info, identity_out);
        return 0;
    }

    identity_out->uid = uid;
    identity_out->gid = (unsigned int)darwin_syscall0(DARWIN_SYS_GETGID);
    rt_unsigned_to_string((unsigned long long)identity_out->uid, identity_out->username, sizeof(identity_out->username));
    fill_group_name(identity_out->groupname, sizeof(identity_out->groupname), identity_out->gid);
    return 0;
}

int platform_get_identity(PlatformIdentity *identity_out) {
    return platform_lookup_identity(0, identity_out);
}

int platform_list_groups_for_identity(
    const PlatformIdentity *identity,
    PlatformGroupEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out
) {
    int group_ids[256];
    long group_count;
    size_t index;
    size_t stored = 0;

    if (identity == 0 || count_out == 0 || (entry_capacity > 0 && entries_out == 0)) {
        return -1;
    }

    *count_out = 0;
    if (entry_capacity == 0) {
        return 0;
    }

    group_count = darwin_syscall2(DARWIN_SYS_GETGROUPS, (long)(sizeof(group_ids) / sizeof(group_ids[0])), (long)group_ids);
    if (group_count < 0) {
        group_count = 0;
    }

    entries_out[stored].gid = identity->gid;
    rt_copy_string(entries_out[stored].name, sizeof(entries_out[stored].name), identity->groupname);
    stored += 1U;

    for (index = 0; index < (size_t)group_count && stored < entry_capacity; ++index) {
        size_t existing;
        int duplicate = 0;
        unsigned int gid = (unsigned int)group_ids[index];

        for (existing = 0; existing < stored; ++existing) {
            if (entries_out[existing].gid == gid) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        entries_out[stored].gid = gid;
        fill_group_name(entries_out[stored].name, sizeof(entries_out[stored].name), gid);
        stored += 1U;
    }

    *count_out = stored;
    return 0;
}

int platform_get_memory_info(PlatformMemoryInfo *info_out) {
    unsigned long long total_memory = 0;
    size_t total_size = sizeof(total_memory);

    if (info_out == 0) {
        return -1;
    }

    memset(info_out, 0, sizeof(*info_out));
    if (sysctlbyname("hw.memsize", &total_memory, &total_size, 0, 0) != 0) {
        return -1;
    }

    info_out->total_bytes = total_memory;
    info_out->free_bytes = total_memory / 2ULL;
    info_out->available_bytes = total_memory / 2ULL;
    return 0;
}

int platform_get_uptime_info(PlatformUptimeInfo *info_out) {
    if (info_out == 0) {
        return -1;
    }

    info_out->uptime_seconds = 0;
    rt_copy_string(info_out->load_average, sizeof(info_out->load_average), "0.00 0.00 0.00");
    return 0;
}

int platform_list_sessions(PlatformSessionEntry *entries_out, size_t entry_capacity, size_t *count_out) {
    (void)entries_out;
    (void)entry_capacity;
    (void)count_out;

    return -1;
}

int platform_set_path_times(
    const char *path,
    long long atime,
    long long mtime,
    int create_if_missing,
    int update_access,
    int update_modify
) {
    PlatformDirEntry entry;
    DarwinTimeval times[2];
    int fd;

    if (path == 0) {
        return -1;
    }
    if (!update_access && !update_modify) {
        return 0;
    }

    if (platform_get_path_info(path, &entry) != 0) {
        if (!create_if_missing) {
            return -1;
        }
        fd = platform_open_write_mode(path, 0644U, 0);
        if (fd < 0) {
            return -1;
        }
        (void)platform_close(fd);
        if (platform_get_path_info(path, &entry) != 0) {
            return -1;
        }
    }

    if (!update_access) {
        atime = entry.atime;
    }
    if (!update_modify) {
        mtime = entry.mtime;
    }

    times[0].tv_sec = (long)atime;
    times[0].tv_usec = 0;
    times[1].tv_sec = (long)mtime;
    times[1].tv_usec = 0;
    return darwin_syscall2(DARWIN_SYS_UTIMES, (long)path, (long)times) < 0 ? -1 : 0;
}

int platform_truncate_path(const char *path, unsigned long long size) {
    int fd;
    long result;

    if (path == 0) {
        return -1;
    }

    fd = platform_open_write_mode(path, 0644U, 0);
    if (fd < 0) {
        return -1;
    }

    result = darwin_syscall2(DARWIN_SYS_FTRUNCATE, (long)fd, (long)size);
    (void)platform_close(fd);
    return result < 0 ? -1 : 0;
}

int platform_sync_all(void) {
    return darwin_syscall0(DARWIN_SYS_SYNC) < 0 ? -1 : 0;
}

long platform_read_kernel_log(char *buffer, size_t buffer_size, int clear_after_read) {
    size_t needed = 0U;

    if (buffer == 0 || buffer_size == 0U) {
        return -1;
    }
    if (clear_after_read) {
        return -1;
    }
    if (sysctlbyname("kern.msgbuf", 0, &needed, 0, 0) != 0 || needed == 0U) {
        return -1;
    }
    if (needed > buffer_size - 1U) {
        needed = buffer_size - 1U;
    }
    if (sysctlbyname("kern.msgbuf", buffer, &needed, 0, 0) != 0) {
        return -1;
    }
    buffer[needed] = '\0';
    return (long)needed;
}

int platform_clear_kernel_log(void) {
    return -1;
}

int platform_set_console_log_level(int level) {
    (void)level;
    return -1;
}

int platform_shutdown_system(int action) {
    (void)action;
    return -1;
}

static int open_sync_fd(const char *path) {
    int fd;

    fd = platform_open_read(path);
    if (fd < 0) {
        fd = platform_open_append_existing(path);
    }

    return fd;
}

int platform_sync_path(const char *path) {
    int fd;
    long result;

    fd = open_sync_fd(path);
    if (fd < 0) {
        return -1;
    }

    result = darwin_syscall1(DARWIN_SYS_FSYNC, (long)fd);
    (void)platform_close(fd);
    return result < 0 ? -1 : 0;
}

int platform_sync_path_data(const char *path) {
    return platform_sync_path(path);
}

int platform_get_process_id(void) {
    long pid = darwin_syscall0(DARWIN_SYS_GETPID);
    return pid < 0 ? -1 : (int)pid;
}

int platform_drop_privileges(const char *username, const char *groupname) {
    (void)username;
    (void)groupname;
    return -1;
}

int platform_spawn_process_ex(
    char *const argv[],
    int stdin_fd,
    int stdout_fd,
    const char *input_path,
    const char *output_path,
    int output_append,
    const char *working_directory,
    const char *drop_user,
    const char *drop_group,
    int *pid_out
) {
    int pid;

    if (argv == 0 || argv[0] == 0 || pid_out == 0) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        int fd;

        if (working_directory != 0 && working_directory[0] != '\0' && chdir(working_directory) != 0) {
            _exit(126);
        }
        if ((drop_user != 0 && drop_user[0] != '\0') || (drop_group != 0 && drop_group[0] != '\0')) {
            _exit(126);
        }

        if (input_path != 0) {
            fd = platform_open_read(input_path);
            if (fd < 0) {
                _exit(126);
            }
            if (fd != 0) {
                if (dup2(fd, 0) < 0) {
                    _exit(126);
                }
                (void)platform_close(fd);
            }
        } else if (stdin_fd >= 0 && stdin_fd != 0) {
            if (dup2(stdin_fd, 0) < 0) {
                _exit(126);
            }
        }

        if (output_path != 0) {
            fd = platform_open_write_mode(output_path, 0644U, output_append ? 0 : 1);
            if (fd < 0) {
                _exit(126);
            }
            if (fd != 1) {
                if (dup2(fd, 1) < 0) {
                    _exit(126);
                }
                (void)platform_close(fd);
            }
        } else if (stdout_fd >= 0 && stdout_fd != 1) {
            if (dup2(stdout_fd, 1) < 0) {
                _exit(126);
            }
        }

        if (output_path != 0 || stdout_fd >= 0) {
            if (dup2(1, 2) < 0) {
                _exit(126);
            }
        }
        if (stdin_fd > 2) {
            (void)platform_close(stdin_fd);
        }
        if (stdout_fd > 2) {
            (void)platform_close(stdout_fd);
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    *pid_out = pid;
    return 0;
}

int platform_spawn_process(
    char *const argv[],
    int stdin_fd,
    int stdout_fd,
    const char *input_path,
    const char *output_path,
    int output_append,
    int *pid_out
) {
    return platform_spawn_process_ex(argv, stdin_fd, stdout_fd, input_path, output_path, output_append, 0, 0, 0, pid_out);
}

static int decode_wait_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

int platform_trace_syscalls(char *const argv[], PlatformSyscallTraceCallback callback, void *user_data, int *exit_status_out) {
    (void)argv;
    (void)callback;
    (void)user_data;
    if (exit_status_out != 0) *exit_status_out = 1;
    return -1;
}

int platform_wait_process(int pid, int *exit_status_out) {
    int status = 0;

    if (exit_status_out == 0) {
        return -1;
    }
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    *exit_status_out = decode_wait_status(status);
    return 0;
}

int platform_poll_process_exit(int pid, int *finished_out, int *exit_status_out) {
    int status = 0;
    int waited;

    if (finished_out == 0 || exit_status_out == 0) {
        return -1;
    }
    waited = waitpid(pid, &status, WNOHANG);
    if (waited == 0) {
        *finished_out = 0;
        *exit_status_out = 0;
        return 0;
    }
    if (waited < 0) {
        return -1;
    }
    *finished_out = 1;
    *exit_status_out = decode_wait_status(status);
    return 0;
}

int platform_wait_process_timeout(
    int pid,
    unsigned long long timeout_milliseconds,
    unsigned long long kill_after_milliseconds,
    int signal_number,
    int preserve_status,
    int *exit_status_out
) {
    unsigned long long elapsed = 0;
    unsigned long long after_signal = 0;
    int timed_out = 0;

    if (exit_status_out == 0) {
        return -1;
    }

    for (;;) {
        int finished = 0;
        int status = 0;

        if (platform_poll_process_exit(pid, &finished, &status) != 0) {
            return -1;
        }
        if (finished) {
            *exit_status_out = (timed_out && !preserve_status) ? 124 : status;
            return 0;
        }

        if (!timed_out && elapsed >= timeout_milliseconds) {
            (void)platform_send_signal(pid, signal_number);
            timed_out = 1;
            after_signal = 0;
        } else if (timed_out && kill_after_milliseconds > 0 && after_signal >= kill_after_milliseconds) {
            (void)platform_send_signal(pid, SIGKILL);
            kill_after_milliseconds = 0;
        }

        if (platform_sleep_milliseconds(50ULL) != 0) {
            return -1;
        }
        if (!timed_out) {
            elapsed += 50ULL;
        } else {
            after_signal += 50ULL;
        }
    }
}

int platform_list_processes(PlatformProcessEntry *entries_out, size_t entry_capacity, size_t *count_out) {
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    size_t buffer_size = 0;
    struct kinfo_proc *processes;
    size_t process_count;
    size_t index;
    size_t count = 0;

    if (entries_out == 0 || count_out == 0) {
        return -1;
    }
    if (sysctl(mib, 4U, 0, &buffer_size, 0, 0) != 0 || buffer_size == 0) {
        return -1;
    }

    processes = (struct kinfo_proc *)platform_allocate_pages(buffer_size);
    if (processes == 0) {
        return -1;
    }
    if (sysctl(mib, 4U, processes, &buffer_size, 0, 0) != 0) {
        return -1;
    }

    process_count = buffer_size / sizeof(processes[0]);
    for (index = 0; index < process_count && count < entry_capacity; ++index) {
        struct kinfo_proc *process = &processes[index];
        PlatformProcessEntry *entry = &entries_out[count];
        int pid = process->kp_proc.p_pid;
        unsigned int uid = (unsigned int)process->kp_eproc.e_ucred.cr_uid;

        if (pid <= 0) {
            continue;
        }

        memset(entry, 0, sizeof(*entry));
        entry->pid = pid;
        entry->ppid = process->kp_eproc.e_ppid;
        entry->uid = uid;
        entry->rss_kb = 0;
        process_state_string(process->kp_proc.p_stat, entry->state, sizeof(entry->state));
        fill_process_user(entry->user, sizeof(entry->user), uid);
        rt_copy_string(entry->name, sizeof(entry->name), process->kp_proc.p_comm[0] != '\0' ? process->kp_proc.p_comm : "?");
        count += 1U;
    }

    *count_out = count;
    return 0;
}

int platform_random_bytes(unsigned char *buffer, size_t count) {
    size_t offset = 0;

    if (buffer == 0) {
        return -1;
    }
    while (offset < count) {
        size_t chunk = count - offset;
        if (chunk > 256U) {
            chunk = 256U;
        }
        if (getentropy(buffer + offset, chunk) != 0) {
            return -1;
        }
        offset += chunk;
    }
    return 0;
}

int platform_connect_tcp(const char *host, unsigned int port, int *socket_fd_out) {
    struct addrinfo hints;
    struct addrinfo *results = 0;
    struct addrinfo *current;
    char port_text[16];
    int sock = -1;
    int last_error = EHOSTUNREACH;

    if (host == 0 || host[0] == '\0' || socket_fd_out == 0 || port == 0U || port > 65535U) {
        errno = EINVAL;
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    rt_unsigned_to_string((unsigned long long)port, port_text, sizeof(port_text));

    if (getaddrinfo(host, port_text, &hints, &results) != 0) {
        errno = EHOSTUNREACH;
        return -1;
    }

    for (current = results; current != 0; current = current->ai_next) {
        sock = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (sock < 0) {
            continue;
        }
        if (connect(sock, current->ai_addr, current->ai_addrlen) == 0) {
            break;
        }
        {
            int socket_error = 0;
            socklen_t socket_error_size = (socklen_t)sizeof(socket_error);
            last_error = errno;
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) == 0 && socket_error != 0) {
                last_error = socket_error;
            }
        }
        (void)platform_close(sock);
        sock = -1;
    }

    freeaddrinfo(results);
    if (sock < 0) {
        errno = last_error;
        return -1;
    }

    *socket_fd_out = sock;
    return 0;
}

static int macos_connect_status_from_errno(int error_code) {
    switch (error_code) {
    case ECONNREFUSED:
        return PLATFORM_CONNECT_STATUS_CLOSED;
    case ETIMEDOUT:
#ifdef EAGAIN
    case EAGAIN:
#endif
#ifdef EWOULDBLOCK
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
#endif
#ifdef EINPROGRESS
    case EINPROGRESS:
#endif
#ifdef EACCES
    case EACCES:
#endif
#ifdef EPERM
    case EPERM:
#endif
        return PLATFORM_CONNECT_STATUS_FILTERED;
#ifdef EHOSTUNREACH
    case EHOSTUNREACH:
#endif
#ifdef ENETUNREACH
    case ENETUNREACH:
#endif
#ifdef EHOSTDOWN
    case EHOSTDOWN:
#endif
#ifdef ENETDOWN
    case ENETDOWN:
#endif
#ifdef EADDRNOTAVAIL
    case EADDRNOTAVAIL:
#endif
        return PLATFORM_CONNECT_STATUS_UNREACHABLE;
    default:
        return PLATFORM_CONNECT_STATUS_ERROR;
    }
}

int platform_open_tcp_listener(const char *host, unsigned int port, int *socket_fd_out) {
    struct addrinfo hints;
    struct addrinfo *results = 0;
    struct addrinfo *current;
    char port_text[16];
    int sock = -1;
    int reuse = 1;

    if (socket_fd_out == 0 || port == 0U || port > 65535U) {
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    rt_unsigned_to_string((unsigned long long)port, port_text, sizeof(port_text));

    if (getaddrinfo(host != 0 && host[0] != '\0' ? host : 0, port_text, &hints, &results) != 0) {
        return -1;
    }

    for (current = results; current != 0; current = current->ai_next) {
        sock = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (sock < 0) {
            continue;
        }
        (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (bind(sock, current->ai_addr, current->ai_addrlen) == 0 && listen(sock, 16) == 0) {
            break;
        }
        (void)platform_close(sock);
        sock = -1;
    }

    freeaddrinfo(results);
    if (sock < 0) {
        return -1;
    }

    *socket_fd_out = sock;
    return 0;
}

int platform_accept_tcp(int listener_fd, int *client_fd_out) {
    int client;

    if (listener_fd < 0 || client_fd_out == 0) {
        return -1;
    }

    client = accept(listener_fd, 0, 0);
    if (client < 0) {
        return -1;
    }
    *client_fd_out = client;
    return 0;
}

int platform_create_pipe(int pipe_fds[2]) {
    if (pipe_fds == 0) {
        return -1;
    }
    return pipe(pipe_fds);
}

static int macos_socket_to_fd(int socket_fd, int output_fd) {
    char buffer[4096];
    long amount;

    for (;;) {
        amount = recv(socket_fd, buffer, sizeof(buffer), 0);
        if (amount < 0) {
            return -1;
        }
        if (amount == 0) {
            return 0;
        }
        if (rt_write_all(output_fd, buffer, (size_t)amount) != 0) {
            return -1;
        }
    }
}

static int macos_fd_to_socket(int input_fd, int socket_fd) {
    char buffer[4096];
    long amount;

    for (;;) {
        amount = platform_read(input_fd, buffer, sizeof(buffer));
        if (amount < 0) {
            return -1;
        }
        if (amount == 0) {
            return 0;
        }
        if (send(socket_fd, buffer, (size_t)amount, 0) != amount) {
            return -1;
        }
    }
}

int platform_netcat(const char *host, unsigned int port, const PlatformNetcatOptions *options) {
    PlatformNetcatOptions defaults;
    int sock = -1;

    if (options == 0) {
        memset(&defaults, 0, sizeof(defaults));
        options = &defaults;
    }
    if (options->connect_status_out != 0) {
        *options->connect_status_out = PLATFORM_CONNECT_STATUS_ERROR;
    }
    if (options->use_udp) {
        return -1;
    }

    if (options->listen_mode) {
        int client = -1;
        const char *bind_host = options->bind_host[0] != '\0' ? options->bind_host : host;
        unsigned int listen_port = options->bind_port != 0U ? options->bind_port : port;

        if (platform_open_tcp_listener(bind_host, listen_port, &sock) != 0) {
            return -1;
        }
        if (platform_accept_tcp(sock, &client) != 0) {
            (void)platform_close(sock);
            return -1;
        }
        (void)platform_close(sock);
        if (!options->scan_mode && macos_socket_to_fd(client, 1) != 0) {
            (void)platform_close(client);
            return -1;
        }
        (void)platform_close(client);
        return 0;
    }

    if (platform_connect_tcp(host, port, &sock) != 0) {
        if (options->connect_status_out != 0) {
            *options->connect_status_out = macos_connect_status_from_errno(errno);
        }
        return -1;
    }
    if (options->connect_status_out != 0) {
        *options->connect_status_out = PLATFORM_CONNECT_STATUS_OPEN;
    }
    if (options->scan_mode) {
        if (options->banner_received_length != 0) {
            *options->banner_received_length = 0U;
        }
        if (options->banner_buffer != 0 && options->banner_capacity > 0U) {
            unsigned int read_timeout = options->banner_read_timeout_milliseconds;
            size_t ready_index = 0U;

            if (read_timeout == 0U) {
                read_timeout = 500U;
            }
            if (platform_poll_fds(&sock, 1U, &ready_index, (int)read_timeout) > 0) {
                long received = recv(sock, options->banner_buffer, (size_t)options->banner_capacity, 0);
                if (received > 0 && options->banner_received_length != 0) {
                    *options->banner_received_length = (unsigned int)received;
                }
            }
        }
        (void)platform_close(sock);
        return 0;
    }
    if (!platform_isatty(0)) {
        if (macos_fd_to_socket(0, sock) != 0) {
            (void)platform_close(sock);
            return -1;
        }
        (void)shutdown(sock, SHUT_WR);
    }
    if (macos_socket_to_fd(sock, 1) != 0) {
        (void)platform_close(sock);
        return -1;
    }
    (void)platform_close(sock);
    return 0;
}

int platform_netcat_tcp(const char *host, unsigned int port, int listen_mode) {
    PlatformNetcatOptions options;

    memset(&options, 0, sizeof(options));
    options.listen_mode = listen_mode;
    return platform_netcat(host, port, &options);
}

static int macos_dns_family_matches(int ai_family, int family_filter) {
    if (family_filter == PLATFORM_NETWORK_FAMILY_ANY) {
        return ai_family == AF_INET || ai_family == AF_INET6;
    }
    if (family_filter == PLATFORM_NETWORK_FAMILY_IPV4) {
        return ai_family == AF_INET;
    }
    if (family_filter == PLATFORM_NETWORK_FAMILY_IPV6) {
        return ai_family == AF_INET6;
    }
    return 0;
}

static int macos_add_dns_entry(
    PlatformDnsEntry *entries_out,
    size_t entry_capacity,
    size_t *count,
    const char *name,
    int ai_family,
    const void *address
) {
    PlatformDnsEntry *entry;

    if (*count >= entry_capacity) {
        return -1;
    }
    entry = &entries_out[*count];
    memset(entry, 0, sizeof(*entry));
    entry->record_type = ai_family == AF_INET6 ? PLATFORM_DNS_RECORD_AAAA : PLATFORM_DNS_RECORD_A;
    rt_copy_string(entry->name, sizeof(entry->name), name);
    if (inet_ntop(ai_family, address, entry->address, sizeof(entry->address)) == 0) {
        return -1;
    }
    *count += 1U;
    return 0;
}

int platform_dns_lookup(
    const char *server,
    unsigned int port,
    const char *name,
    int family_filter,
    PlatformDnsEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out
) {
    struct addrinfo hints;
    struct addrinfo *results = 0;
    struct addrinfo *current;
    size_t count = 0U;

    (void)server;
    (void)port;
    if (name == 0 || entries_out == 0 || count_out == 0) {
        return -1;
    }
    *count_out = 0U;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family_filter == PLATFORM_NETWORK_FAMILY_IPV4 ? AF_INET :
        (family_filter == PLATFORM_NETWORK_FAMILY_IPV6 ? AF_INET6 : AF_UNSPEC);
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(name, 0, &hints, &results) != 0) {
        return -1;
    }

    for (current = results; current != 0; current = current->ai_next) {
        if (!macos_dns_family_matches(current->ai_family, family_filter)) {
            continue;
        }
        if (current->ai_family == AF_INET) {
            const struct sockaddr_in *sin = (const struct sockaddr_in *)current->ai_addr;
            (void)macos_add_dns_entry(entries_out, entry_capacity, &count, name, AF_INET, &sin->sin_addr);
        } else if (current->ai_family == AF_INET6) {
            const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)current->ai_addr;
            (void)macos_add_dns_entry(entries_out, entry_capacity, &count, name, AF_INET6, &sin6->sin6_addr);
        }
    }

    freeaddrinfo(results);
    *count_out = count;
    return count > 0U ? 0 : -1;
}

int platform_dns_query(
    const char *server,
    unsigned int port,
    const char *name,
    unsigned short record_type,
    PlatformDnsEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out
) {
    int family_filter;

    if (record_type == PLATFORM_DNS_RECORD_A) {
        family_filter = PLATFORM_NETWORK_FAMILY_IPV4;
    } else if (record_type == PLATFORM_DNS_RECORD_AAAA) {
        family_filter = PLATFORM_NETWORK_FAMILY_IPV6;
    } else {
        return -1;
    }
    return platform_dns_lookup(server, port, name, family_filter, entries_out, entry_capacity, count_out);
}

static int macos_network_family_matches(unsigned char family, int family_filter) {
    if (family_filter == PLATFORM_NETWORK_FAMILY_ANY) {
        return family == AF_INET || family == AF_INET6;
    }
    if (family_filter == PLATFORM_NETWORK_FAMILY_IPV4) {
        return family == AF_INET;
    }
    if (family_filter == PLATFORM_NETWORK_FAMILY_IPV6) {
        return family == AF_INET6;
    }
    return 0;
}

static int macos_network_family_code(unsigned char family) {
    if (family == AF_INET6) {
        return PLATFORM_NETWORK_FAMILY_IPV6;
    }
    if (family == AF_INET) {
        return PLATFORM_NETWORK_FAMILY_IPV4;
    }
    return PLATFORM_NETWORK_FAMILY_ANY;
}

static unsigned int macos_map_link_flags(unsigned int ifa_flags) {
    unsigned int flags = 0U;

    if ((ifa_flags & IFF_UP) != 0U) {
        flags |= PLATFORM_NETWORK_FLAG_UP;
    }
    if ((ifa_flags & IFF_BROADCAST) != 0U) {
        flags |= PLATFORM_NETWORK_FLAG_BROADCAST;
    }
    if ((ifa_flags & IFF_LOOPBACK) != 0U) {
        flags |= PLATFORM_NETWORK_FLAG_LOOPBACK;
    }
    if ((ifa_flags & IFF_RUNNING) != 0U) {
        flags |= PLATFORM_NETWORK_FLAG_RUNNING;
    }
    if ((ifa_flags & IFF_MULTICAST) != 0U) {
        flags |= PLATFORM_NETWORK_FLAG_MULTICAST;
    }
    return flags;
}

static int macos_find_link_index(PlatformNetworkLink *entries_out, size_t count, const char *name) {
    size_t index;

    for (index = 0U; index < count; ++index) {
        if (rt_strcmp(entries_out[index].name, name) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static void macos_format_mac_address(const unsigned char *bytes, size_t count, char *buffer, size_t buffer_size) {
    static const char hex[] = "0123456789abcdef";
    size_t index;
    size_t used = 0U;

    if (buffer_size == 0U) {
        return;
    }
    buffer[0] = '\0';
    for (index = 0U; index < count; ++index) {
        if (used + 4U > buffer_size) {
            break;
        }
        if (index != 0U) {
            buffer[used++] = ':';
        }
        buffer[used++] = hex[(bytes[index] >> 4) & 0x0fU];
        buffer[used++] = hex[bytes[index] & 0x0fU];
        buffer[used] = '\0';
    }
}

static int macos_format_address_text(const struct sockaddr *address, char *buffer, size_t buffer_size) {
    if (address == 0 || buffer == 0 || buffer_size == 0U) {
        return -1;
    }
    if (address->sa_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)address;
        return inet_ntop(AF_INET, &sin->sin_addr, buffer, buffer_size) == 0 ? -1 : 0;
    }
    if (address->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)address;
        return inet_ntop(AF_INET6, &sin6->sin6_addr, buffer, buffer_size) == 0 ? -1 : 0;
    }
    return -1;
}

static unsigned int macos_count_mask_bits_v4(const struct sockaddr *mask) {
    const struct sockaddr_in *sin;
    unsigned int value;
    unsigned int bits = 0U;

    if (mask == 0 || mask->sa_family != AF_INET) {
        return 0U;
    }
    sin = (const struct sockaddr_in *)mask;
    value = ntohl(sin->sin_addr.s_addr);
    while ((value & 0x80000000U) != 0U) {
        bits += 1U;
        value <<= 1U;
    }
    return bits;
}

static unsigned int macos_count_mask_bits_v6(const struct sockaddr *mask) {
    const struct sockaddr_in6 *sin6;
    unsigned int bits = 0U;
    size_t index;

    if (mask == 0 || mask->sa_family != AF_INET6) {
        return 0U;
    }
    sin6 = (const struct sockaddr_in6 *)mask;
    for (index = 0U; index < sizeof(sin6->sin6_addr.s6_addr); ++index) {
        unsigned char value = sin6->sin6_addr.s6_addr[index];
        while ((value & 0x80U) != 0U) {
            bits += 1U;
            value = (unsigned char)(value << 1U);
        }
        if (value != 0U) {
            break;
        }
    }
    return bits;
}

static const char *macos_address_scope_name(const char *ifname, const struct sockaddr *address) {
    (void)ifname;
    if (address != 0 && address->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)address;
        if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr)) {
            return "host";
        }
        if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
            return "link";
        }
    }
    if (address != 0 && address->sa_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)address;
        if ((ntohl(sin->sin_addr.s_addr) >> 24) == 127U) {
            return "host";
        }
    }
    return "global";
}

int platform_list_network_links(PlatformNetworkLink *entries_out, size_t entry_capacity, size_t *count_out) {
    struct ifaddrs *entries = 0;
    struct ifaddrs *current;
    int sock = -1;
    size_t count = 0U;

    if (entries_out == 0 || count_out == 0) {
        return -1;
    }

    *count_out = 0U;
    if (getifaddrs(&entries) != 0) {
        return -1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    for (current = entries; current != 0; current = current->ifa_next) {
        int index;

        if (current->ifa_name == 0 || current->ifa_name[0] == '\0') {
            continue;
        }

        index = macos_find_link_index(entries_out, count, current->ifa_name);
        if (index < 0) {
            struct ifreq ifr;

            if (count >= entry_capacity) {
                break;
            }

            index = (int)count;
            memset(&entries_out[index], 0, sizeof(entries_out[index]));
            rt_copy_string(entries_out[index].name, sizeof(entries_out[index].name), current->ifa_name);
            entries_out[index].index = if_nametoindex(current->ifa_name);
            entries_out[index].flags = macos_map_link_flags((unsigned int)current->ifa_flags);
            entries_out[index].mtu = 1500U;

            if (sock >= 0) {
                memset(&ifr, 0, sizeof(ifr));
                rt_copy_string(ifr.ifr_name, sizeof(ifr.ifr_name), current->ifa_name);
                if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
                    entries_out[index].flags = macos_map_link_flags((unsigned int)ifr.ifr_flags);
                }
                if (ioctl(sock, SIOCGIFMTU, &ifr) == 0 && ifr.ifr_mtu > 0) {
                    entries_out[index].mtu = (unsigned int)ifr.ifr_mtu;
                }
            }
            count += 1U;
        }

        if (current->ifa_addr != 0 && current->ifa_addr->sa_family == AF_LINK && !entries_out[index].has_mac) {
            const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)current->ifa_addr;
            if (sdl->sdl_alen > 0) {
                const unsigned char *addr = (const unsigned char *)(sdl->sdl_data + sdl->sdl_nlen);
                macos_format_mac_address(addr, (size_t)sdl->sdl_alen, entries_out[index].mac, sizeof(entries_out[index].mac));
                entries_out[index].has_mac = 1;
            }
        }
    }

    if (sock >= 0) {
        (void)platform_close(sock);
    }
    freeifaddrs(entries);
    *count_out = count;
    return 0;
}

int platform_list_network_addresses(
    PlatformNetworkAddress *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int family_filter,
    const char *ifname_filter
) {
    struct ifaddrs *entries = 0;
    struct ifaddrs *current;
    size_t count = 0U;

    if (entries_out == 0 || count_out == 0) {
        return -1;
    }

    *count_out = 0U;
    if (getifaddrs(&entries) != 0) {
        return -1;
    }

    for (current = entries; current != 0; current = current->ifa_next) {
        PlatformNetworkAddress *entry;

        if (current->ifa_name == 0 || current->ifa_addr == 0) {
            continue;
        }
        if (ifname_filter != 0 && rt_strcmp(current->ifa_name, ifname_filter) != 0) {
            continue;
        }
        if (!macos_network_family_matches((unsigned char)current->ifa_addr->sa_family, family_filter)) {
            continue;
        }
        if (count >= entry_capacity) {
            break;
        }

        entry = &entries_out[count];
        memset(entry, 0, sizeof(*entry));
        rt_copy_string(entry->ifname, sizeof(entry->ifname), current->ifa_name);
        entry->family = macos_network_family_code((unsigned char)current->ifa_addr->sa_family);
        if (macos_format_address_text(current->ifa_addr, entry->address, sizeof(entry->address)) != 0) {
            continue;
        }

        if (current->ifa_addr->sa_family == AF_INET) {
            entry->prefix_length = macos_count_mask_bits_v4(current->ifa_netmask);
            if (current->ifa_broadaddr != 0 && (current->ifa_flags & IFF_BROADCAST) != 0 &&
                macos_format_address_text(current->ifa_broadaddr, entry->broadcast, sizeof(entry->broadcast)) == 0) {
                entry->has_broadcast = 1;
            }
        } else {
            entry->prefix_length = macos_count_mask_bits_v6(current->ifa_netmask);
        }

        rt_copy_string(entry->scope, sizeof(entry->scope), macos_address_scope_name(current->ifa_name, current->ifa_addr));
        count += 1U;
    }

    freeifaddrs(entries);
    *count_out = count;
    return 0;
}

int platform_list_network_routes(
    PlatformRouteEntry *entries_out,
    size_t entry_capacity,
    size_t *count_out,
    int family_filter,
    const char *ifname_filter
) {
    (void)entries_out;
    (void)entry_capacity;
    (void)family_filter;
    (void)ifname_filter;
    if (count_out == 0) {
        return -1;
    }
    *count_out = 0U;
    return -1;
}

int platform_network_link_set(const char *ifname, int want_up, unsigned int mtu_value, int set_mtu) {
    (void)ifname;
    (void)want_up;
    (void)mtu_value;
    (void)set_mtu;
    return -1;
}

int platform_network_address_change(const char *ifname, const char *cidr, int add) {
    (void)ifname;
    (void)cidr;
    (void)add;
    return -1;
}

int platform_network_route_change(const char *destination, const char *gateway, const char *ifname, int add) {
    (void)destination;
    (void)gateway;
    (void)ifname;
    (void)add;
    return -1;
}

static int macos_memory_equal(const unsigned char *left, const unsigned char *right, size_t count) {
    size_t index;

    for (index = 0U; index < count; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

static int macos_ifname_is_valid(const char *ifname) {
    size_t index;

    if (ifname == 0 || ifname[0] == '\0') {
        return 0;
    }
    if (rt_strlen(ifname) >= IFNAMSIZ) {
        return 0;
    }
    for (index = 0U; ifname[index] != '\0'; ++index) {
        unsigned char ch = (unsigned char)ifname[index];
        if (ch <= ' ' || ch == '/' || ch == '\\') {
            return 0;
        }
    }
    return 1;
}

static int macos_select_mac_address(const char *ifname, unsigned char mac_out[6]) {
    struct ifaddrs *ifaddr = 0;
    struct ifaddrs *ifa;

    if (mac_out == 0 || (ifname != 0 && ifname[0] != '\0' && !macos_ifname_is_valid(ifname))) {
        return -1;
    }
    if (getifaddrs(&ifaddr) != 0) {
        return -1;
    }

    for (ifa = ifaddr; ifa != 0; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == 0) {
            continue;
        }
        if (ifname != 0 && ifname[0] != '\0' && rt_strcmp(ifname, ifa->ifa_name) != 0) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_LINK) {
            const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)ifa->ifa_addr;
            const unsigned char *addr = (const unsigned char *)(sdl->sdl_data + sdl->sdl_nlen);
            if ((ifa->ifa_flags & IFF_LOOPBACK) != 0 || sdl->sdl_alen < 6) {
                continue;
            }
            memcpy(mac_out, addr, 6U);
            freeifaddrs(ifaddr);
            return 0;
        }
    }

    freeifaddrs(ifaddr);
    return -1;
}

static void macos_store_be16(unsigned char *buffer, unsigned short value) {
    buffer[0] = (unsigned char)(value >> 8);
    buffer[1] = (unsigned char)(value & 0xffU);
}

static void macos_store_be32(unsigned char *buffer, unsigned int value) {
    buffer[0] = (unsigned char)(value >> 24);
    buffer[1] = (unsigned char)((value >> 16) & 0xffU);
    buffer[2] = (unsigned char)((value >> 8) & 0xffU);
    buffer[3] = (unsigned char)(value & 0xffU);
}

static unsigned int macos_load_be32(const unsigned char *buffer) {
    return ((unsigned int)buffer[0] << 24) |
           ((unsigned int)buffer[1] << 16) |
           ((unsigned int)buffer[2] << 8) |
           (unsigned int)buffer[3];
}

static int macos_build_dhcp_packet(
    unsigned char *packet,
    size_t packet_size,
    unsigned int xid,
    const unsigned char mac[6],
    unsigned char message_type,
    const unsigned char *requested_ip,
    const unsigned char *server_id
) {
    size_t offset = 240U;
    static const unsigned char param_request[] = { 1U, 3U, 6U, 15U, 51U, 54U };

    if (packet == 0 || packet_size < 300U || mac == 0) {
        return -1;
    }
    memset(packet, 0, packet_size);
    packet[0] = 1U;
    packet[1] = 1U;
    packet[2] = 6U;
    macos_store_be32(packet + 4, xid);
    macos_store_be16(packet + 10, 0x8000U);
    memcpy(packet + 28, mac, 6U);
    macos_store_be32(packet + 236, 0x63825363U);

    packet[offset++] = 53U;
    packet[offset++] = 1U;
    packet[offset++] = message_type;

    packet[offset++] = 61U;
    packet[offset++] = 7U;
    packet[offset++] = 1U;
    memcpy(packet + offset, mac, 6U);
    offset += 6U;

    if (requested_ip != 0) {
        packet[offset++] = 50U;
        packet[offset++] = 4U;
        memcpy(packet + offset, requested_ip, 4U);
        offset += 4U;
    }
    if (server_id != 0) {
        packet[offset++] = 54U;
        packet[offset++] = 4U;
        memcpy(packet + offset, server_id, 4U);
        offset += 4U;
    }

    packet[offset++] = 55U;
    packet[offset++] = (unsigned char)sizeof(param_request);
    memcpy(packet + offset, param_request, sizeof(param_request));
    offset += sizeof(param_request);
    packet[offset++] = 255U;
    return (int)offset;
}

static int macos_mask_to_prefix(const unsigned char *mask) {
    int prefix = 0;
    int index;
    int bit;

    for (index = 0; index < 4; ++index) {
        for (bit = 7; bit >= 0; --bit) {
            if ((mask[index] & (1U << bit)) != 0U) {
                prefix += 1;
            } else {
                return prefix;
            }
        }
    }
    return prefix;
}

static int macos_parse_dhcp_reply(
    const unsigned char *packet,
    size_t packet_length,
    unsigned int xid,
    const unsigned char mac[6],
    unsigned char expected_message_type,
    PlatformDhcpLease *lease_out
) {
    size_t offset = 240U;
    unsigned char message_type = 0U;
    struct in_addr addr;

    if (packet == 0 || lease_out == 0 || packet_length < 240U || mac == 0) {
        return -1;
    }
    if (packet[0] != 2U || macos_load_be32(packet + 4) != xid ||
        !macos_memory_equal(packet + 28, mac, 6U) || macos_load_be32(packet + 236) != 0x63825363U) {
        return -1;
    }

    memcpy(&addr, packet + 16, 4U);
    if (inet_ntop(AF_INET, &addr, lease_out->address, sizeof(lease_out->address)) == 0) {
        return -1;
    }

    while (offset < packet_length) {
        unsigned char option = packet[offset++];
        unsigned char length;

        if (option == 0U) {
            continue;
        }
        if (option == 255U) {
            break;
        }
        if (offset >= packet_length) {
            break;
        }
        length = packet[offset++];
        if (offset + length > packet_length) {
            break;
        }

        if (option == 53U && length >= 1U) {
            message_type = packet[offset];
        } else if (option == 1U && length == 4U) {
            lease_out->prefix_length = (unsigned int)macos_mask_to_prefix(packet + offset);
        } else if (option == 3U && length >= 4U) {
            memcpy(&addr, packet + offset, 4U);
            (void)inet_ntop(AF_INET, &addr, lease_out->router, sizeof(lease_out->router));
        } else if (option == 6U && length >= 4U) {
            memcpy(&addr, packet + offset, 4U);
            (void)inet_ntop(AF_INET, &addr, lease_out->dns1, sizeof(lease_out->dns1));
            if (length >= 8U) {
                memcpy(&addr, packet + offset + 4U, 4U);
                (void)inet_ntop(AF_INET, &addr, lease_out->dns2, sizeof(lease_out->dns2));
            }
        } else if (option == 51U && length == 4U) {
            lease_out->lease_seconds = macos_load_be32(packet + offset);
        } else if (option == 54U && length == 4U) {
            memcpy(&addr, packet + offset, 4U);
            (void)inet_ntop(AF_INET, &addr, lease_out->server, sizeof(lease_out->server));
        }

        offset += length;
    }

    if (message_type != expected_message_type) {
        return -1;
    }
    if (lease_out->prefix_length == 0U) {
        lease_out->prefix_length = 24U;
    }
    return 0;
}

int platform_dhcp_request(
    const char *ifname,
    const char *server,
    unsigned int server_port,
    unsigned int client_port,
    unsigned int timeout_milliseconds,
    PlatformDhcpLease *lease_out
) {
    unsigned char mac[6];
    unsigned char packet[512];
    unsigned char reply[512];
    struct sockaddr_in server_addr;
    struct sockaddr_in peer_addr;
    struct sockaddr_in bind_addr;
    socklen_t peer_len = sizeof(peer_addr);
    int sock;
    int packet_length;
    unsigned int xid;
    int broadcast = 1;
    struct timeval timeout;
    unsigned char requested_ip[4];
    unsigned char server_id[4];
    int have_server_id = 0;

    if (lease_out == 0 || macos_select_mac_address(ifname, mac) != 0) {
        return -1;
    }
    memset(lease_out, 0, sizeof(*lease_out));

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return -1;
    }

    timeout.tv_sec = (time_t)((timeout_milliseconds == 0U ? 3000U : timeout_milliseconds) / 1000U);
    timeout.tv_usec = (int)(((timeout_milliseconds == 0U ? 3000U : timeout_milliseconds) % 1000U) * 1000U);
    if (timeout.tv_sec == 0 && timeout.tv_usec == 0) {
        timeout.tv_usec = 1000;
    }
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &broadcast, sizeof(broadcast));
    (void)setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons((unsigned short)(client_port == 0U ? 68U : client_port));
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        (void)platform_close(sock);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)(server_port == 0U ? 67U : server_port));
    if (server == 0 || server[0] == '\0') {
        server_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    } else if (inet_pton(AF_INET, server, &server_addr.sin_addr) != 1) {
        (void)platform_close(sock);
        return -1;
    }

    xid = ((unsigned int)platform_get_process_id() & 0xffffU) ^ 0x44480000U;
    packet_length = macos_build_dhcp_packet(packet, sizeof(packet), xid, mac, 1U, 0, 0);
    if (packet_length < 0 || sendto(sock, packet, (size_t)packet_length, 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        (void)platform_close(sock);
        return -1;
    }

    {
        ssize_t offer_bytes = recvfrom(sock, reply, sizeof(reply), 0, (struct sockaddr *)&peer_addr, &peer_len);
        if (offer_bytes <= 0 || macos_parse_dhcp_reply(reply, (size_t)offer_bytes, xid, mac, 2U, lease_out) != 0) {
            (void)platform_close(sock);
            return -1;
        }
    }

    memcpy(requested_ip, reply + 16, 4U);
    if (lease_out->server[0] != '\0' && inet_pton(AF_INET, lease_out->server, server_id) == 1) {
        have_server_id = 1;
    }

    packet_length = macos_build_dhcp_packet(packet, sizeof(packet), xid, mac, 3U, requested_ip, have_server_id ? server_id : 0);
    if (packet_length < 0 || sendto(sock, packet, (size_t)packet_length, 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
        (void)platform_close(sock);
        return -1;
    }
    {
        ssize_t ack_bytes = recvfrom(sock, reply, sizeof(reply), 0, (struct sockaddr *)&peer_addr, &peer_len);
        if (ack_bytes <= 0 || macos_parse_dhcp_reply(reply, (size_t)ack_bytes, xid, mac, 5U, lease_out) != 0) {
            (void)platform_close(sock);
            return -1;
        }
    }

    (void)platform_close(sock);
    return 0;
}

int platform_list_sockets(PlatformSocketEntry *entries_out, size_t entry_capacity, size_t *count_out, int include_tcp, int include_udp, int listening_only) {
    (void)entries_out;
    (void)entry_capacity;
    (void)include_tcp;
    (void)include_udp;
    (void)listening_only;
    if (count_out != 0) *count_out = 0U;
    return -1;
}

static unsigned short macos_icmp_checksum(const void *data, size_t length) {
    const unsigned short *words = (const unsigned short *)data;
    unsigned int sum = 0U;
    size_t remaining = length;

    while (remaining > 1U) {
        sum += *words++;
        remaining -= 2U;
    }
    if (remaining == 1U) {
        sum += *(const unsigned char *)words;
    }
    while ((sum >> 16) != 0U) {
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return (unsigned short)~sum;
}

static int macos_resolve_ping_host(const char *host, struct sockaddr_in *addr_out, char *ip_out, size_t ip_out_size) {
    struct addrinfo hints;
    struct addrinfo *results = 0;
    int result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    result = getaddrinfo(host, 0, &hints, &results);
    if (result != 0 || results == 0) {
        return -1;
    }
    memcpy(addr_out, results->ai_addr, sizeof(*addr_out));
    if (inet_ntop(AF_INET, &addr_out->sin_addr, ip_out, ip_out_size) == 0) {
        freeaddrinfo(results);
        return -1;
    }
    freeaddrinfo(results);
    return 0;
}

static int macos_resolve_ping_host6(const char *host, struct sockaddr_in6 *addr_out, char *ip_out, size_t ip_out_size) {
    struct addrinfo hints;
    struct addrinfo *results = 0;
    int result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    result = getaddrinfo(host, 0, &hints, &results);
    if (result != 0 || results == 0) {
        return -1;
    }
    memcpy(addr_out, results->ai_addr, sizeof(*addr_out));
    if (inet_ntop(AF_INET6, &addr_out->sin6_addr, ip_out, ip_out_size) == 0) {
        freeaddrinfo(results);
        return -1;
    }
    freeaddrinfo(results);
    return 0;
}

static double macos_elapsed_milliseconds(const struct timeval *start, const struct timeval *end) {
    long seconds = (long)(end->tv_sec - start->tv_sec);
    long usec = (long)(end->tv_usec - start->tv_usec);
    return (double)(seconds * 1000L) + (double)usec / 1000.0;
}

static void macos_write_milliseconds(double milliseconds) {
    unsigned long long whole_ms = (unsigned long long)milliseconds;
    unsigned long long frac_ms = (unsigned long long)((milliseconds - (double)whole_ms) * 1000.0);

    rt_write_uint(1, whole_ms);
    rt_write_char(1, '.');
    if (frac_ms < 100ULL) {
        rt_write_char(1, '0');
    }
    if (frac_ms < 10ULL) {
        rt_write_char(1, '0');
    }
    rt_write_uint(1, frac_ms);
}

static void macos_ping_error(const char *message, const char *detail) {
    rt_write_cstr(2, "ping: ");
    rt_write_cstr(2, message);
    if (detail != 0) {
        rt_write_cstr(2, detail);
    }
    rt_write_char(2, '\n');
}

static void macos_ping_summary(const char *host, unsigned int transmitted, unsigned int received_count, double min_ms, double max_ms, double total_ms) {
    rt_write_cstr(1, "--- ");
    rt_write_cstr(1, host);
    rt_write_line(1, " ping statistics ---");
    rt_write_uint(1, transmitted);
    rt_write_cstr(1, " packets transmitted, ");
    rt_write_uint(1, received_count);
    rt_write_cstr(1, " packets received, ");
    if (transmitted > 0U) {
        unsigned long long loss = ((unsigned long long)(transmitted - received_count) * 100ULL) / (unsigned long long)transmitted;
        rt_write_uint(1, loss);
    } else {
        rt_write_uint(1, 0ULL);
    }
    rt_write_line(1, "% packet loss");
    if (received_count > 0U) {
        rt_write_cstr(1, "round-trip min/avg/max = ");
        macos_write_milliseconds(min_ms);
        rt_write_char(1, '/');
        macos_write_milliseconds(total_ms / (double)received_count);
        rt_write_char(1, '/');
        macos_write_milliseconds(max_ms);
        rt_write_line(1, " ms");
    }
}

static int macos_contains_char(const char *text, char ch) {
    size_t index = 0U;

    while (text != 0 && text[index] != '\0') {
        if (text[index] == ch) {
            return 1;
        }
        index += 1U;
    }
    return 0;
}

int platform_ping_host(const char *host, const PlatformPingOptions *options) {
    PlatformPingOptions effective_options;
    struct timeval timeout;
    unsigned int seq;
    unsigned int transmitted = 0U;
    unsigned int received_count = 0U;
    unsigned short identifier;
    int process_id = platform_get_process_id();
    double min_ms = 0.0;
    double max_ms = 0.0;
    double total_ms = 0.0;
    struct timeval overall_start;
    int deadline_exceeded = 0;

    if (options == 0) {
        effective_options.count = PLATFORM_PING_DEFAULT_COUNT;
        effective_options.interval_seconds = PLATFORM_PING_DEFAULT_INTERVAL_SECONDS;
        effective_options.timeout_seconds = PLATFORM_PING_DEFAULT_TIMEOUT_SECONDS;
        effective_options.payload_size = PLATFORM_PING_DEFAULT_PAYLOAD_SIZE;
        effective_options.ttl = 0U;
        effective_options.deadline_seconds = 0U;
        effective_options.quiet_output = 0;
        effective_options.family = PLATFORM_NETWORK_FAMILY_ANY;
        effective_options.numeric_only = 0;
        options = &effective_options;
    }

    if (host == 0 || options->count == 0U || options->timeout_seconds == 0U ||
        options->payload_size > PLATFORM_PING_MAX_PAYLOAD_SIZE || options->ttl > PLATFORM_PING_MAX_TTL) {
        return 1;
    }

    if (options->family == PLATFORM_NETWORK_FAMILY_IPV6 ||
        (options->family != PLATFORM_NETWORK_FAMILY_IPV4 && macos_contains_char(host, ':'))) {
        struct sockaddr_in6 addr6;
        char ip_text6[INET6_ADDRSTRLEN];
        int sock;

        if (macos_resolve_ping_host6(host, &addr6, ip_text6, sizeof(ip_text6)) != 0) {
            macos_ping_error("unknown host ", host);
            return 1;
        }

        sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6);
        if (sock < 0) {
            sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
        }
        if (sock < 0) {
            macos_ping_error("unable to create ICMPv6 socket", 0);
            return 1;
        }

        timeout.tv_sec = (time_t)options->timeout_seconds;
        timeout.tv_usec = 0;
        (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (options->ttl > 0U) {
            int hops_value = (int)options->ttl;
            (void)setsockopt(sock, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hops_value, sizeof(hops_value));
        }
        if (connect(sock, (struct sockaddr *)&addr6, sizeof(addr6)) != 0) {
            (void)platform_close(sock);
            macos_ping_error("cannot reach ", host);
            return 1;
        }

        identifier = (unsigned short)((process_id > 0) ? process_id : 0x1234);
        (void)gettimeofday(&overall_start, 0);
        rt_write_cstr(1, "PING ");
        rt_write_cstr(1, host);
        rt_write_cstr(1, " (");
        rt_write_cstr(1, ip_text6);
        rt_write_cstr(1, ") ");
        rt_write_uint(1, options->payload_size);
        rt_write_line(1, " data bytes");

        for (seq = 1U; seq <= options->count; ++seq) {
            unsigned char packet[sizeof(MacosIcmpv6Packet) + PLATFORM_PING_MAX_PAYLOAD_SIZE];
            unsigned char reply[sizeof(packet) + 128U];
            MacosIcmpv6Packet *header = (MacosIcmpv6Packet *)packet;
            struct timeval start_time;
            struct timeval end_time;
            struct timeval current_time;
            ssize_t reply_size;
            size_t packet_size = sizeof(MacosIcmpv6Packet) + options->payload_size;
            int matched = 0;

            if (options->deadline_seconds > 0U) {
                (void)gettimeofday(&current_time, 0);
                if (macos_elapsed_milliseconds(&overall_start, &current_time) >= (double)options->deadline_seconds * 1000.0) {
                    deadline_exceeded = 1;
                    break;
                }
            }

            memset(packet, 0, packet_size);
            header->type = MACOS_ICMPV6_ECHO_REQUEST;
            header->identifier = htons(identifier);
            header->sequence = htons((unsigned short)seq);
            memset(packet + sizeof(MacosIcmpv6Packet), 0x42, options->payload_size);

            (void)gettimeofday(&start_time, 0);
            if (send(sock, packet, packet_size, 0) < 0) {
                (void)platform_close(sock);
                macos_ping_error("send failed to ", host);
                return 1;
            }
            transmitted += 1U;

            for (;;) {
                reply_size = recv(sock, reply, sizeof(reply), 0);
                if (reply_size < 0) {
                    break;
                }
                {
                    size_t offset = 0U;
                    const MacosIcmpv6Packet *reply_header;
                    if ((size_t)reply_size >= 40U && (reply[0] >> 4) == 6U) {
                        offset = 40U;
                    }
                    if ((size_t)reply_size < offset + sizeof(MacosIcmpv6Packet)) {
                        continue;
                    }
                    reply_header = (const MacosIcmpv6Packet *)(reply + offset);
                    if (reply_header->type == MACOS_ICMPV6_ECHO_REPLY &&
                        ntohs(reply_header->identifier) == identifier &&
                        ntohs(reply_header->sequence) == (unsigned short)seq) {
                        double rtt_ms;
                        (void)gettimeofday(&end_time, 0);
                        rtt_ms = macos_elapsed_milliseconds(&start_time, &end_time);
                        if (received_count == 0U || rtt_ms < min_ms) {
                            min_ms = rtt_ms;
                        }
                        if (received_count == 0U || rtt_ms > max_ms) {
                            max_ms = rtt_ms;
                        }
                        total_ms += rtt_ms;
                        received_count += 1U;
                        if (!options->quiet_output) {
                            rt_write_uint(1, (unsigned long long)((reply_size > (ssize_t)offset) ? (reply_size - (ssize_t)offset) : reply_size));
                            rt_write_cstr(1, " bytes from ");
                            rt_write_cstr(1, ip_text6);
                            rt_write_cstr(1, ": icmp_seq=");
                            rt_write_uint(1, seq);
                            rt_write_cstr(1, " time=");
                            macos_write_milliseconds(rtt_ms);
                            rt_write_line(1, " ms");
                        }
                        matched = 1;
                        break;
                    }
                }
            }
            if (!matched && !options->quiet_output) {
                rt_write_cstr(1, "Request timeout for icmp_seq ");
                rt_write_uint(1, seq);
                rt_write_char(1, '\n');
            }
            if (seq < options->count && options->interval_seconds > 0U) {
                if (options->deadline_seconds > 0U) {
                    (void)gettimeofday(&current_time, 0);
                    if (macos_elapsed_milliseconds(&overall_start, &current_time) >= (double)options->deadline_seconds * 1000.0) {
                        deadline_exceeded = 1;
                        break;
                    }
                }
                (void)platform_sleep_seconds(options->interval_seconds);
            }
        }
        (void)platform_close(sock);
        if (deadline_exceeded && !options->quiet_output) {
            rt_write_line(1, "ping: deadline reached");
        }
        macos_ping_summary(host, transmitted, received_count, min_ms, max_ms, total_ms);
        return received_count > 0U ? 0 : 1;
    }

    {
        struct sockaddr_in addr;
        char ip_text[INET_ADDRSTRLEN];
        int sock;

        if (macos_resolve_ping_host(host, &addr, ip_text, sizeof(ip_text)) != 0) {
            macos_ping_error("unknown host ", host);
            return 1;
        }
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        if (sock < 0) {
            sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        }
        if (sock < 0) {
            macos_ping_error("unable to create ICMP socket", 0);
            return 1;
        }

        timeout.tv_sec = (time_t)options->timeout_seconds;
        timeout.tv_usec = 0;
        (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (options->ttl > 0U) {
            int ttl_value = (int)options->ttl;
            (void)setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl_value, sizeof(ttl_value));
        }

        identifier = (unsigned short)((process_id > 0) ? process_id : 0x1234);
        (void)gettimeofday(&overall_start, 0);
        rt_write_cstr(1, "PING ");
        rt_write_cstr(1, host);
        rt_write_cstr(1, " (");
        rt_write_cstr(1, ip_text);
        rt_write_cstr(1, ") ");
        rt_write_uint(1, options->payload_size);
        rt_write_line(1, " data bytes");

        for (seq = 1U; seq <= options->count; ++seq) {
            unsigned char packet[sizeof(MacosIcmpPacket) + PLATFORM_PING_MAX_PAYLOAD_SIZE];
            unsigned char reply[sizeof(packet) + 128U];
            MacosIcmpPacket *header = (MacosIcmpPacket *)packet;
            struct timeval start_time;
            struct timeval end_time;
            struct timeval current_time;
            ssize_t reply_size;
            size_t packet_size = sizeof(MacosIcmpPacket) + options->payload_size;
            int matched = 0;

            if (options->deadline_seconds > 0U) {
                (void)gettimeofday(&current_time, 0);
                if (macos_elapsed_milliseconds(&overall_start, &current_time) >= (double)options->deadline_seconds * 1000.0) {
                    deadline_exceeded = 1;
                    break;
                }
            }

            memset(packet, 0, packet_size);
            header->type = MACOS_ICMP_ECHO;
            header->identifier = htons(identifier);
            header->sequence = htons((unsigned short)seq);
            memset(packet + sizeof(MacosIcmpPacket), 0x42, options->payload_size);
            header->checksum = macos_icmp_checksum(packet, packet_size);

            (void)gettimeofday(&start_time, 0);
            if (sendto(sock, packet, packet_size, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                (void)platform_close(sock);
                macos_ping_error("send failed to ", host);
                return 1;
            }
            transmitted += 1U;

            for (;;) {
                reply_size = recvfrom(sock, reply, sizeof(reply), 0, 0, 0);
                if (reply_size < 0) {
                    break;
                }
                {
                    size_t offset = 0U;
                    const MacosIcmpPacket *reply_header;
                    if ((size_t)reply_size >= 20U && (reply[0] >> 4) == 4U) {
                        offset = (size_t)(reply[0] & 0x0fU) * 4U;
                    }
                    if ((size_t)reply_size < offset + sizeof(MacosIcmpPacket)) {
                        continue;
                    }
                    reply_header = (const MacosIcmpPacket *)(reply + offset);
                    if (reply_header->type == MACOS_ICMP_ECHO_REPLY &&
                        ntohs(reply_header->identifier) == identifier &&
                        ntohs(reply_header->sequence) == (unsigned short)seq) {
                        double rtt_ms;
                        unsigned int ttl = (offset >= 20U && (size_t)reply_size >= 9U) ? reply[8] : 0U;
                        (void)gettimeofday(&end_time, 0);
                        rtt_ms = macos_elapsed_milliseconds(&start_time, &end_time);
                        if (received_count == 0U || rtt_ms < min_ms) {
                            min_ms = rtt_ms;
                        }
                        if (received_count == 0U || rtt_ms > max_ms) {
                            max_ms = rtt_ms;
                        }
                        total_ms += rtt_ms;
                        received_count += 1U;
                        if (!options->quiet_output) {
                            rt_write_uint(1, (unsigned long long)((reply_size > (ssize_t)offset) ? (reply_size - (ssize_t)offset) : reply_size));
                            rt_write_cstr(1, " bytes from ");
                            rt_write_cstr(1, ip_text);
                            rt_write_cstr(1, ": icmp_seq=");
                            rt_write_uint(1, seq);
                            if (ttl != 0U) {
                                rt_write_cstr(1, " ttl=");
                                rt_write_uint(1, (unsigned long long)ttl);
                            }
                            rt_write_cstr(1, " time=");
                            macos_write_milliseconds(rtt_ms);
                            rt_write_line(1, " ms");
                        }
                        matched = 1;
                        break;
                    }
                }
            }
            if (!matched && !options->quiet_output) {
                rt_write_cstr(1, "Request timeout for icmp_seq ");
                rt_write_uint(1, seq);
                rt_write_char(1, '\n');
            }
            if (seq < options->count && options->interval_seconds > 0U) {
                if (options->deadline_seconds > 0U) {
                    (void)gettimeofday(&current_time, 0);
                    if (macos_elapsed_milliseconds(&overall_start, &current_time) >= (double)options->deadline_seconds * 1000.0) {
                        deadline_exceeded = 1;
                        break;
                    }
                }
                (void)platform_sleep_seconds(options->interval_seconds);
            }
        }
        (void)platform_close(sock);
        if (deadline_exceeded && !options->quiet_output) {
            rt_write_line(1, "ping: deadline reached");
        }
        macos_ping_summary(host, transmitted, received_count, min_ms, max_ms, total_ms);
        return received_count > 0U ? 0 : 1;
    }
}

static int macos_trace_match_inner_ipv4(
    const unsigned char *reply,
    size_t reply_size,
    size_t outer_offset,
    unsigned short identifier,
    unsigned short sequence,
    int allow_identifier_rewrite
) {
    size_t inner_ip_offset = outer_offset + sizeof(MacosIcmpPacket);
    size_t inner_ip_header_length;
    const MacosIcmpPacket *inner_icmp;

    if (reply_size < inner_ip_offset + 20U || (reply[inner_ip_offset] >> 4) != 4U) {
        return 0;
    }
    inner_ip_header_length = (size_t)(reply[inner_ip_offset] & 0x0fU) * 4U;
    if (inner_ip_header_length < 20U || reply_size < inner_ip_offset + inner_ip_header_length + sizeof(MacosIcmpPacket)) {
        return 0;
    }
    inner_icmp = (const MacosIcmpPacket *)(reply + inner_ip_offset + inner_ip_header_length);
    return inner_icmp->type == MACOS_ICMP_ECHO &&
           ntohs(inner_icmp->sequence) == sequence &&
           (allow_identifier_rewrite || ntohs(inner_icmp->identifier) == identifier);
}

static int macos_trace_match_inner_ipv6(
    const unsigned char *reply,
    size_t reply_size,
    size_t outer_offset,
    unsigned short identifier,
    unsigned short sequence,
    int allow_identifier_rewrite
) {
    size_t inner_ip_offset = outer_offset + sizeof(MacosIcmpv6Packet);
    const MacosIcmpv6Packet *inner_icmp;

    if (reply_size < inner_ip_offset + 40U || (reply[inner_ip_offset] >> 4) != 6U) {
        return 0;
    }
    if (reply_size < inner_ip_offset + 40U + sizeof(MacosIcmpv6Packet)) {
        return 0;
    }
    inner_icmp = (const MacosIcmpv6Packet *)(reply + inner_ip_offset + 40U);
    return inner_icmp->type == MACOS_ICMPV6_ECHO_REQUEST &&
           ntohs(inner_icmp->sequence) == sequence &&
           (allow_identifier_rewrite || ntohs(inner_icmp->identifier) == identifier);
}

static void macos_trace_lookup_name(const struct sockaddr *peer, socklen_t peer_len, char *name_out, size_t name_size) {
    if (name_out == 0 || name_size == 0U) {
        return;
    }
    name_out[0] = '\0';
    if (peer == 0) {
        return;
    }
    if (getnameinfo(peer, peer_len, name_out, (socklen_t)name_size, 0, 0, NI_NAMEREQD) != 0) {
        name_out[0] = '\0';
    }
}

int platform_trace_route(
    const char *host,
    const PlatformTracerouteOptions *options,
    PlatformTracerouteHop *hops_out,
    size_t hop_capacity,
    size_t *hop_count_out
) {
    PlatformTracerouteOptions defaults;
    struct sockaddr_in addr;
    struct timeval timeout;
    unsigned int max_ttl;
    unsigned int queries;
    unsigned int timeout_seconds;
    unsigned int payload_size;
    unsigned short identifier;
    unsigned int ttl;
    int sock;
    int allow_identifier_rewrite = 0;
    size_t hop_count = 0U;

    if (hop_count_out != 0) {
        *hop_count_out = 0U;
    }
    if (host == 0 || hops_out == 0 || hop_count_out == 0 || hop_capacity == 0U) {
        return -1;
    }
    if (options == 0) {
        defaults.max_ttl = 30U;
        defaults.queries = 3U;
        defaults.timeout_seconds = PLATFORM_PING_DEFAULT_TIMEOUT_SECONDS;
        defaults.payload_size = PLATFORM_PING_DEFAULT_PAYLOAD_SIZE;
        defaults.family = PLATFORM_NETWORK_FAMILY_IPV4;
        defaults.numeric_only = 1;
        defaults.hop_callback = 0;
        defaults.hop_callback_user_data = 0;
        options = &defaults;
    }
    max_ttl = options->max_ttl == 0U ? 30U : options->max_ttl;
    queries = options->queries == 0U ? 1U : options->queries;
    timeout_seconds = options->timeout_seconds == 0U ? PLATFORM_PING_DEFAULT_TIMEOUT_SECONDS : options->timeout_seconds;
    payload_size = options->payload_size > PLATFORM_PING_MAX_PAYLOAD_SIZE ? PLATFORM_PING_MAX_PAYLOAD_SIZE : options->payload_size;
    if (max_ttl > PLATFORM_PING_MAX_TTL || queries > PLATFORM_TRACEROUTE_MAX_QUERIES) {
        return -1;
    }

    if (options->family == PLATFORM_NETWORK_FAMILY_IPV6 ||
        (options->family != PLATFORM_NETWORK_FAMILY_IPV4 && macos_contains_char(host, ':'))) {
        struct sockaddr_in6 addr6;
        int allow_identifier_rewrite6 = 1;

        if (macos_resolve_ping_host6(host, &addr6, hops_out[0].address, sizeof(hops_out[0].address)) != 0) {
            return -1;
        }
        sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6);
        if (sock < 0) {
            sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
            allow_identifier_rewrite6 = 0;
        }
        if (sock < 0) {
            return -1;
        }
        timeout.tv_sec = (time_t)timeout_seconds;
        timeout.tv_usec = 0;
        (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        identifier = (unsigned short)((platform_get_process_id() > 0) ? platform_get_process_id() : 0x1234);

        for (ttl = 1U; ttl <= max_ttl && hop_count < hop_capacity; ++ttl) {
            PlatformTracerouteHop *hop = hops_out + hop_count;
            unsigned int probe;
            int hops_value = (int)ttl;

            rt_memset(hop, 0, sizeof(*hop));
            hop->ttl = ttl;
            hop->probe_count = queries;
            (void)setsockopt(sock, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hops_value, sizeof(hops_value));

            for (probe = 0U; probe < queries; ++probe) {
                unsigned char packet[sizeof(MacosIcmpv6Packet) + PLATFORM_PING_MAX_PAYLOAD_SIZE];
                unsigned char reply[768];
                MacosIcmpv6Packet *header = (MacosIcmpv6Packet *)packet;
                size_t packet_size = sizeof(MacosIcmpv6Packet) + payload_size;
                unsigned short sequence = (unsigned short)(ttl * 32U + probe);
                struct timeval start_time;
                struct timeval end_time;

                memset(packet, 0, packet_size);
                header->type = MACOS_ICMPV6_ECHO_REQUEST;
                header->code = 0;
                header->identifier = htons(identifier);
                header->sequence = htons(sequence);
                memset(packet + sizeof(MacosIcmpv6Packet), 0x42, payload_size);
                header->checksum = 0;

                (void)gettimeofday(&start_time, 0);
                if (sendto(sock, packet, packet_size, 0, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
                    continue;
                }

                for (;;) {
                    struct sockaddr_storage peer;
                    socklen_t peer_len = sizeof(peer);
                    ssize_t reply_size = recvfrom(sock, reply, sizeof(reply), 0, (struct sockaddr *)&peer, &peer_len);
                    size_t offset = 0U;
                    const MacosIcmpv6Packet *reply_header;
                    int matched = 0;

                    if (reply_size < 0) {
                        break;
                    }
                    if ((size_t)reply_size >= 40U && (reply[0] >> 4) == 6U) {
                        offset = 40U;
                    }
                    if ((size_t)reply_size < offset + sizeof(MacosIcmpv6Packet)) {
                        continue;
                    }
                    reply_header = (const MacosIcmpv6Packet *)(reply + offset);
                    if (reply_header->type == MACOS_ICMPV6_ECHO_REPLY &&
                        ntohs(reply_header->sequence) == sequence &&
                        (allow_identifier_rewrite6 || ntohs(reply_header->identifier) == identifier)) {
                        matched = 1;
                        hop->reached_destination = 1;
                    } else if (reply_header->type == MACOS_ICMPV6_TIME_EXCEEDED &&
                               macos_trace_match_inner_ipv6(reply, (size_t)reply_size, offset, identifier, sequence, allow_identifier_rewrite6)) {
                        matched = 1;
                    }
                    if (!matched) {
                        continue;
                    }

                    (void)gettimeofday(&end_time, 0);
                    hop->probe_replied[probe] = 1U;
                    hop->rtt_milliseconds[probe] = (unsigned int)(macos_elapsed_milliseconds(&start_time, &end_time) + 0.5);
                    hop->reply_count += 1U;
                    if (peer.ss_family == AF_INET6) {
                        const struct sockaddr_in6 *peer6 = (const struct sockaddr_in6 *)&peer;
                        (void)inet_ntop(AF_INET6, &peer6->sin6_addr, hop->address, sizeof(hop->address));
                        if (!options->numeric_only) {
                            macos_trace_lookup_name((const struct sockaddr *)&peer, peer_len, hop->hostname, sizeof(hop->hostname));
                        }
                    }
                    break;
                }
            }

            if (options->hop_callback != 0) {
                options->hop_callback(hop, options->hop_callback_user_data);
            }
            hop_count += 1U;
            if (hop->reached_destination) {
                break;
            }
        }

        (void)platform_close(sock);
        *hop_count_out = hop_count;
        return 0;
    }

    if (macos_resolve_ping_host(host, &addr, hops_out[0].address, sizeof(hops_out[0].address)) != 0) {
        return -1;
    }

    sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        allow_identifier_rewrite = 1;
    }
    if (sock < 0) {
        return -1;
    }
    timeout.tv_sec = (time_t)timeout_seconds;
    timeout.tv_usec = 0;
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    identifier = (unsigned short)((platform_get_process_id() > 0) ? platform_get_process_id() : 0x1234);

    for (ttl = 1U; ttl <= max_ttl && hop_count < hop_capacity; ++ttl) {
        PlatformTracerouteHop *hop = hops_out + hop_count;
        unsigned int probe;
        int ttl_value = (int)ttl;

        rt_memset(hop, 0, sizeof(*hop));
        hop->ttl = ttl;
        hop->probe_count = queries;
        (void)setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl_value, sizeof(ttl_value));

        for (probe = 0U; probe < queries; ++probe) {
            unsigned char packet[sizeof(MacosIcmpPacket) + PLATFORM_PING_MAX_PAYLOAD_SIZE];
            unsigned char reply[512];
            MacosIcmpPacket *header = (MacosIcmpPacket *)packet;
            size_t packet_size = sizeof(MacosIcmpPacket) + payload_size;
            unsigned short sequence = (unsigned short)(ttl * 32U + probe);
            struct timeval start_time;
            struct timeval end_time;

            memset(packet, 0, packet_size);
            header->type = MACOS_ICMP_ECHO;
            header->code = 0;
            header->identifier = htons(identifier);
            header->sequence = htons(sequence);
            memset(packet + sizeof(MacosIcmpPacket), 0x42, payload_size);
            header->checksum = macos_icmp_checksum(packet, packet_size);

            (void)gettimeofday(&start_time, 0);
            if (sendto(sock, packet, packet_size, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                continue;
            }

            for (;;) {
                struct sockaddr_storage peer;
                socklen_t peer_len = sizeof(peer);
                ssize_t reply_size = recvfrom(sock, reply, sizeof(reply), 0, (struct sockaddr *)&peer, &peer_len);
                size_t offset;
                const MacosIcmpPacket *reply_header;
                int matched = 0;

                if (reply_size < 0) {
                    break;
                }
                offset = 0U;
                if ((size_t)reply_size >= 20U && (reply[0] >> 4) == 4U) {
                    offset = (size_t)(reply[0] & 0x0fU) * 4U;
                }
                if ((size_t)reply_size < offset + sizeof(MacosIcmpPacket)) {
                    continue;
                }
                reply_header = (const MacosIcmpPacket *)(reply + offset);
                if (reply_header->type == MACOS_ICMP_ECHO_REPLY &&
                    ntohs(reply_header->sequence) == sequence &&
                    (allow_identifier_rewrite || ntohs(reply_header->identifier) == identifier)) {
                    matched = 1;
                    hop->reached_destination = 1;
                } else if (reply_header->type == MACOS_ICMP_TIME_EXCEEDED &&
                           macos_trace_match_inner_ipv4(reply, (size_t)reply_size, offset, identifier, sequence, allow_identifier_rewrite)) {
                    matched = 1;
                }
                if (!matched) {
                    continue;
                }

                (void)gettimeofday(&end_time, 0);
                hop->probe_replied[probe] = 1U;
                hop->rtt_milliseconds[probe] = (unsigned int)(macos_elapsed_milliseconds(&start_time, &end_time) + 0.5);
                hop->reply_count += 1U;
                if (peer.ss_family == AF_INET) {
                    const struct sockaddr_in *peer4 = (const struct sockaddr_in *)&peer;
                    (void)inet_ntop(AF_INET, &peer4->sin_addr, hop->address, sizeof(hop->address));
                    if (!options->numeric_only) {
                        macos_trace_lookup_name((const struct sockaddr *)&peer, peer_len, hop->hostname, sizeof(hop->hostname));
                    }
                }
                break;
            }
        }

        if (options->hop_callback != 0) {
            options->hop_callback(hop, options->hop_callback_user_data);
        }
        hop_count += 1U;
        if (hop->reached_destination) {
            break;
        }
    }

    (void)platform_close(sock);
    *hop_count_out = hop_count;
    return 0;
}

int platform_poll_fds(const int *fds, size_t fd_count, size_t *ready_index_out, int timeout_milliseconds) {
    struct pollfd stack_fds[16];
    struct pollfd *poll_fds = stack_fds;
    size_t index;
    int result;

    if (fds == 0 || fd_count == 0U || ready_index_out == 0) {
        return -1;
    }
    if (fd_count > sizeof(stack_fds) / sizeof(stack_fds[0])) {
        poll_fds = (struct pollfd *)rt_malloc(fd_count * sizeof(poll_fds[0]));
        if (poll_fds == 0) {
            return -1;
        }
    }

    for (index = 0; index < fd_count; ++index) {
        poll_fds[index].fd = fds[index];
        poll_fds[index].events = (short)(POLLIN | POLLERR | POLLHUP);
        poll_fds[index].revents = 0;
    }

    result = poll(poll_fds, (nfds_t)fd_count, timeout_milliseconds);
    if (result <= 0) {
        if (poll_fds != stack_fds) {
            rt_free(poll_fds);
        }
        return result;
    }

    for (index = 0; index < fd_count; ++index) {
        if (poll_fds[index].revents != 0) {
            *ready_index_out = index;
            if (poll_fds != stack_fds) {
                rt_free(poll_fds);
            }
            return result;
        }
    }

    if (poll_fds != stack_fds) {
        rt_free(poll_fds);
    }
    return 0;
}

int platform_get_terminal_size(int fd, unsigned int *rows_out, unsigned int *columns_out) {
    struct winsize size;

    memset(&size, 0, sizeof(size));
    if (ioctl(fd, TIOCGWINSZ, &size) != 0 || (size.ws_row == 0 && size.ws_col == 0)) {
        return -1;
    }
    if (rows_out != 0) {
        *rows_out = (unsigned int)size.ws_row;
    }
    if (columns_out != 0) {
        *columns_out = (unsigned int)size.ws_col;
    }
    return 0;
}

int platform_terminal_get_mode(int fd, PlatformTerminalMode *mode_out) {
    struct termios term;
    struct winsize size;

    if (mode_out == 0) {
        return -1;
    }
    if (tcgetattr(fd, &term) != 0) {
        return -1;
    }

    memset(mode_out, 0, sizeof(*mode_out));
    mode_out->echo = (term.c_lflag & ECHO) != 0 ? 1 : 0;
    mode_out->icanon = (term.c_lflag & ICANON) != 0 ? 1 : 0;
    mode_out->isig = (term.c_lflag & ISIG) != 0 ? 1 : 0;
    mode_out->ixon = (term.c_iflag & IXON) != 0 ? 1 : 0;
    mode_out->opost = (term.c_oflag & OPOST) != 0 ? 1 : 0;

    memset(&size, 0, sizeof(size));
    if (ioctl(fd, TIOCGWINSZ, &size) == 0) {
        mode_out->rows = (unsigned int)size.ws_row;
        mode_out->columns = (unsigned int)size.ws_col;
    }
    return 0;
}

int platform_terminal_set_mode(int fd, const PlatformTerminalMode *mode, unsigned int change_mask) {
    struct termios term;

    if (mode == 0) {
        return -1;
    }
    if ((change_mask & (PLATFORM_TERMINAL_ECHO | PLATFORM_TERMINAL_ICANON | PLATFORM_TERMINAL_ISIG |
                        PLATFORM_TERMINAL_IXON | PLATFORM_TERMINAL_OPOST)) != 0U) {
        if (tcgetattr(fd, &term) != 0) {
            return -1;
        }
        if ((change_mask & PLATFORM_TERMINAL_ECHO) != 0U) {
            term.c_lflag = mode->echo ? (term.c_lflag | ECHO) : (term.c_lflag & ~(tcflag_t)ECHO);
        }
        if ((change_mask & PLATFORM_TERMINAL_ICANON) != 0U) {
            term.c_lflag = mode->icanon ? (term.c_lflag | ICANON) : (term.c_lflag & ~(tcflag_t)ICANON);
        }
        if ((change_mask & PLATFORM_TERMINAL_ISIG) != 0U) {
            term.c_lflag = mode->isig ? (term.c_lflag | ISIG) : (term.c_lflag & ~(tcflag_t)ISIG);
        }
        if ((change_mask & PLATFORM_TERMINAL_IXON) != 0U) {
            term.c_iflag = mode->ixon ? (term.c_iflag | IXON) : (term.c_iflag & ~(tcflag_t)IXON);
        }
        if ((change_mask & PLATFORM_TERMINAL_OPOST) != 0U) {
            term.c_oflag = mode->opost ? (term.c_oflag | OPOST) : (term.c_oflag & ~(tcflag_t)OPOST);
        }
        if (tcsetattr(fd, TCSANOW, &term) != 0) {
            return -1;
        }
    }

    if ((change_mask & (PLATFORM_TERMINAL_ROWS | PLATFORM_TERMINAL_COLUMNS)) != 0U) {
        struct winsize size;

        memset(&size, 0, sizeof(size));
        if (ioctl(fd, TIOCGWINSZ, &size) != 0) {
            return -1;
        }
        if ((change_mask & PLATFORM_TERMINAL_ROWS) != 0U) {
            size.ws_row = (unsigned short)mode->rows;
        }
        if ((change_mask & PLATFORM_TERMINAL_COLUMNS) != 0U) {
            size.ws_col = (unsigned short)mode->columns;
        }
        if (ioctl(fd, TIOCSWINSZ, &size) != 0) {
            return -1;
        }
    }

    return 0;
}

int platform_terminal_enable_raw_mode(int fd, PlatformTerminalState *state_out) {
    struct termios saved;
    struct termios raw;

    if (state_out == 0 || sizeof(saved) > PLATFORM_TERMINAL_STATE_CAPACITY) {
        return -1;
    }
    if (tcgetattr(fd, &saved) != 0) {
        return -1;
    }

    memset(state_out, 0, sizeof(*state_out));
    memcpy(state_out->bytes, &saved, sizeof(saved));
    memcpy(&raw, &saved, sizeof(raw));
    make_raw_termios(&raw);

    return tcsetattr(fd, TCSANOW, &raw);
}

int platform_terminal_restore_mode(int fd, const PlatformTerminalState *state) {
    struct termios saved;

    if (state == 0 || sizeof(saved) > PLATFORM_TERMINAL_STATE_CAPACITY) {
        return -1;
    }
    memcpy(&saved, state->bytes, sizeof(saved));
    return tcsetattr(fd, TCSANOW, &saved);
}
