#include "platform.h"

#define WIN_INFINITE 0xffffffffUL
#define WIN_MEM_RELEASE 0x8000UL

typedef unsigned long (__stdcall *WindowsThreadEntry)(void *arg);
typedef int (__stdcall *WindowsWaitOnAddress)(
    volatile void *address, void *compare_address, size_t address_size,
    unsigned long milliseconds
);
typedef void (__stdcall *WindowsWakeByAddress)(void *address);

typedef struct {
    PlatformThreadMain entry;
    void *arg;
    PlatformThread *thread;
    int result;
} WindowsThreadStart;

__declspec(dllimport) int __stdcall CloseHandle(void *handle);
__declspec(dllimport) void *__stdcall CreateThread(
    void *security, size_t stack_size, WindowsThreadEntry entry, void *arg,
    unsigned long flags, unsigned long *thread_id
);
__declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *name);
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *name);
__declspec(dllimport) int __stdcall VirtualFree(void *address, size_t size, unsigned long free_type);

static WindowsWaitOnAddress windows_wait_on_address;
static WindowsWakeByAddress windows_wake_by_address_all;
static WindowsWakeByAddress windows_wake_by_address_single;

static int windows_thread_api_init(void) {
    void *module;

    if (windows_wait_on_address != 0 && windows_wake_by_address_all != 0 &&
        windows_wake_by_address_single != 0) {
        return 1;
    }
    module = LoadLibraryA("KernelBase.dll");
    if (module == 0) return 0;
    windows_wait_on_address = (WindowsWaitOnAddress)GetProcAddress(module, "WaitOnAddress");
    windows_wake_by_address_all =
        (WindowsWakeByAddress)GetProcAddress(module, "WakeByAddressAll");
    windows_wake_by_address_single =
        (WindowsWakeByAddress)GetProcAddress(module, "WakeByAddressSingle");
    return windows_wait_on_address != 0 && windows_wake_by_address_all != 0 &&
        windows_wake_by_address_single != 0;
}

static void windows_wait_int(volatile int *address, int expected) {
    if (!windows_thread_api_init()) return;
    (void)windows_wait_on_address(address, &expected, sizeof(expected), WIN_INFINITE);
}

static unsigned long __stdcall windows_thread_entry(void *arg) {
    WindowsThreadStart *start = (WindowsThreadStart *)arg;

    start->result = start->entry(start->arg);
    __atomic_store_n(&start->thread->clear_tid, 0, __ATOMIC_RELEASE);
    windows_wake_by_address_all((void *)&start->thread->clear_tid);
    return 0UL;
}

int platform_thread_start(PlatformThread *thread, PlatformThreadMain entry, void *arg, size_t stack_size) {
    WindowsThreadStart *start;
    void *handle;
    unsigned long thread_id = 0UL;

    if (thread == 0 || entry == 0 || !windows_thread_api_init()) return -1;
    start = (WindowsThreadStart *)platform_allocate_pages(4096U);
    if (start == 0) return -1;
    start->entry = entry;
    start->arg = arg;
    start->thread = thread;
    start->result = 0;
    thread->tid = 0;
    thread->clear_tid = 1;
    thread->stack = start;
    thread->stack_size = 4096U;
    handle = CreateThread(0, stack_size, windows_thread_entry, start, 0UL, &thread_id);
    if (handle == 0) {
        (void)VirtualFree(start, 0U, WIN_MEM_RELEASE);
        thread->clear_tid = 0;
        thread->stack = 0;
        thread->stack_size = 0U;
        return -1;
    }
    thread->tid = (int)thread_id;
    (void)CloseHandle(handle);
    return 0;
}

int platform_thread_join(PlatformThread *thread, int *result_out) {
    WindowsThreadStart *start;

    if (thread == 0 || thread->stack == 0) return -1;
    while (__atomic_load_n(&thread->clear_tid, __ATOMIC_ACQUIRE) != 0) {
        windows_wait_int(&thread->clear_tid, 1);
    }
    start = (WindowsThreadStart *)thread->stack;
    if (result_out != 0) *result_out = start->result;
    (void)VirtualFree(start, 0U, WIN_MEM_RELEASE);
    thread->tid = 0;
    thread->stack = 0;
    thread->stack_size = 0U;
    return 0;
}

void platform_mutex_init(PlatformMutex *mutex) {
    if (mutex != 0) __atomic_store_n(&mutex->state, 0, __ATOMIC_RELEASE);
}

void platform_mutex_lock(PlatformMutex *mutex) {
    int expected = 0;

    if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
    for (;;) {
        int previous = __atomic_exchange_n(&mutex->state, 2, __ATOMIC_ACQUIRE);
        if (previous == 0) return;
        windows_wait_int(&mutex->state, 2);
    }
}

void platform_mutex_unlock(PlatformMutex *mutex) {
    if (__atomic_fetch_sub(&mutex->state, 1, __ATOMIC_RELEASE) != 1) {
        __atomic_store_n(&mutex->state, 0, __ATOMIC_RELEASE);
        if (windows_thread_api_init()) windows_wake_by_address_single((void *)&mutex->state);
    }
}

void platform_semaphore_init(PlatformSemaphore *semaphore, int value) {
    if (semaphore != 0) __atomic_store_n(&semaphore->count, value, __ATOMIC_RELEASE);
}

void platform_semaphore_wait(PlatformSemaphore *semaphore) {
    for (;;) {
        int value = __atomic_load_n(&semaphore->count, __ATOMIC_ACQUIRE);

        while (value > 0) {
            int desired = value - 1;
            if (__atomic_compare_exchange_n(&semaphore->count, &value, desired, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
        }
        windows_wait_int(&semaphore->count, 0);
    }
}

void platform_semaphore_post(PlatformSemaphore *semaphore) {
    (void)__atomic_fetch_add(&semaphore->count, 1, __ATOMIC_RELEASE);
    if (windows_thread_api_init()) windows_wake_by_address_single((void *)&semaphore->count);
}