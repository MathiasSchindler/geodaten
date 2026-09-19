#include "platform.h"
#include "runtime.h"

#define THREADTEST_DEFAULT_WORKERS 4U
#define THREADTEST_ITERATIONS 25000U
#define THREADTEST_MAX_WORKERS 256U

typedef struct {
    PlatformMutex *mutex;
    unsigned long long *counter;
    unsigned int iterations;
    unsigned int worker_index;
} ThreadTestWorker;

static int threadtest_worker_main(void *arg) {
    ThreadTestWorker *worker = (ThreadTestWorker *)arg;
    unsigned int iteration_index;

    for (iteration_index = 0U; iteration_index < worker->iterations; ++iteration_index) {
        platform_mutex_lock(worker->mutex);
        *worker->counter += 1ULL;
        platform_mutex_unlock(worker->mutex);
    }
    return (int)(worker->worker_index + worker->iterations);
}

static void threadtest_write_failure(const char *message) {
    rt_write_cstr(2, "test-thread: ");
    rt_write_cstr(2, message);
    rt_write_char(2, '\n');
}

static int threadtest_parse_uint(const char *text, unsigned int *value_out) {
    unsigned long long value = 0ULL;
    size_t index = 0U;

    if (text == 0 || text[0] == '\0') {
        return -1;
    }
    while (text[index] != '\0') {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        value = value * 10ULL + (unsigned long long)(text[index] - '0');
        if (value > 4294967295ULL) {
            return -1;
        }
        index += 1U;
    }
    *value_out = (unsigned int)value;
    return 0;
}

static void threadtest_write_usage(const char *program) {
    rt_write_cstr(2, "Usage: ");
    rt_write_cstr(2, program);
    rt_write_cstr(2, " [workers] [iterations]\n");
}

int main(int argc, char **argv) {
    PlatformThread *threads;
    ThreadTestWorker *workers;
    PlatformMutex mutex;
    unsigned long long counter = 0ULL;
    unsigned int worker_count = THREADTEST_DEFAULT_WORKERS;
    unsigned int iterations = THREADTEST_ITERATIONS;
    unsigned int worker_index;
    const char *program = argc > 0 ? argv[0] : "test-thread";

    if (argc > 3) {
        threadtest_write_usage(program);
        return 1;
    }
    if (argc >= 2 && (threadtest_parse_uint(argv[1], &worker_count) != 0 || worker_count == 0U || worker_count > THREADTEST_MAX_WORKERS)) {
        threadtest_write_failure("invalid worker count");
        threadtest_write_usage(program);
        return 1;
    }
    if (argc >= 3 && (threadtest_parse_uint(argv[2], &iterations) != 0 || iterations == 0U)) {
        threadtest_write_failure("invalid iteration count");
        threadtest_write_usage(program);
        return 1;
    }

    threads = (PlatformThread *)rt_malloc(sizeof(PlatformThread) * (size_t)worker_count);
    workers = (ThreadTestWorker *)rt_malloc(sizeof(ThreadTestWorker) * (size_t)worker_count);
    if (threads == 0 || workers == 0) {
        threadtest_write_failure("out of memory");
        return 1;
    }

    platform_mutex_init(&mutex);
    for (worker_index = 0U; worker_index < worker_count; ++worker_index) {
        workers[worker_index].mutex = &mutex;
        workers[worker_index].counter = &counter;
        workers[worker_index].iterations = iterations;
        workers[worker_index].worker_index = worker_index;
        if (platform_thread_start(&threads[worker_index], threadtest_worker_main, &workers[worker_index], 0U) != 0) {
            threadtest_write_failure("could not start thread");
            return 1;
        }
    }
    for (worker_index = 0U; worker_index < worker_count; ++worker_index) {
        int result = 0;
        int expected = (int)(worker_index + iterations);

        if (platform_thread_join(&threads[worker_index], &result) != 0) {
            threadtest_write_failure("could not join thread");
            return 1;
        }
        if (result != expected) {
            threadtest_write_failure("thread result mismatch");
            return 1;
        }
    }
    if (counter != (unsigned long long)worker_count * (unsigned long long)iterations) {
        threadtest_write_failure("counter mismatch");
        return 1;
    }

    rt_write_cstr(1, "threads: ");
    rt_write_uint(1, worker_count);
    rt_write_char(1, '\n');
    rt_write_cstr(1, "iterations_per_thread: ");
    rt_write_uint(1, iterations);
    rt_write_char(1, '\n');
    rt_write_cstr(1, "counter: ");
    rt_write_uint(1, counter);
    rt_write_char(1, '\n');
    rt_write_cstr(1, "status: ok\n");
    rt_free(workers);
    rt_free(threads);
    return 0;
}