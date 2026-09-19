#include "platform.h"
#include "common.h"
#include "signal_util.h"

_Static_assert(sizeof(struct linux_termios) <= PLATFORM_TERMINAL_STATE_CAPACITY, "PlatformTerminalState is too small");

#define LINUX_ENV_MAX_ENTRIES 256
#define LINUX_ENV_ENTRY_CAPACITY 1024
#define LINUX_ENV_RAW_CAPACITY (LINUX_ENV_MAX_ENTRIES * LINUX_ENV_ENTRY_CAPACITY)

static char linux_env_raw[LINUX_ENV_RAW_CAPACITY];
static char *linux_env_entries[LINUX_ENV_MAX_ENTRIES + 1];
static size_t linux_env_count = 0U;
static size_t linux_env_raw_used = 0U;
static int linux_env_initialized = 0;
static int linux_random_fd = -2;

static int linux_path_has_slash(const char *path) {
    unsigned long i = 0;

    while (path[i] != '\0') {
        if (path[i] == '/') {
            return 1;
        }
        i += 1;
    }

    return 0;
}

static int linux_is_valid_env_name(const char *name) {
    size_t i = 0U;

    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    if (!((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') ||
          name[0] == '_')) {
        return 0;
    }
    while (name[i] != '\0') {
        if (!((name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= '0' && name[i] <= '9') ||
              name[i] == '_')) {
            return 0;
        }
        i += 1U;
    }
    return 1;
}

static int linux_env_name_matches(const char *entry, const char *name) {
    size_t i = 0U;

    if (entry == 0 || name == 0) {
        return 0;
    }
    while (name[i] != '\0' && entry[i] == name[i]) {
        i += 1U;
    }
    return name[i] == '\0' && entry[i] == '=';
}

static int linux_compact_env_entries(void) {
    size_t used = 0U;
    size_t i;

    for (i = 0U; i < linux_env_count; ++i) {
        char *entry = linux_env_entries[i];
        size_t entry_len;

        if (entry == 0 || entry[0] == '\0') {
            continue;
        }

        entry_len = rt_strlen(entry) + 1U;
        if (used + entry_len > sizeof(linux_env_raw)) {
            return -1;
        }

        memmove(linux_env_raw + used, entry, entry_len);
        linux_env_entries[i] = linux_env_raw + used;
        used += entry_len;
    }

    linux_env_raw_used = used;
    if (linux_env_raw_used < sizeof(linux_env_raw)) {
        linux_env_raw[linux_env_raw_used] = '\0';
        linux_env_raw_used += 1U;
    }
    linux_env_entries[linux_env_count] = 0;
    return 0;
}

static int linux_write_env_entry(size_t index, const char *name, const char *value) {
    size_t name_len;
    size_t value_len;
    size_t total_len;
    char *entry;

    if (index >= LINUX_ENV_MAX_ENTRIES || name == 0) {
        return -1;
    }
    name_len = rt_strlen(name);
    value_len = value != 0 ? rt_strlen(value) : 0U;
    total_len = name_len + 1U + value_len + 1U;

    if (total_len > sizeof(linux_env_raw)) {
        return -1;
    }
    if (linux_env_raw_used + total_len > sizeof(linux_env_raw)) {
        if (linux_compact_env_entries() != 0 || linux_env_raw_used + total_len > sizeof(linux_env_raw)) {
            return -1;
        }
    }

    entry = linux_env_raw + linux_env_raw_used;
    memcpy(entry, name, name_len);
    entry[name_len] = '=';
    if (value_len > 0U) {
        memcpy(entry + name_len + 1U, value, value_len);
    }
    entry[name_len + 1U + value_len] = '\0';
    linux_env_entries[index] = entry;
    linux_env_raw_used += total_len;
    return 0;
}

static void linux_env_ensure_loaded(void) {
    long fd;
    long bytes;
    size_t start = 0U;
    size_t i;

    if (linux_env_initialized) {
        return;
    }
    linux_env_initialized = 1;
    linux_env_count = 0U;
    linux_env_raw_used = 0U;
    fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)"/proc/self/environ", LINUX_O_RDONLY, 0);
    if (fd < 0) {
        linux_env_entries[0] = 0;
        return;
    }
    bytes = linux_syscall3(LINUX_SYS_READ, fd, (long)linux_env_raw, (long)(sizeof(linux_env_raw) - 1U));
    linux_syscall1(LINUX_SYS_CLOSE, fd);
    if (bytes <= 0) {
        linux_env_entries[0] = 0;
        return;
    }
    linux_env_raw[bytes] = '\0';
    linux_env_raw_used = (size_t)bytes + 1U;
    for (i = 0U; i < (size_t)bytes && linux_env_count < LINUX_ENV_MAX_ENTRIES; ++i) {
        if (linux_env_raw[i] == '\0') {
            if (i > start) {
                linux_env_entries[linux_env_count] = linux_env_raw + start;
                linux_env_count += 1U;
            }
            start = i + 1U;
        }
    }
    if (start < (size_t)bytes && linux_env_count < LINUX_ENV_MAX_ENTRIES) {
        linux_env_entries[linux_env_count] = linux_env_raw + start;
        linux_env_count += 1U;
    }
    linux_env_entries[linux_env_count] = 0;
}

static int linux_find_env_index(const char *name) {
    size_t i;

    linux_env_ensure_loaded();
    for (i = 0U; i < linux_env_count; ++i) {
        if (linux_env_name_matches(linux_env_entries[i], name)) {
            return (int)i;
        }
    }
    return -1;
}

static void linux_child_exit(int status) {
    linux_syscall1(LINUX_SYS_EXIT, status);
    for (;;) {
    }
}

static void linux_try_exec(const char *path, char *const argv[]) {
    linux_env_ensure_loaded();
    linux_syscall3(LINUX_SYS_EXECVE, (long)path, (long)argv, (long)linux_env_entries);
}

typedef struct {
    char name[8];
    int value;
} LinuxSignalEntry;

static const LinuxSignalEntry LINUX_SIGNAL_TABLE[] = {
    { "HUP", LINUX_SIGHUP },
    { "INT", LINUX_SIGINT },
    { "QUIT", LINUX_SIGQUIT },
    { "ILL", LINUX_SIGILL },
    { "TRAP", LINUX_SIGTRAP },
    { "ABRT", LINUX_SIGABRT },
    { "BUS", LINUX_SIGBUS },
    { "FPE", LINUX_SIGFPE },
    { "KILL", LINUX_SIGKILL },
    { "USR1", LINUX_SIGUSR1 },
    { "SEGV", LINUX_SIGSEGV },
    { "USR2", LINUX_SIGUSR2 },
    { "PIPE", LINUX_SIGPIPE },
    { "ALRM", LINUX_SIGALRM },
    { "TERM", LINUX_SIGTERM },
    { "CHLD", LINUX_SIGCHLD },
    { "CONT", LINUX_SIGCONT },
    { "STOP", LINUX_SIGSTOP },
    { "TSTP", LINUX_SIGTSTP },
    { "TTIN", LINUX_SIGTTIN },
    { "TTOU", LINUX_SIGTTOU },
};

static int linux_set_signal_disposition(int signal_number, unsigned long handler) {
    struct linux_sigaction action;

    memset(&action, 0, sizeof(action));
    action.handler = handler;
    return linux_syscall4(LINUX_SYS_RT_SIGACTION,
                          signal_number,
                          (long)&action,
                          0,
                          (long)sizeof(action.mask)) < 0 ? -1 : 0;
}

static void linux_reset_child_signals(void) {
    (void)linux_set_signal_disposition(LINUX_SIGINT, LINUX_SIG_DFL);
    (void)linux_set_signal_disposition(LINUX_SIGQUIT, LINUX_SIG_DFL);
    (void)linux_set_signal_disposition(LINUX_SIGPIPE, LINUX_SIG_DFL);
}

static int linux_decode_wait_status(int status) {
    if ((status & 0x7f) == 0) {
        return (status >> 8) & 0xff;
    }
    return 128 + (status & 0x7f);
}

static void linux_close_child_fds(void) {
    long result = linux_syscall3(LINUX_SYS_CLOSE_RANGE, 3, ~0UL, 0);
    int fd;

    if (result >= 0) {
        return;
    }

    for (fd = 3; fd < 1024; ++fd) {
        (void)linux_syscall1(LINUX_SYS_CLOSE, fd);
    }
}

int platform_parse_signal_name(const char *text, int *signal_out) {
    unsigned long long numeric = 0;
    size_t i;

    if (text == 0 || signal_out == 0 || text[0] == '\0') {
        return -1;
    }

    if (rt_parse_uint(text, &numeric) == 0) {
        *signal_out = (int)numeric;
        return 0;
    }

    for (i = 0; i < sizeof(LINUX_SIGNAL_TABLE) / sizeof(LINUX_SIGNAL_TABLE[0]); ++i) {
        if (signal_name_matches(text, LINUX_SIGNAL_TABLE[i].name)) {
            *signal_out = LINUX_SIGNAL_TABLE[i].value;
            return 0;
        }
    }

    return -1;
}

const char *platform_signal_name(int signal_number) {
    size_t i;

    for (i = 0; i < sizeof(LINUX_SIGNAL_TABLE) / sizeof(LINUX_SIGNAL_TABLE[0]); ++i) {
        if (LINUX_SIGNAL_TABLE[i].value == signal_number) {
            return LINUX_SIGNAL_TABLE[i].name;
        }
    }

    return "UNKNOWN";
}

void platform_write_signal_list(int fd) {
    size_t i;

    for (i = 0; i < sizeof(LINUX_SIGNAL_TABLE) / sizeof(LINUX_SIGNAL_TABLE[0]); ++i) {
        if (i > 0) {
            (void)platform_write(fd, " ", 1U);
        }
        (void)platform_write(fd, LINUX_SIGNAL_TABLE[i].name, rt_strlen(LINUX_SIGNAL_TABLE[i].name));
    }
    (void)platform_write(fd, "\n", 1U);
}

const char *platform_getenv(const char *name) {
    int index;
    char *entry;

    if (!linux_is_valid_env_name(name)) {
        return 0;
    }
    index = linux_find_env_index(name);
    if (index < 0) {
        return 0;
    }
    entry = linux_env_entries[index];
    while (*entry != '\0' && *entry != '=') {
        entry += 1;
    }
    return *entry == '=' ? entry + 1 : 0;
}

const char *platform_getenv_entry(size_t index) {
    linux_env_ensure_loaded();
    return index < linux_env_count ? linux_env_entries[index] : 0;
}

int platform_setenv(const char *name, const char *value, int overwrite) {
    int index;

    if (!linux_is_valid_env_name(name)) {
        return -1;
    }
    linux_env_ensure_loaded();
    index = linux_find_env_index(name);
    if (index >= 0) {
        if (!overwrite) {
            return 0;
        }
        return linux_write_env_entry((size_t)index, name, value != 0 ? value : "");
    }
    if (linux_env_count >= LINUX_ENV_MAX_ENTRIES) {
        return -1;
    }
    if (linux_write_env_entry(linux_env_count, name, value != 0 ? value : "") != 0) {
        return -1;
    }
    linux_env_count += 1U;
    linux_env_entries[linux_env_count] = 0;
    return 0;
}

int platform_unsetenv(const char *name) {
    int index;
    size_t i;

    if (!linux_is_valid_env_name(name)) {
        return -1;
    }
    index = linux_find_env_index(name);
    if (index < 0) {
        return 0;
    }
    for (i = (size_t)index; i + 1U < linux_env_count; ++i) {
        linux_env_entries[i] = linux_env_entries[i + 1U];
    }
    if (linux_env_count > 0U) {
        linux_env_count -= 1U;
        linux_env_entries[linux_env_count] = 0;
    }
    if (linux_env_count == 0U) {
        linux_env_raw_used = 0U;
        linux_env_raw[0] = '\0';
    }
    return 0;
}

int platform_clearenv(void) {
    linux_env_ensure_loaded();
    linux_env_count = 0U;
    linux_env_raw_used = 0U;
    linux_env_raw[0] = '\0';
    linux_env_entries[0] = 0;
    return 0;
}

int platform_isatty(int fd) {
    struct linux_winsize winsize;

    return linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TIOCGWINSZ, (long)&winsize) < 0 ? 0 : 1;
}

int platform_get_terminal_size(int fd, unsigned int *rows_out, unsigned int *columns_out) {
    struct linux_winsize winsize;

    if (linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TIOCGWINSZ, (long)&winsize) < 0 ||
        (winsize.ws_row == 0U && winsize.ws_col == 0U)) {
        return -1;
    }

    if (rows_out != 0) {
        *rows_out = (unsigned int)winsize.ws_row;
    }
    if (columns_out != 0) {
        *columns_out = (unsigned int)winsize.ws_col;
    }

    return 0;
}

int platform_get_process_id(void) {
    long pid = linux_syscall0(LINUX_SYS_GETPID);
    return pid < 0 ? -1 : (int)pid;
}

long platform_read_kernel_log(char *buffer, size_t buffer_size, int clear_after_read) {
    long bytes;
    long action;

    if (buffer == 0 || buffer_size == 0U) {
        return -1;
    }

    action = clear_after_read ? 4 : 3;
    bytes = linux_syscall3(LINUX_SYS_SYSLOG, action, (long)buffer, (long)(buffer_size - 1U));
    if (bytes < 0) {
        return -1;
    }

    buffer[bytes] = '\0';
    return bytes;
}

int platform_clear_kernel_log(void) {
    return linux_syscall3(LINUX_SYS_SYSLOG, 5, 0, 0) < 0 ? -1 : 0;
}

int platform_set_console_log_level(int level) {
    return linux_syscall3(LINUX_SYS_SYSLOG, 8, 0, level) < 0 ? -1 : 0;
}

int platform_random_bytes(unsigned char *buffer, size_t count) {
    long fd;
    size_t offset = 0;

    if (buffer == 0 && count != 0U) {
        return -1;
    }
    if (count == 0U) {
        return 0;
    }

    while (offset < count) {
        long bytes = linux_syscall3(LINUX_SYS_GETRANDOM, (long)(buffer + offset), (long)(count - offset), 0);

        if (bytes == -LINUX_EINTR) {
            continue;
        }
        if (bytes == -LINUX_ENOSYS || bytes == -LINUX_EINVAL) {
            break;
        }
        if (bytes <= 0) {
            return -1;
        }
        offset += (size_t)bytes;
    }
    if (offset == count) {
        return 0;
    }

    fd = (long)linux_random_fd;
    if (fd < 0) {
        fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)"/dev/urandom", LINUX_O_RDONLY | LINUX_O_CLOEXEC, 0);
        if (fd < 0) {
            return -1;
        }
        linux_random_fd = (int)fd;
    }

    while (offset < count) {
        long bytes = linux_syscall3(LINUX_SYS_READ, fd, (long)(buffer + offset), (long)(count - offset));
        if (bytes <= 0) {
            linux_syscall1(LINUX_SYS_CLOSE, fd);
            linux_random_fd = -2;
            return -1;
        }
        offset += (size_t)bytes;
    }

    return 0;
}

int platform_terminal_get_mode(int fd, PlatformTerminalMode *mode_out) {
    struct linux_termios term;
    struct linux_winsize window_size;

    if (mode_out == 0) {
        return -1;
    }
    if (linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TCGETS, (long)&term) < 0) {
        return -1;
    }

    memset(mode_out, 0, sizeof(*mode_out));
    mode_out->echo = (term.c_lflag & LINUX_ECHO) != 0U ? 1 : 0;
    mode_out->icanon = (term.c_lflag & LINUX_ICANON) != 0U ? 1 : 0;
    mode_out->isig = (term.c_lflag & LINUX_ISIG) != 0U ? 1 : 0;
    mode_out->ixon = (term.c_iflag & LINUX_IXON) != 0U ? 1 : 0;
    mode_out->opost = (term.c_oflag & LINUX_OPOST) != 0U ? 1 : 0;

    memset(&window_size, 0, sizeof(window_size));
    if (linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TIOCGWINSZ, (long)&window_size) == 0) {
        mode_out->rows = (unsigned int)window_size.ws_row;
        mode_out->columns = (unsigned int)window_size.ws_col;
    }

    return 0;
}

int platform_terminal_set_mode(int fd, const PlatformTerminalMode *mode, unsigned int change_mask) {
    struct linux_termios term;

    if (mode == 0) {
        return -1;
    }

    if ((change_mask & (PLATFORM_TERMINAL_ECHO |
                        PLATFORM_TERMINAL_ICANON |
                        PLATFORM_TERMINAL_ISIG |
                        PLATFORM_TERMINAL_IXON |
                        PLATFORM_TERMINAL_OPOST)) != 0U) {
        if (linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TCGETS, (long)&term) < 0) {
            return -1;
        }
        if ((change_mask & PLATFORM_TERMINAL_ECHO) != 0U) {
            term.c_lflag = mode->echo ? (term.c_lflag | LINUX_ECHO) : (term.c_lflag & ~LINUX_ECHO);
        }
        if ((change_mask & PLATFORM_TERMINAL_ICANON) != 0U) {
            term.c_lflag = mode->icanon ? (term.c_lflag | LINUX_ICANON) : (term.c_lflag & ~LINUX_ICANON);
        }
        if ((change_mask & PLATFORM_TERMINAL_ISIG) != 0U) {
            term.c_lflag = mode->isig ? (term.c_lflag | LINUX_ISIG) : (term.c_lflag & ~LINUX_ISIG);
        }
        if ((change_mask & PLATFORM_TERMINAL_IXON) != 0U) {
            term.c_iflag = mode->ixon ? (term.c_iflag | LINUX_IXON) : (term.c_iflag & ~LINUX_IXON);
        }
        if ((change_mask & PLATFORM_TERMINAL_OPOST) != 0U) {
            term.c_oflag = mode->opost ? (term.c_oflag | LINUX_OPOST) : (term.c_oflag & ~LINUX_OPOST);
        }
        if (linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TCSETS, (long)&term) < 0) {
            return -1;
        }
    }

    if ((change_mask & (PLATFORM_TERMINAL_ROWS | PLATFORM_TERMINAL_COLUMNS)) != 0U) {
        struct linux_winsize window_size;

        memset(&window_size, 0, sizeof(window_size));
        if (linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TIOCGWINSZ, (long)&window_size) < 0) {
            return -1;
        }
        if ((change_mask & PLATFORM_TERMINAL_ROWS) != 0U) {
            window_size.ws_row = (unsigned short)mode->rows;
        }
        if ((change_mask & PLATFORM_TERMINAL_COLUMNS) != 0U) {
            window_size.ws_col = (unsigned short)mode->columns;
        }
        if (linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TIOCSWINSZ, (long)&window_size) < 0) {
            return -1;
        }
    }

    return 0;
}

int platform_terminal_enable_raw_mode(int fd, PlatformTerminalState *state_out) {
    struct linux_termios saved;
    struct linux_termios raw;

    if (state_out == 0) {
        return -1;
    }

    if (linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TCGETS, (long)&saved) < 0) {
        return -1;
    }

    memset(state_out, 0, sizeof(*state_out));
    memcpy(state_out->bytes, &saved, sizeof(saved));

    raw = saved;
    raw.c_iflag &= ~(LINUX_BRKINT | LINUX_ICRNL | LINUX_INPCK | LINUX_ISTRIP | LINUX_IXON);
    raw.c_cflag |= LINUX_CS8;
    /* Keep ISIG enabled so Ctrl+C still interrupts interactive tools. */
    raw.c_lflag &= ~(LINUX_ECHO | LINUX_ICANON | LINUX_IEXTEN);
    raw.c_cc[LINUX_VMIN] = 1;
    raw.c_cc[LINUX_VTIME] = 0;

    return linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TCSETS, (long)&raw) < 0 ? -1 : 0;
}

int platform_terminal_restore_mode(int fd, const PlatformTerminalState *state) {
    struct linux_termios saved;

    if (state == 0) {
        return -1;
    }

    memcpy(&saved, state->bytes, sizeof(saved));
    return linux_syscall3(LINUX_SYS_IOCTL, fd, LINUX_TCSETS, (long)&saved) < 0 ? -1 : 0;
}

int platform_create_pipe(int pipe_fds[2]) {
    return linux_syscall2(LINUX_SYS_PIPE2, (long)pipe_fds, LINUX_O_CLOEXEC) < 0 ? -1 : 0;
}

int platform_drop_privileges(const char *username, const char *groupname) {
    unsigned long long current_uid = (unsigned long long)linux_syscall1(LINUX_SYS_GETUID, 0);
    unsigned long long current_gid = (unsigned long long)linux_syscall1(LINUX_SYS_GETGID, 0);
    unsigned long long target_uid = current_uid;
    unsigned long long target_gid = current_gid;
    unsigned int lookup_gid = 0U;
    PlatformIdentity identity;

    if ((username == 0 || username[0] == '\0') && (groupname == 0 || groupname[0] == '\0')) {
        return 0;
    }

    if (groupname != 0 && groupname[0] != '\0') {
        if (platform_lookup_group(groupname, &lookup_gid) != 0) {
            return -1;
        }
        target_gid = (unsigned long long)lookup_gid;
    }

    if (username != 0 && username[0] != '\0') {
        if (platform_lookup_identity(username, &identity) != 0) {
            return -1;
        }
        target_uid = (unsigned long long)identity.uid;
        if (groupname == 0 || groupname[0] == '\0') {
            target_gid = (unsigned long long)identity.gid;
        }
    }

    if (current_uid == 0ULL) {
        (void)linux_syscall2(LINUX_SYS_SETGROUPS, 0, 0);
    }
    if (target_gid != current_gid) {
        if (linux_syscall1(LINUX_SYS_SETGID, (long)target_gid) < 0) {
            return -1;
        }
    }
    if (target_uid != current_uid) {
        if (linux_syscall1(LINUX_SYS_SETUID, (long)target_uid) < 0) {
            return -1;
        }
    }
    if ((unsigned long long)linux_syscall1(LINUX_SYS_GETUID, 0) != target_uid ||
        (unsigned long long)linux_syscall1(LINUX_SYS_GETGID, 0) != target_gid) {
        return -1;
    }

    return 0;
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
    long pid;

    if (argv == 0 || argv[0] == 0 || pid_out == 0) {
        return -1;
    }

    pid = linux_syscall5(LINUX_SYS_CLONE, LINUX_SIGCHLD, 0, 0, 0, 0);
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        linux_reset_child_signals();

        if (working_directory != 0 && working_directory[0] != '\0') {
            if (linux_syscall1(LINUX_SYS_CHDIR, (long)working_directory) < 0) {
                linux_child_exit(126);
            }
        }
        if (platform_drop_privileges(drop_user, drop_group) != 0) {
            linux_child_exit(126);
        }

        if (input_path != 0) {
            long fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)input_path, LINUX_O_RDONLY, 0);
            if (fd < 0) {
                linux_child_exit(126);
            }
            if (fd != 0) {
                if (linux_syscall3(LINUX_SYS_DUP3, fd, 0, 0) < 0) {
                    linux_child_exit(126);
                }
                linux_syscall1(LINUX_SYS_CLOSE, fd);
            }
        } else if (stdin_fd >= 0 && stdin_fd != 0) {
            if (linux_syscall3(LINUX_SYS_DUP3, stdin_fd, 0, 0) < 0) {
                linux_child_exit(126);
            }
        }

        if (output_path != 0) {
            long flags = LINUX_O_WRONLY | LINUX_O_CREAT | (output_append ? LINUX_O_APPEND : LINUX_O_TRUNC);
            long fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)output_path, flags, 0644);
            if (fd < 0) {
                linux_child_exit(126);
            }
            if (fd != 1) {
                if (linux_syscall3(LINUX_SYS_DUP3, fd, 1, 0) < 0) {
                    linux_child_exit(126);
                }
                linux_syscall1(LINUX_SYS_CLOSE, fd);
            }
        } else if (stdout_fd >= 0 && stdout_fd != 1) {
            if (linux_syscall3(LINUX_SYS_DUP3, stdout_fd, 1, 0) < 0) {
                linux_child_exit(126);
            }
        }

        if (output_path != 0 || stdout_fd >= 0) {
            if (linux_syscall3(LINUX_SYS_DUP3, 1, 2, 0) < 0) {
                linux_child_exit(126);
            }
        }

        if (stdin_fd > 1) {
            linux_syscall1(LINUX_SYS_CLOSE, stdin_fd);
        }
        if (stdout_fd > 1) {
            linux_syscall1(LINUX_SYS_CLOSE, stdout_fd);
        }

        linux_close_child_fds();

        if (linux_path_has_slash(argv[0])) {
            linux_try_exec(argv[0], argv);
        } else {
            char candidate[512];
            const char *prefixes[] = { "./", "/bin/", "/usr/bin/" };
            unsigned long i;

            for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
                if (linux_join_path(prefixes[i], argv[0], candidate, sizeof(candidate)) == 0) {
                    linux_try_exec(candidate, argv);
                }
            }
        }

        linux_child_exit(127);
    }

    *pid_out = (int)pid;
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

#if defined(__x86_64__)
typedef struct {
    unsigned long r15, r14, r13, r12, rbp, rbx;
    unsigned long r11, r10, r9, r8, rax, rcx, rdx, rsi, rdi;
    unsigned long orig_rax, rip, cs, eflags, rsp, ss;
    unsigned long fs_base, gs_base, ds, es, fs, gs;
} LinuxUserRegs;

#define LINUX_PTRACE_TRACEME 0
#define LINUX_PTRACE_PEEKUSER 3
#define LINUX_PTRACE_SYSCALL 24
#define LINUX_PTRACE_GETREGS 12
#define LINUX_PTRACE_SETOPTIONS 0x4200
#define LINUX_PTRACE_O_TRACESYSGOOD 1

static int linux_wait_for_trace(int pid, int *status_out) {
    return linux_syscall4(LINUX_SYS_WAIT4, pid, (long)status_out, 0, 0) < 0 ? -1 : 0;
}

int platform_trace_syscalls(char *const argv[], PlatformSyscallTraceCallback callback, void *user_data, int *exit_status_out) {
    long pid;
    int status = 0;
    int in_syscall = 0;
    PlatformSyscallEvent event;

    if (argv == 0 || argv[0] == 0 || callback == 0) return -1;
    pid = linux_syscall5(LINUX_SYS_CLONE, LINUX_SIGCHLD, 0, 0, 0, 0);
    if (pid < 0) return -1;
    if (pid == 0) {
        (void)linux_syscall4(LINUX_SYS_PTRACE, LINUX_PTRACE_TRACEME, 0, 0, 0);
        (void)linux_syscall2(LINUX_SYS_KILL, linux_syscall0(LINUX_SYS_GETPID), LINUX_SIGSTOP);
        linux_reset_child_signals();
        if (linux_path_has_slash(argv[0])) {
            linux_try_exec(argv[0], argv);
        } else {
            char candidate[512];
            const char *prefixes[] = { "./", "/bin/", "/usr/bin/" };
            unsigned long i;
            for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
                if (linux_join_path(prefixes[i], argv[0], candidate, sizeof(candidate)) == 0) {
                    linux_try_exec(candidate, argv);
                }
            }
        }
        linux_child_exit(127);
    }
    if (linux_wait_for_trace((int)pid, &status) != 0) return -1;
    (void)linux_syscall4(LINUX_SYS_PTRACE, LINUX_PTRACE_SETOPTIONS, pid, 0, LINUX_PTRACE_O_TRACESYSGOOD);
    for (;;) {
        LinuxUserRegs regs;
        int sig;

        if (linux_syscall4(LINUX_SYS_PTRACE, LINUX_PTRACE_SYSCALL, pid, 0, 0) < 0) return -1;
        if (linux_wait_for_trace((int)pid, &status) != 0) return -1;
        if ((status & 0x7f) == 0) {
            if (exit_status_out != 0) *exit_status_out = (status >> 8) & 0xff;
            return 0;
        }
        if ((status & 0xff) != 0x7f) continue;
        sig = (status >> 8) & 0xff;
        if (sig != (LINUX_SIGTRAP | 0x80)) {
            continue;
        }
        if (linux_syscall4(LINUX_SYS_PTRACE, LINUX_PTRACE_GETREGS, pid, 0, (long)&regs) < 0) return -1;
        rt_memset(&event, 0, sizeof(event));
        event.entering = !in_syscall;
        event.number = (long)regs.orig_rax;
        event.args[0] = (long)regs.rdi;
        event.args[1] = (long)regs.rsi;
        event.args[2] = (long)regs.rdx;
        event.args[3] = (long)regs.r10;
        event.args[4] = (long)regs.r8;
        event.args[5] = (long)regs.r9;
        event.result = (long)regs.rax;
        if (callback(&event, user_data) != 0) return -1;
        in_syscall = !in_syscall;
    }
}
#else
int platform_trace_syscalls(char *const argv[], PlatformSyscallTraceCallback callback, void *user_data, int *exit_status_out) {
    (void)argv;
    (void)callback;
    (void)user_data;
    if (exit_status_out != 0) *exit_status_out = 1;
    return -1;
}
#endif

int platform_send_signal(int pid, int signal_number) {
    return linux_syscall2(LINUX_SYS_KILL, pid, signal_number) < 0 ? -1 : 0;
}

int platform_ignore_signal(int signal_number) {
    return linux_set_signal_disposition(signal_number, LINUX_SIG_IGN);
}

int platform_shutdown_system(int action) {
    unsigned long command = LINUX_REBOOT_CMD_POWER_OFF;

    if (action == PLATFORM_SHUTDOWN_REBOOT) {
        command = LINUX_REBOOT_CMD_RESTART;
    } else if (action == PLATFORM_SHUTDOWN_HALT) {
        command = LINUX_REBOOT_CMD_HALT;
    }

    (void)platform_sync_all();
    return linux_syscall4(LINUX_SYS_REBOOT,
                          LINUX_REBOOT_MAGIC1,
                          LINUX_REBOOT_MAGIC2,
                          command,
                          0) < 0 ? -1 : 0;
}

int platform_wait_process(int pid, int *exit_status_out) {
    int status = 0;
    long result;

    if (exit_status_out == 0) {
        return -1;
    }

    do {
        result = linux_syscall4(LINUX_SYS_WAIT4, pid, (long)&status, 0, 0);
    } while (result == -LINUX_EINTR);
    if (result < 0) {
        return -1;
    }

    *exit_status_out = linux_decode_wait_status(status);
    return 0;
}

int platform_poll_process_exit(int pid, int *finished_out, int *exit_status_out) {
    int status = 0;
    long waited;

    if (finished_out == 0 || exit_status_out == 0) {
        return -1;
    }

    do {
        waited = linux_syscall4(LINUX_SYS_WAIT4, pid, (long)&status, LINUX_WNOHANG, 0);
    } while (waited == -LINUX_EINTR);
    if (waited == 0) {
        *finished_out = 0;
        *exit_status_out = 0;
        return 0;
    }
    if (waited < 0) {
        return -1;
    }

    *finished_out = 1;
    *exit_status_out = linux_decode_wait_status(status);
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
    const unsigned long long poll_milliseconds = 50ULL;

    if (exit_status_out == 0) {
        return -1;
    }

    for (;;) {
        int status = 0;
        long waited;

        do {
            waited = linux_syscall4(LINUX_SYS_WAIT4, pid, (long)&status, LINUX_WNOHANG, 0);
        } while (waited == -LINUX_EINTR);

        if (waited == pid) {
            *exit_status_out = (timed_out && !preserve_status) ? 124 : linux_decode_wait_status(status);
            return 0;
        }

        if (waited < 0) {
            return -1;
        }

        if (!timed_out && elapsed >= timeout_milliseconds) {
            (void)platform_send_signal(pid, signal_number);
            timed_out = 1;
            after_signal = 0;
        } else if (timed_out && kill_after_milliseconds > 0 && after_signal >= kill_after_milliseconds) {
            (void)platform_send_signal(pid, LINUX_SIGKILL);
            kill_after_milliseconds = 0;
        }

        {
            unsigned long long sleep_for = poll_milliseconds;

            if (!timed_out && elapsed < timeout_milliseconds && timeout_milliseconds - elapsed < sleep_for) {
                sleep_for = timeout_milliseconds - elapsed;
            } else if (timed_out && kill_after_milliseconds > 0 &&
                       after_signal < kill_after_milliseconds &&
                       kill_after_milliseconds - after_signal < sleep_for) {
                sleep_for = kill_after_milliseconds - after_signal;
            }

            if (sleep_for == 0ULL) {
                sleep_for = 1ULL;
            }
            if (platform_sleep_milliseconds(sleep_for) != 0) {
                return -1;
            }

            if (!timed_out) {
                elapsed += sleep_for;
            } else {
                after_signal += sleep_for;
            }
        }
    }
}

static void linux_lookup_username(unsigned int uid, char *buffer, size_t buffer_size) {
    char passwd_buf[4096];
    long passwd_fd;
    long bytes;
    size_t i;

    linux_unsigned_to_string((unsigned long long)uid, buffer, buffer_size);

    passwd_fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)"/etc/passwd", LINUX_O_RDONLY, 0);
    if (passwd_fd < 0) {
        return;
    }
    bytes = linux_syscall3(LINUX_SYS_READ, passwd_fd, (long)passwd_buf, (long)(sizeof(passwd_buf) - 1));
    linux_syscall1(LINUX_SYS_CLOSE, passwd_fd);
    if (bytes <= 0) {
        return;
    }
    passwd_buf[bytes] = '\0';

    i = 0;
    while (i < (size_t)bytes) {
        const char *line = passwd_buf + i;
        const char *p = line;
        size_t name_len;
        unsigned long long file_uid;

        while (i < (size_t)bytes && passwd_buf[i] != '\n') {
            i++;
        }
        passwd_buf[i] = '\0';
        i++;

        while (*p && *p != ':') p++;
        if (!*p) continue;
        name_len = (size_t)(p - line);
        p++;
        while (*p && *p != ':') p++;
        if (!*p) continue;
        p++;
        file_uid = 0;
        while (*p >= '0' && *p <= '9') {
            file_uid = file_uid * 10 + (unsigned long long)(*p - '0');
            p++;
        }
        if (file_uid == (unsigned long long)uid && *p == ':' && name_len > 0 && name_len < buffer_size) {
            memcpy(buffer, line, name_len);
            buffer[name_len] = '\0';
            return;
        }
    }
}

static void linux_load_process_status(const char *status_path, PlatformProcessEntry *entry) {
    char buf[2048];
    long status_fd;
    long bytes;
    size_t i;

    status_fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)status_path, LINUX_O_RDONLY, 0);
    if (status_fd < 0) {
        return;
    }
    bytes = linux_syscall3(LINUX_SYS_READ, status_fd, (long)buf, (long)(sizeof(buf) - 1));
    linux_syscall1(LINUX_SYS_CLOSE, status_fd);
    if (bytes <= 0) {
        return;
    }
    buf[bytes] = '\0';

    i = 0;
    while (i < (size_t)bytes) {
        const char *line = buf + i;
        const char *p;
        size_t j = i;

        while (j < (size_t)bytes && buf[j] != '\n') {
            j++;
        }
        buf[j] = '\0';

        if (rt_strncmp(line, "Name:", 5) == 0) {
            p = line + 5;
            while (*p == ' ' || *p == '\t') p++;
            linux_copy_string(entry->name, sizeof(entry->name), p);
        } else if (rt_strncmp(line, "State:", 6) == 0) {
            char state_buf[16];
            size_t k = 0;
            p = line + 6;
            while (*p == ' ' || *p == '\t') p++;
            while (*p && *p != ' ' && *p != '\t' && k < sizeof(state_buf) - 1) {
                state_buf[k++] = *p++;
            }
            state_buf[k] = '\0';
            linux_copy_string(entry->state, sizeof(entry->state), state_buf);
        } else if (rt_strncmp(line, "PPid:", 5) == 0) {
            unsigned long long ppid = 0;
            p = line + 5;
            while (*p == ' ' || *p == '\t') p++;
            rt_parse_uint(p, &ppid);
            entry->ppid = (int)ppid;
        } else if (rt_strncmp(line, "Uid:", 4) == 0) {
            unsigned long long uid = 0;
            p = line + 4;
            while (*p == ' ' || *p == '\t') p++;
            rt_parse_uint(p, &uid);
            entry->uid = (unsigned int)uid;
            linux_lookup_username(entry->uid, entry->user, sizeof(entry->user));
        } else if (rt_strncmp(line, "VmRSS:", 6) == 0) {
            unsigned long long rss = 0;
            p = line + 6;
            while (*p == ' ' || *p == '\t') p++;
            rt_parse_uint(p, &rss);
            entry->rss_kb = rss;
        }

        i = j + 1;
    }
}

int platform_list_processes(PlatformProcessEntry *entries_out, size_t entry_capacity, size_t *count_out) {
    long fd;
    size_t count = 0;
    char buffer[4096];

    if (entries_out == 0 || count_out == 0) {
        return -1;
    }

    fd = linux_syscall4(LINUX_SYS_OPENAT, LINUX_AT_FDCWD, (long)"/proc", LINUX_O_RDONLY | LINUX_O_DIRECTORY, 0);
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

        while (offset < (unsigned long)bytes && count < entry_capacity) {
            struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + offset);
            const char *name = entry->d_name;

            if (linux_is_digit_string(name)) {
                char proc_dir[1024];
                char status_path[1024];

                entries_out[count].pid = linux_parse_pid_value(name);
                entries_out[count].ppid = 0;
                entries_out[count].uid = 0;
                entries_out[count].rss_kb = 0;
                linux_copy_string(entries_out[count].state, sizeof(entries_out[count].state), "?");
                linux_copy_string(entries_out[count].user, sizeof(entries_out[count].user), "?");
                linux_copy_string(entries_out[count].name, sizeof(entries_out[count].name), name);

                if (linux_join_path("/proc", name, proc_dir, sizeof(proc_dir)) == 0 &&
                    linux_join_path(proc_dir, "status", status_path, sizeof(status_path)) == 0) {
                    linux_load_process_status(status_path, &entries_out[count]);
                }

                count += 1;
            }

            offset += entry->d_reclen;
        }
    }

    linux_syscall1(LINUX_SYS_CLOSE, fd);
    *count_out = count;
    return 0;
}
