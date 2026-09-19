#ifndef NEWOS_PLATFORM_LINUX_COMMON_H
#define NEWOS_PLATFORM_LINUX_COMMON_H

#include "platform.h"
#include "runtime.h"

#if defined(__x86_64__)
#include "../../arch/x86_64/linux/syscall.h"
#elif defined(__aarch64__)
#include "../../arch/aarch64/linux/syscall.h"
#else
#error "Unsupported Linux architecture for syscall definitions"
#endif

#define LINUX_AT_FDCWD (-100)
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100
#define LINUX_AT_REMOVEDIR 0x200
#define LINUX_AT_EMPTY_PATH 0x1000

#define LINUX_O_RDONLY 0
#define LINUX_O_WRONLY 1
#define LINUX_O_CREAT 0100
#define LINUX_O_EXCL 0200
#define LINUX_O_NOFOLLOW 0400000
#define LINUX_O_APPEND 02000
#define LINUX_O_TRUNC 01000
#define LINUX_O_CLOEXEC 02000000
#define LINUX_O_DIRECTORY 0200000

#define LINUX_PROT_READ 1
#define LINUX_PROT_WRITE 2
#define LINUX_MAP_PRIVATE 2
#define LINUX_MAP_ANONYMOUS 0x20

#define LINUX_CLONE_VM             0x00000100UL
#define LINUX_CLONE_FS             0x00000200UL
#define LINUX_CLONE_FILES          0x00000400UL
#define LINUX_CLONE_SIGHAND        0x00000800UL
#define LINUX_CLONE_THREAD         0x00010000UL
#define LINUX_CLONE_SYSVSEM        0x00040000UL
#define LINUX_CLONE_PARENT_SETTID  0x00100000UL
#define LINUX_CLONE_CHILD_CLEARTID 0x00200000UL
#define LINUX_CLONE_CHILD_SETTID   0x01000000UL

#define LINUX_FUTEX_WAIT 0
#define LINUX_FUTEX_WAKE 1
#define LINUX_FUTEX_PRIVATE_FLAG 128

#define LINUX_UTIME_OMIT 0x3ffffffeL
#define LINUX_UTIME_NOW  0x3fffffffL

#define LINUX_SIGHUP 1
#define LINUX_SIGINT 2
#define LINUX_SIGQUIT 3
#define LINUX_SIGILL 4
#define LINUX_SIGTRAP 5
#define LINUX_SIGABRT 6
#define LINUX_SIGBUS 7
#define LINUX_SIGFPE 8
#define LINUX_SIGKILL 9
#define LINUX_SIGUSR1 10
#define LINUX_SIGSEGV 11
#define LINUX_SIGUSR2 12
#define LINUX_SIGPIPE 13
#define LINUX_SIGALRM 14
#define LINUX_SIGTERM 15
#define LINUX_SIGCHLD 17
#define LINUX_SIGCONT 18
#define LINUX_SIGSTOP 19
#define LINUX_SIGTSTP 20
#define LINUX_SIGTTIN 21
#define LINUX_SIGTTOU 22
#define LINUX_SIG_DFL 0UL
#define LINUX_SIG_IGN 1UL

#define LINUX_EINTR 4
#define LINUX_EAGAIN 11
#define LINUX_EINVAL 22
#define LINUX_ENOSYS 38

#define LINUX_F_GETFD 1
#define LINUX_F_SETFD 2
#define LINUX_FD_CLOEXEC 1

#define LINUX_WNOHANG 1
#define LINUX_TCGETS 0x5401
#define LINUX_TCSETS 0x5402
#define LINUX_TIOCGWINSZ 0x5413
#define LINUX_TIOCSWINSZ 0x5414

#define LINUX_REBOOT_MAGIC1 0xfee1deadUL
#define LINUX_REBOOT_MAGIC2 0x28121969UL
#define LINUX_REBOOT_CMD_RESTART 0x1234567UL
#define LINUX_REBOOT_CMD_HALT 0xcdef0123UL
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedcUL

#define LINUX_ICANON 0x00000002U
#define LINUX_ISIG   0x00000001U
#define LINUX_ECHO 0x00000008U
#define LINUX_IEXTEN 0x00008000U
#define LINUX_BRKINT 0x00000002U
#define LINUX_INPCK  0x00000010U
#define LINUX_ISTRIP 0x00000020U
#define LINUX_ICRNL  0x00000100U
#define LINUX_IXON   0x00000400U
#define LINUX_OPOST  0x00000001U
#define LINUX_CS8    0x00000030U
#define LINUX_VTIME 5
#define LINUX_VMIN 6

#define LINUX_S_IFMT   0170000U
#define LINUX_S_IFIFO  0010000U
#define LINUX_S_IFCHR  0020000U
#define LINUX_S_IFDIR  0040000U
#define LINUX_S_IFBLK  0060000U
#define LINUX_S_IFREG  0100000U
#define LINUX_S_IFLNK  0120000U
#define LINUX_S_IFSOCK 0140000U

#define LINUX_SOCK_CLOEXEC LINUX_O_CLOEXEC

#define LINUX_S_IRUSR 0400U
#define LINUX_S_IWUSR 0200U
#define LINUX_S_IXUSR 0100U
#define LINUX_S_IRGRP 0040U
#define LINUX_S_IWGRP 0020U
#define LINUX_S_IXGRP 0010U
#define LINUX_S_IROTH 0004U
#define LINUX_S_IWOTH 0002U
#define LINUX_S_IXOTH 0001U

#if defined(__x86_64__)
struct linux_stat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned long st_nlink;
    unsigned int st_mode;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned int __pad0;
    unsigned long st_rdev;
    long st_size;
    long st_blksize;
    long st_blocks;
    long st_atime;
    unsigned long st_atime_nsec;
    long st_mtime;
    unsigned long st_mtime_nsec;
    long st_ctime;
    unsigned long st_ctime_nsec;
    long __unused[3];
};
#else
struct linux_stat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned long st_rdev;
    unsigned long __pad1;
    long st_size;
    int st_blksize;
    int __pad2;
    long st_blocks;
    long st_atime;
    unsigned long st_atime_nsec;
    long st_mtime;
    unsigned long st_mtime_nsec;
    long st_ctime;
    unsigned long st_ctime_nsec;
    unsigned int __unused4;
    unsigned int __unused5;
};
#endif

struct linux_dirent64 {
    unsigned long long d_ino;
    long long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

struct linux_timespec {
    long tv_sec;
    long tv_nsec;
};

struct linux_winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

struct linux_termios {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[19];
};

struct linux_sigaction {
    unsigned long handler;
    unsigned long flags;
    unsigned long restorer;
    unsigned long mask;
};

#define linux_copy_string rt_copy_string
#define linux_string_length rt_strlen
#define linux_unsigned_to_string rt_unsigned_to_string
#define linux_join_path rt_join_path
#define linux_is_digit_string rt_is_digit_string
#define linux_parse_pid_value rt_parse_pid_value
#define linux_trim_newline rt_trim_newline

#define copy_string rt_copy_string
#define string_length rt_strlen
#define unsigned_to_string rt_unsigned_to_string
#define join_path rt_join_path
#define is_digit_string rt_is_digit_string
#define parse_pid_value rt_parse_pid_value
#define trim_newline rt_trim_newline

static inline int linux_mark_fd_cloexec(int fd) {
    long flags;

    if (fd < 0) {
        return -1;
    }

    flags = linux_syscall2(LINUX_SYS_FCNTL, fd, LINUX_F_GETFD);
    if (flags < 0) {
        return -1;
    }
    if ((flags & LINUX_FD_CLOEXEC) != 0) {
        return 0;
    }
    return linux_syscall3(LINUX_SYS_FCNTL, fd, LINUX_F_SETFD, flags | LINUX_FD_CLOEXEC) < 0 ? -1 : 0;
}

#endif
