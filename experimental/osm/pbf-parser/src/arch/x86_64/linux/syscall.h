#ifndef NEWOS_ARCH_X86_64_LINUX_SYSCALL_H
#define NEWOS_ARCH_X86_64_LINUX_SYSCALL_H

#define LINUX_SYS_READ 0
#define LINUX_SYS_WRITE 1
#define LINUX_SYS_CLOSE 3
#define LINUX_SYS_POLL 7
#define LINUX_SYS_LSEEK 8
#define LINUX_SYS_MMAP 9
#define LINUX_SYS_MUNMAP 11
#define LINUX_SYS_IOCTL 16
#define LINUX_SYS_RT_SIGACTION 13
#define LINUX_SYS_NANOSLEEP 35
#define LINUX_SYS_GETPID 39
#define LINUX_SYS_SOCKET 41
#define LINUX_SYS_CONNECT 42
#define LINUX_SYS_ACCEPT 43
#define LINUX_SYS_SENDTO 44
#define LINUX_SYS_RECVFROM 45
#define LINUX_SYS_RECVMSG 47
#define LINUX_SYS_SHUTDOWN 48
#define LINUX_SYS_BIND 49
#define LINUX_SYS_LISTEN 50
#define LINUX_SYS_SETSOCKOPT 54
#define LINUX_SYS_FCNTL 72
#define LINUX_SYS_SYSLOG 103
#define LINUX_SYS_CLONE 56
#define LINUX_SYS_EXECVE 59
#define LINUX_SYS_EXIT 60
#define LINUX_SYS_WAIT4 61
#define LINUX_SYS_KILL 62
#define LINUX_SYS_UNAME 63
#define LINUX_SYS_PTRACE 101
#define LINUX_SYS_FUTEX 202
#define LINUX_SYS_FSYNC 74
#define LINUX_SYS_FDATASYNC 75
#define LINUX_SYS_TRUNCATE 76
#define LINUX_SYS_FTRUNCATE 77
#define LINUX_SYS_GETCWD 79
#define LINUX_SYS_CHDIR 80
#define LINUX_SYS_MOUNT 165
#define LINUX_SYS_UMOUNT2 166
#define LINUX_SYS_SYNC 162
#define LINUX_SYS_REBOOT 169
#define LINUX_SYS_SETHOSTNAME 170
#define LINUX_SYS_GETUID 102
#define LINUX_SYS_GETGID 104
#define LINUX_SYS_SETUID 105
#define LINUX_SYS_SETGID 106
#define LINUX_SYS_SETGROUPS 116
#define LINUX_SYS_STATFS 137
#define LINUX_SYS_CLOCK_GETTIME 228
#define LINUX_SYS_ACCEPT4 288
#define LINUX_SYS_PPOLL 271
#define LINUX_SYS_PIPE2 293
#define LINUX_SYS_GETRANDOM 318
#define LINUX_SYS_CLOSE_RANGE 436
#define LINUX_SYS_OPENAT 257
#define LINUX_SYS_MKDIRAT 258
#define LINUX_SYS_MKNODAT 259
#define LINUX_SYS_FCHOWNAT 260
#define LINUX_SYS_NEWFSTATAT 262
#define LINUX_SYS_UNLINKAT 263
#define LINUX_SYS_RENAMEAT 264
#define LINUX_SYS_LINKAT 265
#define LINUX_SYS_SYMLINKAT 266
#define LINUX_SYS_READLINKAT 267
#define LINUX_SYS_FCHMODAT 268
#define LINUX_SYS_UTIMENSAT 280
#define LINUX_SYS_DUP3 292
#define LINUX_SYS_GETDENTS64 217

long linux_syscall0(long number);
long linux_syscall1(long number, long arg0);
long linux_syscall2(long number, long arg0, long arg1);
long linux_syscall3(long number, long arg0, long arg1, long arg2);
long linux_syscall4(long number, long arg0, long arg1, long arg2, long arg3);
long linux_syscall5(long number, long arg0, long arg1, long arg2, long arg3, long arg4);
long linux_syscall6(long number, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5);
long linux_clone_thread(long flags, void *stack_top, int *child_tid, int (*entry)(void *), void *arg);

#endif
