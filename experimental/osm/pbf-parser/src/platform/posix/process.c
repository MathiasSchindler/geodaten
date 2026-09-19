#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#if defined(__linux__)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#endif
#define _POSIX_C_SOURCE 200809L

#include "platform.h"
#include "common.h"
#include "signal_util.h"

#include <limits.h>
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <sys/ioctl.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/klog.h>
#include <sys/reboot.h>
#endif
#if defined(__APPLE__)
int initgroups(const char *name, int basegid);
#endif

extern char **environ;

typedef struct {
    const char *name;
    int value;
} PosixSignalEntry;

static int posix_is_env_name_start(char ch) {
    return ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            ch == '_');
}

static int posix_is_env_name_char(char ch) {
    return posix_is_env_name_start(ch) || (ch >= '0' && ch <= '9');
}

static int posix_is_valid_env_name(const char *name) {
    size_t i = 0U;

    if (name == NULL || !posix_is_env_name_start(name[0])) {
        return 0;
    }

    for (i = 1U; name[i] != '\0'; ++i) {
        if (!posix_is_env_name_char(name[i])) {
            return 0;
        }
    }

    return 1;
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

static void posix_close_child_fds(void) {
    long max_fd = sysconf(_SC_OPEN_MAX);
    int fd;

#if defined(__linux__) && defined(SYS_close_range)
    if (syscall(SYS_close_range, (unsigned int)(STDERR_FILENO + 1), ~0U, 0) == 0) {
        return;
    }
#elif defined(__FreeBSD__)
    closefrom(STDERR_FILENO + 1);
    return;
#endif

    if (max_fd < 0 || max_fd > 65536L) {
        max_fd = 1024L;
    }

    for (fd = STDERR_FILENO + 1; fd < max_fd; ++fd) {
        (void)close(fd);
    }
}

static int posix_clear_supplementary_groups(const char *username, gid_t target_gid) {
#if defined(__MSYS__)
    (void)username;
    (void)target_gid;
    errno = ENOSYS;
    return -1;
#else
    if (username != NULL && username[0] != '\0') {
        return initgroups(username, target_gid);
    }
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
    return setgroups(0, NULL);
#else
    errno = ENOSYS;
    return -1;
#endif
#endif
}

static const PosixSignalEntry POSIX_SIGNAL_TABLE[] = {
#ifdef SIGHUP
    { "HUP", SIGHUP },
#endif
#ifdef SIGINT
    { "INT", SIGINT },
#endif
#ifdef SIGQUIT
    { "QUIT", SIGQUIT },
#endif
#ifdef SIGILL
    { "ILL", SIGILL },
#endif
#ifdef SIGTRAP
    { "TRAP", SIGTRAP },
#endif
#ifdef SIGABRT
    { "ABRT", SIGABRT },
#endif
#ifdef SIGBUS
    { "BUS", SIGBUS },
#endif
#ifdef SIGFPE
    { "FPE", SIGFPE },
#endif
#ifdef SIGKILL
    { "KILL", SIGKILL },
#endif
#ifdef SIGUSR1
    { "USR1", SIGUSR1 },
#endif
#ifdef SIGSEGV
    { "SEGV", SIGSEGV },
#endif
#ifdef SIGUSR2
    { "USR2", SIGUSR2 },
#endif
#ifdef SIGPIPE
    { "PIPE", SIGPIPE },
#endif
#ifdef SIGALRM
    { "ALRM", SIGALRM },
#endif
#ifdef SIGTERM
    { "TERM", SIGTERM },
#endif
#ifdef SIGCHLD
    { "CHLD", SIGCHLD },
#endif
#ifdef SIGCONT
    { "CONT", SIGCONT },
#endif
#ifdef SIGSTOP
    { "STOP", SIGSTOP },
#endif
#ifdef SIGTSTP
    { "TSTP", SIGTSTP },
#endif
#ifdef SIGTTIN
    { "TTIN", SIGTTIN },
#endif
#ifdef SIGTTOU
    { "TTOU", SIGTTOU },
#endif
};

static void posix_reset_child_signals(void) {
#ifdef SIGINT
    (void)signal(SIGINT, SIG_DFL);
#endif
#ifdef SIGQUIT
    (void)signal(SIGQUIT, SIG_DFL);
#endif
#ifdef SIGPIPE
    (void)signal(SIGPIPE, SIG_DFL);
#endif
}

int platform_parse_signal_name(const char *text, int *signal_out) {
    unsigned long long numeric = 0;
    size_t i;

    if (text == NULL || signal_out == NULL || text[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (rt_parse_uint(text, &numeric) == 0) {
        *signal_out = (int)numeric;
        return 0;
    }

    for (i = 0; i < sizeof(POSIX_SIGNAL_TABLE) / sizeof(POSIX_SIGNAL_TABLE[0]); ++i) {
        if (signal_name_matches(text, POSIX_SIGNAL_TABLE[i].name)) {
            *signal_out = POSIX_SIGNAL_TABLE[i].value;
            return 0;
        }
    }

    errno = EINVAL;
    return -1;
}

const char *platform_signal_name(int signal_number) {
    size_t i;

    for (i = 0; i < sizeof(POSIX_SIGNAL_TABLE) / sizeof(POSIX_SIGNAL_TABLE[0]); ++i) {
        if (POSIX_SIGNAL_TABLE[i].value == signal_number) {
            return POSIX_SIGNAL_TABLE[i].name;
        }
    }

    return "UNKNOWN";
}

void platform_write_signal_list(int fd) {
    size_t i;

    for (i = 0; i < sizeof(POSIX_SIGNAL_TABLE) / sizeof(POSIX_SIGNAL_TABLE[0]); ++i) {
        if (i > 0) {
            (void)platform_write(fd, " ", 1U);
        }
        (void)platform_write(fd, POSIX_SIGNAL_TABLE[i].name, rt_strlen(POSIX_SIGNAL_TABLE[i].name));
    }
    (void)platform_write(fd, "\n", 1U);
}

_Static_assert(sizeof(struct termios) <= PLATFORM_TERMINAL_STATE_CAPACITY, "PlatformTerminalState is too small");

const char *platform_getenv(const char *name) {
    if (!posix_is_valid_env_name(name)) {
        errno = EINVAL;
        return NULL;
    }

    return getenv(name);
}

const char *platform_getenv_entry(size_t index) {
    size_t current_index = 0;
    char **current = environ;

    while (current != NULL && *current != NULL) {
        if (current_index == index) {
            return *current;
        }
        current += 1;
        current_index += 1;
    }

    return NULL;
}

int platform_setenv(const char *name, const char *value, int overwrite) {
    if (!posix_is_valid_env_name(name)) {
        errno = EINVAL;
        return -1;
    }

    return setenv(name, value != NULL ? value : "", overwrite);
}

int platform_unsetenv(const char *name) {
    if (!posix_is_valid_env_name(name)) {
        errno = EINVAL;
        return -1;
    }

    return unsetenv(name);
}

int platform_clearenv(void) {
    static char *empty_environment[] = { NULL };

    environ = empty_environment;
    return 0;
}

int platform_isatty(int fd) {
    return isatty(fd) ? 1 : 0;
}

int platform_get_terminal_size(int fd, unsigned int *rows_out, unsigned int *columns_out) {
    struct winsize winsize;

    if (ioctl(fd, TIOCGWINSZ, &winsize) != 0 || (winsize.ws_row == 0 && winsize.ws_col == 0)) {
        errno = ENOTTY;
        return -1;
    }

    if (rows_out != NULL) {
        *rows_out = (unsigned int)winsize.ws_row;
    }
    if (columns_out != NULL) {
        *columns_out = (unsigned int)winsize.ws_col;
    }

    return 0;
}

int platform_get_process_id(void) {
    return (int)getpid();
}

long platform_read_kernel_log(char *buffer, size_t buffer_size, int clear_after_read) {
    if (buffer == NULL || buffer_size == 0U) {
        errno = EINVAL;
        return -1;
    }

#ifndef __linux__
    (void)clear_after_read;
#endif

#ifdef __linux__
    {
        int action = clear_after_read ? 4 : 3;
        int bytes = klogctl(action, buffer, (int)(buffer_size - 1U));
        if (bytes >= 0) {
            buffer[bytes] = '\0';
            return bytes;
        }
    }
#endif

    {
        const char *fallbacks[] = { "/var/run/dmesg.boot", "/var/log/dmesg" };
        size_t i;

        for (i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); ++i) {
            int fd = open(fallbacks[i], O_RDONLY);
            if (fd >= 0) {
                ssize_t bytes = read(fd, buffer, buffer_size - 1U);
                close(fd);
                if (bytes < 0) {
                    return -1;
                }
                buffer[bytes] = '\0';
                return bytes;
            }
        }
    }

    errno = ENOSYS;
    return -1;
}

int platform_clear_kernel_log(void) {
#ifdef __linux__
    return klogctl(5, NULL, 0) < 0 ? -1 : 0;
#else
    errno = ENOSYS;
    return -1;
#endif
}

int platform_set_console_log_level(int level) {
#ifdef __linux__
    return klogctl(8, NULL, level) < 0 ? -1 : 0;
#else
    (void)level;
    errno = ENOSYS;
    return -1;
#endif
}

int platform_shutdown_system(int action) {
#ifdef __linux__
    int command = RB_POWER_OFF;

    if (action == PLATFORM_SHUTDOWN_REBOOT) {
        command = RB_AUTOBOOT;
    } else if (action == PLATFORM_SHUTDOWN_HALT) {
        command = RB_HALT_SYSTEM;
    }

    (void)platform_sync_all();
    return reboot(command) < 0 ? -1 : 0;
#else
    (void)action;
    errno = ENOSYS;
    return -1;
#endif
}

int platform_random_bytes(unsigned char *buffer, size_t count) {
    size_t offset = 0;
    int fd;

    if (buffer == NULL && count != 0U) {
        errno = EINVAL;
        return -1;
    }
    if (count == 0U) {
        return 0;
    }

#ifdef O_CLOEXEC
    fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
#else
    fd = open("/dev/urandom", O_RDONLY);
#endif
    if (fd < 0) {
        return -1;
    }
#ifndef O_CLOEXEC
    (void)posix_mark_fd_cloexec(fd);
#endif

    while (offset < count) {
        ssize_t bytes = read(fd, buffer + offset, count - offset);
        if (bytes <= 0) {
            close(fd);
            errno = EIO;
            return -1;
        }
        offset += (size_t)bytes;
    }

    close(fd);
    return 0;
}

static void platform_make_raw_termios(struct termios *raw) {
    if (raw == NULL) {
        return;
    }

    raw->c_iflag &= ~(tcflag_t)(
#ifdef IGNBRK
        IGNBRK |
#endif
#ifdef BRKINT
        BRKINT |
#endif
#ifdef PARMRK
        PARMRK |
#endif
#ifdef ISTRIP
        ISTRIP |
#endif
#ifdef INLCR
        INLCR |
#endif
#ifdef IGNCR
        IGNCR |
#endif
#ifdef ICRNL
        ICRNL |
#endif
#ifdef IXON
        IXON |
#endif
        0);
    raw->c_lflag &= ~(tcflag_t)(
#ifdef ECHO
        ECHO |
#endif
#ifdef ECHONL
        ECHONL |
#endif
#ifdef ICANON
        ICANON |
#endif
#ifdef IEXTEN
        IEXTEN |
#endif
        0);
    raw->c_cflag &= ~(tcflag_t)(
#ifdef CSIZE
        CSIZE |
#endif
#ifdef PARENB
        PARENB |
#endif
        0);
#ifdef CS8
    raw->c_cflag |= CS8;
#endif
#ifdef VMIN
    raw->c_cc[VMIN] = 1;
#endif
#ifdef VTIME
    raw->c_cc[VTIME] = 0;
#endif
}

int platform_terminal_get_mode(int fd, PlatformTerminalMode *mode_out) {
    struct termios term;
    struct winsize window_size;

    if (mode_out == NULL) {
        errno = EINVAL;
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

    memset(&window_size, 0, sizeof(window_size));
    if (ioctl(fd, TIOCGWINSZ, &window_size) == 0) {
        mode_out->rows = (unsigned int)window_size.ws_row;
        mode_out->columns = (unsigned int)window_size.ws_col;
    }

    return 0;
}

int platform_terminal_set_mode(int fd, const PlatformTerminalMode *mode, unsigned int change_mask) {
    struct termios term;

    if (mode == NULL) {
        errno = EINVAL;
        return -1;
    }

    if ((change_mask & (PLATFORM_TERMINAL_ECHO |
                        PLATFORM_TERMINAL_ICANON |
                        PLATFORM_TERMINAL_ISIG |
                        PLATFORM_TERMINAL_IXON |
                        PLATFORM_TERMINAL_OPOST)) != 0U) {
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
#ifdef TIOCSWINSZ
        struct winsize window_size;

        memset(&window_size, 0, sizeof(window_size));
        if (ioctl(fd, TIOCGWINSZ, &window_size) != 0) {
            return -1;
        }
        if ((change_mask & PLATFORM_TERMINAL_ROWS) != 0U) {
            window_size.ws_row = (unsigned short)mode->rows;
        }
        if ((change_mask & PLATFORM_TERMINAL_COLUMNS) != 0U) {
            window_size.ws_col = (unsigned short)mode->columns;
        }
        if (ioctl(fd, TIOCSWINSZ, &window_size) != 0) {
            return -1;
        }
#else
        errno = ENOTSUP;
        return -1;
#endif
    }

    return 0;
}

int platform_terminal_enable_raw_mode(int fd, PlatformTerminalState *state_out) {
    unsigned char saved_bytes[PLATFORM_TERMINAL_STATE_CAPACITY];
    unsigned char raw_bytes[PLATFORM_TERMINAL_STATE_CAPACITY];
    struct termios *saved = (struct termios *)saved_bytes;
    struct termios *raw = (struct termios *)raw_bytes;

    if (state_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (tcgetattr(fd, saved) != 0) {
        return -1;
    }

    memset(state_out, 0, PLATFORM_TERMINAL_STATE_CAPACITY);
    memcpy(state_out, saved_bytes, PLATFORM_TERMINAL_STATE_CAPACITY);

    memcpy(raw_bytes, saved_bytes, PLATFORM_TERMINAL_STATE_CAPACITY);
    platform_make_raw_termios(raw);

    return tcsetattr(fd, TCSANOW, raw);
}

int platform_terminal_restore_mode(int fd, const PlatformTerminalState *state) {
    unsigned char saved_bytes[PLATFORM_TERMINAL_STATE_CAPACITY];
    struct termios *saved = (struct termios *)saved_bytes;

    if (state == NULL) {
        errno = EINVAL;
        return -1;
    }

    memcpy(saved_bytes, state, PLATFORM_TERMINAL_STATE_CAPACITY);
    return tcsetattr(fd, TCSANOW, saved);
}

int platform_create_pipe(int pipe_fds[2]) {
    if (pipe_fds == NULL) {
        errno = EINVAL;
        return -1;
    }
#if defined(__linux__)
    if (pipe2(pipe_fds, O_CLOEXEC) == 0) {
        return 0;
    }
    if (errno != ENOSYS && errno != EINVAL) {
        return -1;
    }
#endif
    if (pipe(pipe_fds) != 0) {
        return -1;
    }
    if (posix_mark_fd_cloexec(pipe_fds[0]) != 0 || posix_mark_fd_cloexec(pipe_fds[1]) != 0) {
        int saved_errno = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        pipe_fds[0] = -1;
        pipe_fds[1] = -1;
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int platform_drop_privileges(const char *username, const char *groupname) {
    uid_t current_uid = getuid();
    gid_t current_gid = getgid();
    uid_t target_uid = current_uid;
    gid_t target_gid = current_gid;
    const char *group_user_name = NULL;
    unsigned int lookup_gid = 0U;
    unsigned long long numeric = 0ULL;

    if ((username == NULL || username[0] == '\0') && (groupname == NULL || groupname[0] == '\0')) {
        return 0;
    }

    if (groupname != NULL && groupname[0] != '\0') {
        if (platform_lookup_group(groupname, &lookup_gid) != 0) {
            return -1;
        }
        target_gid = (gid_t)lookup_gid;
    }

    if (username != NULL && username[0] != '\0') {
        if (rt_parse_uint(username, &numeric) == 0) {
            struct passwd *pw = getpwuid((uid_t)numeric);
            target_uid = (uid_t)numeric;
            if (groupname == NULL || groupname[0] == '\0') {
                if (pw == NULL) {
                    errno = ENOENT;
                    return -1;
                }
                target_gid = pw->pw_gid;
            }
            if (pw != NULL && pw->pw_name != NULL && pw->pw_name[0] != '\0') {
                group_user_name = pw->pw_name;
            }
        } else {
            PlatformIdentity identity;
            if (platform_lookup_identity(username, &identity) != 0) {
                return -1;
            }
            target_uid = (uid_t)identity.uid;
            if (groupname == NULL || groupname[0] == '\0') {
                target_gid = (gid_t)identity.gid;
            }
            group_user_name = username;
        }
    }

    if (current_uid == 0U) {
        if (posix_clear_supplementary_groups(group_user_name, target_gid) != 0) {
            return -1;
        }
    }
    if (target_gid != current_gid) {
        if (setgid(target_gid) != 0) {
            return -1;
        }
    }

    if (target_uid != current_uid) {
        if (setuid(target_uid) != 0) {
            return -1;
        }
    }

    return getuid() == target_uid && getgid() == target_gid ? 0 : -1;
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
    pid_t pid;

    if (argv == NULL || argv[0] == NULL || pid_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        int fd;

        posix_reset_child_signals();

        if (working_directory != NULL && working_directory[0] != '\0') {
            if (chdir(working_directory) != 0) {
                _exit(126);
            }
        }
        if (platform_drop_privileges(drop_user, drop_group) != 0) {
            _exit(126);
        }

        if (input_path != NULL) {
            fd = open(input_path, O_RDONLY);
            if (fd < 0) {
                _exit(126);
            }
            if (fd != STDIN_FILENO) {
                if (dup2(fd, STDIN_FILENO) < 0) {
                    _exit(126);
                }
                close(fd);
            }
        } else if (stdin_fd >= 0 && stdin_fd != STDIN_FILENO) {
            if (dup2(stdin_fd, STDIN_FILENO) < 0) {
                _exit(126);
            }
        }

        if (output_path != NULL) {
            int flags = O_WRONLY | O_CREAT | (output_append ? O_APPEND : O_TRUNC);
            fd = open(output_path, flags, 0644);
            if (fd < 0) {
                _exit(126);
            }
            if (fd != STDOUT_FILENO) {
                if (dup2(fd, STDOUT_FILENO) < 0) {
                    _exit(126);
                }
                close(fd);
            }
        } else if (stdout_fd >= 0 && stdout_fd != STDOUT_FILENO) {
            if (dup2(stdout_fd, STDOUT_FILENO) < 0) {
                _exit(126);
            }
        }

        if (output_path != NULL || stdout_fd >= 0) {
            if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
                _exit(126);
            }
        }

        if (stdin_fd > STDERR_FILENO) {
            close(stdin_fd);
        }
        if (stdout_fd > STDERR_FILENO) {
            close(stdout_fd);
        }

        posix_close_child_fds();

        execvp(argv[0], argv);
        _exit(127);
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
    return platform_spawn_process_ex(argv, stdin_fd, stdout_fd, input_path, output_path, output_append, NULL, NULL, NULL, pid_out);
}

int platform_send_signal(int pid, int signal_number) {
    return kill((pid_t)pid, signal_number);
}

int platform_ignore_signal(int signal_number) {
    return signal(signal_number, SIG_IGN) == SIG_ERR ? -1 : 0;
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

static int sleep_uninterrupted(unsigned long long milliseconds) {
    struct timespec req;
    struct timespec rem;

    req.tv_sec = (time_t)(milliseconds / 1000ULL);
    req.tv_nsec = (long)((milliseconds % 1000ULL) * 1000000ULL);

    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) {
            return -1;
        }
        req = rem;
    }

    return 0;
}

int platform_trace_syscalls(char *const argv[], PlatformSyscallTraceCallback callback, void *user_data, int *exit_status_out) {
    (void)argv;
    (void)callback;
    (void)user_data;
    if (exit_status_out != 0) *exit_status_out = 1;
    return -1;
}

int platform_wait_process(int pid, int *exit_status_out) {
    int status;

    if (exit_status_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (waitpid((pid_t)pid, &status, 0) < 0) {
        return -1;
    }

    *exit_status_out = decode_wait_status(status);

    return 0;
}

int platform_poll_process_exit(int pid, int *finished_out, int *exit_status_out) {
    int status = 0;
    pid_t waited;

    if (finished_out == NULL || exit_status_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    waited = waitpid((pid_t)pid, &status, WNOHANG);
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
    const unsigned long long poll_milliseconds = 50ULL;

    if (exit_status_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        int status = 0;
        pid_t waited = waitpid((pid_t)pid, &status, WNOHANG);

        if (waited == (pid_t)pid) {
            *exit_status_out = (timed_out && !preserve_status) ? 124 : decode_wait_status(status);
            return 0;
        }

        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (!timed_out && elapsed >= timeout_milliseconds) {
            (void)platform_send_signal(pid, signal_number);
            timed_out = 1;
            after_signal = 0;
        } else if (timed_out && kill_after_milliseconds > 0 && after_signal >= kill_after_milliseconds) {
            (void)platform_send_signal(pid, SIGKILL);
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
            if (sleep_uninterrupted(sleep_for) != 0) {
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

static void init_process_entry(PlatformProcessEntry *entry, int pid, const char *fallback_name) {
    if (entry == NULL) {
        return;
    }

    entry->pid = pid;
    entry->ppid = 0;
    entry->uid = 0;
    entry->rss_kb = 0;
    posix_copy_string(entry->state, sizeof(entry->state), "?");
    posix_copy_string(entry->user, sizeof(entry->user), "?");
    posix_copy_string(entry->name, sizeof(entry->name), fallback_name != NULL ? fallback_name : "?");
}

#ifndef __APPLE__
static void fill_username(char *buffer, size_t buffer_size, unsigned int uid) {
    struct passwd *pw = getpwuid((uid_t)uid);

    if (pw != NULL && pw->pw_name != NULL && pw->pw_name[0] != '\0') {
        posix_copy_string(buffer, buffer_size, pw->pw_name);
    } else {
        (void)snprintf(buffer, buffer_size, "%u", uid);
    }
}

static void load_status_file(const char *status_path, PlatformProcessEntry *entry) {
    FILE *fp = fopen(status_path, "r");
    char line[512];

    if (fp == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "Name:", 5) == 0) {
            char name[PLATFORM_NAME_CAPACITY];
            if (sscanf(line + 5, " %255s", name) == 1) {
                posix_copy_string(entry->name, sizeof(entry->name), name);
            }
        } else if (strncmp(line, "State:", 6) == 0) {
            char state[sizeof(entry->state)];
            if (sscanf(line + 6, " %15s", state) == 1) {
                posix_copy_string(entry->state, sizeof(entry->state), state);
            }
        } else if (strncmp(line, "PPid:", 5) == 0) {
            int ppid = 0;
            if (sscanf(line + 5, " %d", &ppid) == 1) {
                entry->ppid = ppid;
            }
        } else if (strncmp(line, "Uid:", 4) == 0) {
            unsigned int uid = 0;
            if (sscanf(line + 4, " %u", &uid) == 1) {
                entry->uid = uid;
                fill_username(entry->user, sizeof(entry->user), uid);
            }
        } else if (strncmp(line, "VmRSS:", 6) == 0) {
            unsigned long long rss = 0;
            if (sscanf(line + 6, " %llu", &rss) == 1) {
                entry->rss_kb = rss;
            }
        }
    }

    fclose(fp);
}
#endif

int platform_list_processes(PlatformProcessEntry *entries_out, size_t entry_capacity, size_t *count_out) {
    size_t count = 0;

    if (entries_out == NULL || count_out == NULL) {
        errno = EINVAL;
        return -1;
    }

#ifdef __APPLE__
    {
        FILE *pipe = popen("/bin/ps -ww -axo pid=,ppid=,uid=,user=,state=,rss=,comm=", "r");
        char line[1024];

        if (pipe == NULL) {
            return -1;
        }

        while (fgets(line, sizeof(line), pipe) != NULL && count < entry_capacity) {
            int pid = 0;
            int ppid = 0;
            unsigned int uid = 0;
            unsigned long long rss = 0;
            char user[PLATFORM_NAME_CAPACITY];
            char state[16];
            char name[PLATFORM_NAME_CAPACITY];

            user[0] = '\0';
            state[0] = '\0';
            name[0] = '\0';

            if (sscanf(line, " %d %d %u %255s %15s %llu %255[^\n]", &pid, &ppid, &uid, user, state, &rss, name) == 7 && pid > 0) {
                init_process_entry(&entries_out[count], pid, name);
                entries_out[count].ppid = ppid;
                entries_out[count].uid = uid;
                entries_out[count].rss_kb = rss;
                posix_copy_string(entries_out[count].user, sizeof(entries_out[count].user), user);
                posix_copy_string(entries_out[count].state, sizeof(entries_out[count].state), state);
                count += 1;
            }
        }

        (void)pclose(pipe);
    }
#else
    {
        DIR *dir = opendir("/proc");
        struct dirent *de;

        if (dir == NULL) {
            return -1;
        }

        while ((de = readdir(dir)) != NULL && count < entry_capacity) {
            char proc_dir[1024];
            char status_path[1024];

            if (!posix_is_digit_string(de->d_name)) {
                continue;
            }

            init_process_entry(&entries_out[count], posix_parse_pid_value(de->d_name), de->d_name);
            if (posix_join_path("/proc", de->d_name, proc_dir, sizeof(proc_dir)) == 0 &&
                posix_join_path(proc_dir, "status", status_path, sizeof(status_path)) == 0) {
                load_status_file(status_path, &entries_out[count]);
            }
            count += 1;
        }

        closedir(dir);
    }
#endif

    *count_out = count;
    return 0;
}
