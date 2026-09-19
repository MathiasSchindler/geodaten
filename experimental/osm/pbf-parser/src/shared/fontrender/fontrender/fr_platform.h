#ifndef FONTRENDER_FR_PLATFORM_H
#define FONTRENDER_FR_PLATFORM_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    FR_PLATFORM_LOG_ERROR = 0,
    FR_PLATFORM_LOG_WARN = 1,
    FR_PLATFORM_LOG_INFO = 2,
    FR_PLATFORM_LOG_DEBUG = 3
} FrPlatformLogLevel;

typedef enum {
    FR_PLATFORM_FILE_OK = 0,
    FR_PLATFORM_FILE_ERR_IO = -1,
    FR_PLATFORM_FILE_ERR_NO_MEMORY = -2
} FrPlatformFileResult;

typedef struct {
    const uint8_t *data;
    size_t size;
    void *handle;
} FrPlatformFile;

typedef void *(*FrPlatformAllocFn)(void *user_data, size_t size);
typedef void *(*FrPlatformReallocFn)(void *user_data, void *ptr, size_t size);
typedef void (*FrPlatformFreeFn)(void *user_data, void *ptr);
typedef void *(*FrPlatformMemcpyFn)(void *user_data, void *dst, const void *src, size_t size);
typedef void *(*FrPlatformMemmoveFn)(void *user_data, void *dst, const void *src, size_t size);
typedef void *(*FrPlatformMemsetFn)(void *user_data, void *dst, int value, size_t size);
typedef int (*FrPlatformMemcmpFn)(void *user_data, const void *lhs, const void *rhs, size_t size);
typedef FrPlatformFileResult (*FrPlatformLoadFileFn)(void *user_data, const char *path, FrPlatformFile *out_file);
typedef void (*FrPlatformUnloadFileFn)(void *user_data, FrPlatformFile *file);
typedef void *(*FrPlatformMutexCreateFn)(void *user_data);
typedef void (*FrPlatformMutexDestroyFn)(void *user_data, void *mutex);
typedef void (*FrPlatformMutexLockFn)(void *user_data, void *mutex);
typedef void (*FrPlatformMutexUnlockFn)(void *user_data, void *mutex);
typedef void (*FrPlatformLogFn)(void *user_data, FrPlatformLogLevel level, const char *component,
                                const char *fmt, va_list args);

typedef struct {
    void *user_data;
    FrPlatformAllocFn alloc;
    FrPlatformReallocFn realloc;
    FrPlatformFreeFn free;
    FrPlatformMemcpyFn memcpy;
    FrPlatformMemmoveFn memmove;
    FrPlatformMemsetFn memset;
    FrPlatformMemcmpFn memcmp;
    FrPlatformLoadFileFn load_file;
    FrPlatformUnloadFileFn unload_file;
    FrPlatformMutexCreateFn mutex_create;
    FrPlatformMutexDestroyFn mutex_destroy;
    FrPlatformMutexLockFn mutex_lock;
    FrPlatformMutexUnlockFn mutex_unlock;
    FrPlatformLogFn log;
} FrPlatform;

int fr_platform_set(const FrPlatform *platform);
void fr_platform_get(FrPlatform *out_platform);
void fr_platform_get_default(FrPlatform *out_platform);

#endif
