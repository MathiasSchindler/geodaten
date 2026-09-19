#include "platform.h"
#include "common.h"

#define LINUX_THREAD_DEFAULT_STACK_SIZE (512U * 1024U)

typedef struct {
    PlatformThreadMain entry;
    void *arg;
    int result;
} LinuxThreadStart;

static int linux_thread_entry(void *arg) {
    LinuxThreadStart *start = (LinuxThreadStart *)arg;

    start->result = start->entry(start->arg);
    return 0;
}

static size_t linux_page_align(size_t value) {
    const size_t page_size = 4096U;

    return (value + page_size - 1U) & ~(page_size - 1U);
}

static int linux_futex_wait(volatile int *address, int expected) {
    long result = linux_syscall6(
        LINUX_SYS_FUTEX,
        (long)address,
        LINUX_FUTEX_WAIT,
        expected,
        0,
        0,
        0
    );
    return result < 0 && result != -LINUX_EAGAIN && result != -LINUX_EINTR ? -1 : 0;
}

static int linux_futex_wake(volatile int *address, int count) {
    long result = linux_syscall6(
        LINUX_SYS_FUTEX,
        (long)address,
        LINUX_FUTEX_WAKE,
        count,
        0,
        0,
        0
    );
    return result < 0 ? -1 : 0;
}

int platform_thread_start(PlatformThread *thread, PlatformThreadMain entry, void *arg, size_t stack_size) {
    unsigned char *stack;
    LinuxThreadStart *start;
    unsigned long flags;
    long tid;

    if (thread == 0 || entry == 0) {
        return -1;
    }
    if (stack_size == 0U) {
        stack_size = LINUX_THREAD_DEFAULT_STACK_SIZE;
    }
    stack_size = linux_page_align(stack_size);
    if (stack_size < sizeof(LinuxThreadStart) + 4096U) {
        stack_size = sizeof(LinuxThreadStart) + 4096U;
        stack_size = linux_page_align(stack_size);
    }
    stack = (unsigned char *)platform_allocate_pages(stack_size);
    if (stack == 0) {
        return -1;
    }
    start = (LinuxThreadStart *)stack;
    start->entry = entry;
    start->arg = arg;
    start->result = 0;
    thread->tid = -1;
    thread->clear_tid = 0;
    thread->stack = stack;
    thread->stack_size = stack_size;

    flags = LINUX_CLONE_VM |
            LINUX_CLONE_FS |
            LINUX_CLONE_FILES |
            LINUX_CLONE_SIGHAND |
            LINUX_CLONE_THREAD |
            LINUX_CLONE_SYSVSEM |
            LINUX_CLONE_PARENT_SETTID |
            LINUX_CLONE_CHILD_CLEARTID;
    tid = linux_clone_thread((long)flags, stack + stack_size, (int *)&thread->clear_tid, linux_thread_entry, start);
    if (tid < 0) {
        (void)linux_syscall2(LINUX_SYS_MUNMAP, (long)stack, (long)stack_size);
        thread->stack = 0;
        thread->stack_size = 0U;
        thread->clear_tid = 0;
        return -1;
    }
    thread->tid = (int)tid;
    return 0;
}

int platform_thread_join(PlatformThread *thread, int *result_out) {
    volatile int *clear_tid;

    if (thread == 0) {
        return -1;
    }
    clear_tid = &thread->clear_tid;
    for (;;) {
        int value = __atomic_load_n(clear_tid, __ATOMIC_ACQUIRE);
        if (value == 0) {
            break;
        }
        if (value > 0 && linux_futex_wait(clear_tid, value) != 0) {
            return -1;
        }
    }
    if (result_out != 0 && thread->stack != 0) {
        LinuxThreadStart *start = (LinuxThreadStart *)thread->stack;
        *result_out = start->result;
    }
    if (thread->stack != 0) {
        (void)linux_syscall2(LINUX_SYS_MUNMAP, (long)thread->stack, (long)thread->stack_size);
    }
    thread->stack = 0;
    thread->stack_size = 0U;
    thread->tid = 0;
    return 0;
}

void platform_mutex_init(PlatformMutex *mutex) {
    if (mutex != 0) {
        __atomic_store_n(&mutex->state, 0, __ATOMIC_RELEASE);
    }
}

void platform_mutex_lock(PlatformMutex *mutex) {
    int expected = 0;

    if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return;
    }
    for (;;) {
        int previous = __atomic_exchange_n(&mutex->state, 2, __ATOMIC_ACQUIRE);
        if (previous == 0) {
            return;
        }
        (void)linux_futex_wait(&mutex->state, 2);
    }
}

void platform_mutex_unlock(PlatformMutex *mutex) {
    if (__atomic_fetch_sub(&mutex->state, 1, __ATOMIC_RELEASE) != 1) {
        __atomic_store_n(&mutex->state, 0, __ATOMIC_RELEASE);
        (void)linux_futex_wake(&mutex->state, 1);
    }
}

void platform_semaphore_init(PlatformSemaphore *semaphore, int value) {
    if (semaphore != 0) {
        __atomic_store_n(&semaphore->count, value, __ATOMIC_RELEASE);
    }
}

void platform_semaphore_wait(PlatformSemaphore *semaphore) {
    for (;;) {
        int value = __atomic_load_n(&semaphore->count, __ATOMIC_ACQUIRE);

        while (value > 0) {
            int desired = value - 1;
            if (__atomic_compare_exchange_n(&semaphore->count, &value, desired, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                return;
            }
        }
        (void)linux_futex_wait(&semaphore->count, 0);
    }
}

void platform_semaphore_post(PlatformSemaphore *semaphore) {
    (void)__atomic_fetch_add(&semaphore->count, 1, __ATOMIC_RELEASE);
    (void)linux_futex_wake(&semaphore->count, 1);
}