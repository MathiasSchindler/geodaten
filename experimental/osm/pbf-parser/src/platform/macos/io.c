#include "platform.h"
#include "runtime.h"
#include "syscall.h"

extern int fork(void);
extern int execvp(const char *file, char *const argv[]);
extern int waitpid(int pid, int *status, int options);
extern int dup2(int oldfd, int newfd);
extern int chdir(const char *path);
extern void _exit(int status);

#define DARWIN_PROT_READ 1
#define DARWIN_PROT_WRITE 2
#define DARWIN_MAP_PRIVATE 2
#define DARWIN_MAP_ANONYMOUS 0x1000
#define DARWIN_O_WRONLY 0x0001
#define DARWIN_O_APPEND 0x0008
#define DARWIN_O_CREAT 0x0200
#define DARWIN_O_TRUNC 0x0400
#define DARWIN_O_EXCL 0x0800
#define DARWIN_SEEK_SET 0
#define DARWIN_SEEK_CUR 1
#define DARWIN_SEEK_END 2

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

int platform_open_read(const char *path) {
    long fd;

    if (path == 0 || (path[0] == '-' && path[1] == '\0')) {
        return 0;
    }
    fd = darwin_syscall3(DARWIN_SYS_OPEN, (long)path, 0, 0);
    return fd < 0 ? -1 : (int)fd;
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
    long native_whence = DARWIN_SEEK_SET;
    long result;

    if (whence == PLATFORM_SEEK_CUR) {
        native_whence = DARWIN_SEEK_CUR;
    } else if (whence == PLATFORM_SEEK_END) {
        native_whence = DARWIN_SEEK_END;
    }

    result = darwin_syscall3(DARWIN_SYS_LSEEK, (long)fd, (long)offset, native_whence);
    return result < 0 ? -1 : (long long)result;
}

int platform_close(int fd) {
    if (fd == 0 || fd == 1) {
        return 0;
    }
    return darwin_syscall1(DARWIN_SYS_CLOSE, (long)fd) < 0 ? -1 : 0;
}

int platform_make_directory(const char *path, unsigned int mode) {
    return darwin_syscall2(DARWIN_SYS_MKDIR, (long)path, (long)mode) < 0 ? -1 : 0;
}

int platform_remove_file(const char *path) {
    return darwin_syscall1(DARWIN_SYS_UNLINK, (long)path) < 0 ? -1 : 0;
}

int platform_remove_directory(const char *path) {
    return darwin_syscall1(DARWIN_SYS_RMDIR, (long)path) < 0 ? -1 : 0;
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

    if (argv == 0 || argv[0] == 0 || pid_out == 0) return -1;
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int fd;
        if (working_directory != 0 && working_directory[0] != '\0' && chdir(working_directory) != 0) _exit(126);
        if ((drop_user != 0 && drop_user[0] != '\0') || (drop_group != 0 && drop_group[0] != '\0')) _exit(126);
        if (input_path != 0) {
            fd = platform_open_read(input_path);
            if (fd < 0) _exit(126);
            if (fd != 0) { if (dup2(fd, 0) < 0) _exit(126); (void)platform_close(fd); }
        } else if (stdin_fd >= 0 && stdin_fd != 0) {
            if (dup2(stdin_fd, 0) < 0) _exit(126);
        }
        if (output_path != 0) {
            fd = platform_open_write_mode(output_path, 0644U, output_append ? 0 : 1);
            if (fd < 0) _exit(126);
            if (fd != 1) { if (dup2(fd, 1) < 0) _exit(126); (void)platform_close(fd); }
        } else if (stdout_fd >= 0 && stdout_fd != 1) {
            if (dup2(stdout_fd, 1) < 0) _exit(126);
        }
        if (output_path != 0 || stdout_fd >= 0) { if (dup2(1, 2) < 0) _exit(126); }
        if (stdin_fd > 2) (void)platform_close(stdin_fd);
        if (stdout_fd > 2) (void)platform_close(stdout_fd);
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
    if ((status & 0x7f) == 0) return (status >> 8) & 0xff;
    if ((status & 0x7f) != 0x7f) return 128 + (status & 0x7f);
    return 1;
}

int platform_wait_process(int pid, int *exit_status_out) {
    int status = 0;
    if (exit_status_out == 0) return -1;
    if (waitpid(pid, &status, 0) < 0) return -1;
    *exit_status_out = decode_wait_status(status);
    return 0;
}