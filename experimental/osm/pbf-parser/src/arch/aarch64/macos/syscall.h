#ifndef NEWOS_ARCH_AARCH64_MACOS_SYSCALL_H
#define NEWOS_ARCH_AARCH64_MACOS_SYSCALL_H

#define DARWIN_SYS_EXIT 1
#define DARWIN_SYS_READ 3
#define DARWIN_SYS_WRITE 4
#define DARWIN_SYS_OPEN 5
#define DARWIN_SYS_CLOSE 6
#define DARWIN_SYS_UNLINK 10
#define DARWIN_SYS_GETPID 20
#define DARWIN_SYS_GETUID 24
#define DARWIN_SYS_ACCESS 33
#define DARWIN_SYS_SYNC 36
#define DARWIN_SYS_GETGID 47
#define DARWIN_SYS_IOCTL 54
#define DARWIN_SYS_READLINK 58
#define DARWIN_SYS_GETGROUPS 79
#define DARWIN_SYS_SELECT 93
#define DARWIN_SYS_FSYNC 95
#define DARWIN_SYS_GETTIMEOFDAY 116
#define DARWIN_SYS_MKDIR 136
#define DARWIN_SYS_RMDIR 137
#define DARWIN_SYS_UTIMES 138
#define DARWIN_SYS_MMAP 197
#define DARWIN_SYS_LSEEK 199
#define DARWIN_SYS_FTRUNCATE 201
#define DARWIN_SYS_STAT64 338
#define DARWIN_SYS_LSTAT64 340
#define DARWIN_SYS_BSDTHREAD_CREATE 360
#define DARWIN_SYS_BSDTHREAD_TERMINATE 361

static inline long darwin_syscall0(long number) {
    register long x16 __asm__("x16") = number;
    register long x0 __asm__("x0");

    __asm__ volatile("svc #0x80\n\tcneg %[ret], %[ret], cs" : [ret] "=r"(x0), "+r"(x16) : : "memory", "cc");
    return x0;
}

static inline long darwin_syscall1(long number, long arg0) {
    register long x16 __asm__("x16") = number;
    register long x0 __asm__("x0") = arg0;

    __asm__ volatile("svc #0x80\n\tcneg %[ret], %[ret], cs" : [ret] "+r"(x0), "+r"(x16) : : "memory", "cc");
    return x0;
}

static inline long darwin_syscall2(long number, long arg0, long arg1) {
    register long x16 __asm__("x16") = number;
    register long x0 __asm__("x0") = arg0;
    register long x1 __asm__("x1") = arg1;

    __asm__ volatile("svc #0x80\n\tcneg %[ret], %[ret], cs" : [ret] "+r"(x0), "+r"(x1), "+r"(x16) : : "memory", "cc");
    return x0;
}

static inline long darwin_syscall3(long number, long arg0, long arg1, long arg2) {
    register long x16 __asm__("x16") = number;
    register long x0 __asm__("x0") = arg0;
    register long x1 __asm__("x1") = arg1;
    register long x2 __asm__("x2") = arg2;

    __asm__ volatile("svc #0x80\n\tcneg %[ret], %[ret], cs" : [ret] "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x16) : : "memory", "cc");
    return x0;
}

static inline long darwin_syscall4(long number, long arg0, long arg1, long arg2, long arg3) {
    register long x16 __asm__("x16") = number;
    register long x0 __asm__("x0") = arg0;
    register long x1 __asm__("x1") = arg1;
    register long x2 __asm__("x2") = arg2;
    register long x3 __asm__("x3") = arg3;

    __asm__ volatile("svc #0x80\n\tcneg %[ret], %[ret], cs" : [ret] "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x16) : : "memory", "cc");
    return x0;
}

static inline long darwin_syscall5(long number, long arg0, long arg1, long arg2, long arg3, long arg4) {
    register long x16 __asm__("x16") = number;
    register long x0 __asm__("x0") = arg0;
    register long x1 __asm__("x1") = arg1;
    register long x2 __asm__("x2") = arg2;
    register long x3 __asm__("x3") = arg3;
    register long x4 __asm__("x4") = arg4;

    __asm__ volatile("svc #0x80\n\tcneg %[ret], %[ret], cs" : [ret] "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x16) : : "memory", "cc");
    return x0;
}

static inline long darwin_syscall6(long number, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5) {
    register long x16 __asm__("x16") = number;
    register long x0 __asm__("x0") = arg0;
    register long x1 __asm__("x1") = arg1;
    register long x2 __asm__("x2") = arg2;
    register long x3 __asm__("x3") = arg3;
    register long x4 __asm__("x4") = arg4;
    register long x5 __asm__("x5") = arg5;

    __asm__ volatile("svc #0x80\n\tcneg %[ret], %[ret], cs" : [ret] "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x16) : : "memory", "cc");
    return x0;
}

#endif
