#ifndef FR_PLATFORM_INTERNAL_H
#define FR_PLATFORM_INTERNAL_H

#include <stddef.h>

#include "fontrender/fr_platform.h"

int fr_platform_validate(const FrPlatform *platform);
void fr_platform_resolve(FrPlatform *out_platform);

void *fr_platform_alloc_with(const FrPlatform *platform, size_t size);
void *fr_platform_calloc_with(const FrPlatform *platform, size_t count, size_t size);
void *fr_platform_realloc_with(const FrPlatform *platform, void *ptr, size_t size);
void fr_platform_free_with(const FrPlatform *platform, void *ptr);
void *fr_platform_memcpy_with(const FrPlatform *platform, void *dst, const void *src, size_t size);
void *fr_platform_memmove_with(const FrPlatform *platform, void *dst, const void *src, size_t size);
void *fr_platform_memset_with(const FrPlatform *platform, void *dst, int value, size_t size);
int fr_platform_memcmp_with(const FrPlatform *platform, const void *lhs, const void *rhs, size_t size);
FrPlatformFileResult fr_platform_load_file_with(const FrPlatform *platform, const char *path, FrPlatformFile *out_file);
void fr_platform_unload_file_with(const FrPlatform *platform, FrPlatformFile *file);
void fr_platform_log_with(const FrPlatform *platform, FrPlatformLogLevel level, const char *component,
                          const char *fmt, ...);

void *fr_platform_alloc(size_t size);
void *fr_platform_calloc(size_t count, size_t size);
void *fr_platform_realloc(void *ptr, size_t size);
void fr_platform_free(void *ptr);
void *fr_platform_memcpy(void *dst, const void *src, size_t size);
void *fr_platform_memmove(void *dst, const void *src, size_t size);
void *fr_platform_memset(void *dst, int value, size_t size);
int fr_platform_memcmp(const void *lhs, const void *rhs, size_t size);
void fr_platform_log_message(FrPlatformLogLevel level, const char *component, const char *fmt, ...);

#endif
