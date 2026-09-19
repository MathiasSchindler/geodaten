#include "platform.h"
#include "runtime.h"

typedef void *pthread_t;

int pthread_create(pthread_t *thread, const void *attr, void *(*entry)(void *), void *arg);
int pthread_join(pthread_t thread, void **value_out);

typedef struct {
    pthread_t native;
    PlatformThreadMain entry;
    void *arg;
    int result;
} MacosThreadStart;

static void macos_thread_yield(void) {
    (void)platform_sleep_milliseconds(0ULL);
}

static void *macos_thread_trampoline(void *arg) {
    MacosThreadStart *start = (MacosThreadStart *)arg;

    start->result = start->entry(start->arg);
    return 0;
}

int platform_thread_start(PlatformThread *thread, PlatformThreadMain entry, void *arg, size_t stack_size) {
    MacosThreadStart *start;

    (void)stack_size;
    if (thread == 0 || entry == 0) {
        return -1;
    }

    start = (MacosThreadStart *)rt_malloc(sizeof(*start));
    if (start == 0) {
        return -1;
    }
    start->entry = entry;
    start->arg = arg;
    start->result = 0;

    thread->tid = 0;
    thread->clear_tid = 0;
    thread->stack = start;
    thread->stack_size = sizeof(*start);

    if (pthread_create(&start->native, 0, macos_thread_trampoline, start) != 0) {
        thread->clear_tid = 0;
        thread->stack = 0;
        thread->stack_size = 0U;
        rt_free(start);
        return -1;
    }
    thread->tid = 1;
    return 0;
}

int platform_thread_join(PlatformThread *thread, int *result_out) {
    MacosThreadStart *start;

    if (thread == 0 || thread->stack == 0) {
        return -1;
    }
    start = (MacosThreadStart *)thread->stack;
    if (pthread_join(start->native, 0) != 0) {
        return -1;
    }
    if (result_out != 0) {
        *result_out = start->result;
    }
    rt_free(start);
    thread->tid = 0;
    thread->stack = 0;
    thread->stack_size = 0U;
    return 0;
}

void platform_mutex_init(PlatformMutex *mutex) {
    if (mutex != 0) {
        __atomic_store_n(&mutex->state, 0, __ATOMIC_RELEASE);
    }
}

void platform_mutex_lock(PlatformMutex *mutex) {
    int expected;

    if (mutex == 0) {
        return;
    }
    for (;;) {
        expected = 0;
        if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return;
        }
        macos_thread_yield();
    }
}

void platform_mutex_unlock(PlatformMutex *mutex) {
    if (mutex != 0) {
        __atomic_store_n(&mutex->state, 0, __ATOMIC_RELEASE);
    }
}

void platform_semaphore_init(PlatformSemaphore *semaphore, int value) {
    if (semaphore != 0) {
        __atomic_store_n(&semaphore->count, value, __ATOMIC_RELEASE);
    }
}

void platform_semaphore_wait(PlatformSemaphore *semaphore) {
    if (semaphore == 0) {
        return;
    }
    for (;;) {
        int value = __atomic_load_n(&semaphore->count, __ATOMIC_ACQUIRE);

        while (value > 0) {
            int desired = value - 1;
            if (__atomic_compare_exchange_n(&semaphore->count, &value, desired, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                return;
            }
        }
        macos_thread_yield();
    }
}

void platform_semaphore_post(PlatformSemaphore *semaphore) {
    if (semaphore != 0) {
        (void)__atomic_fetch_add(&semaphore->count, 1, __ATOMIC_RELEASE);
    }
}