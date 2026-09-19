#include "fontrender/fr_platform.h"

#include "fr_platform_internal.h"

#if defined(__clang__)
#define FR_PLATFORM_NO_OPT __attribute__((optnone))
#elif defined(__GNUC__)
#define FR_PLATFORM_NO_OPT __attribute__((optimize("O0")))
#else
#define FR_PLATFORM_NO_OPT
#endif

static void *fr_default_alloc(void *user_data, size_t size);
static void *fr_default_realloc(void *user_data, void *ptr, size_t size);
static void fr_default_free(void *user_data, void *ptr);
static FR_PLATFORM_NO_OPT void *fr_default_memcpy(void *user_data, void *dst, const void *src, size_t size);
static FR_PLATFORM_NO_OPT void *fr_default_memmove(void *user_data, void *dst, const void *src, size_t size);
static FR_PLATFORM_NO_OPT void *fr_default_memset(void *user_data, void *dst, int value, size_t size);
static FR_PLATFORM_NO_OPT int fr_default_memcmp(void *user_data, const void *lhs, const void *rhs, size_t size);
static FrPlatformFileResult fr_default_load_file(void *user_data, const char *path, FrPlatformFile *out_file);
static void fr_default_unload_file(void *user_data, FrPlatformFile *file);
static void fr_default_log(void *user_data, FrPlatformLogLevel level, const char *component,
                           const char *fmt, va_list args);

static const FrPlatform fr_platform_default = {
    NULL,
    fr_default_alloc,
    fr_default_realloc,
    fr_default_free,
    fr_default_memcpy,
    fr_default_memmove,
    fr_default_memset,
    fr_default_memcmp,
    fr_default_load_file,
    fr_default_unload_file,
    NULL,
    NULL,
    NULL,
    NULL,
    fr_default_log
};

static FrPlatform fr_platform_current;
static FrPlatform fr_platform_effective_global = {
    NULL,
    fr_default_alloc,
    fr_default_realloc,
    fr_default_free,
    fr_default_memcpy,
    fr_default_memmove,
    fr_default_memset,
    fr_default_memcmp,
    fr_default_load_file,
    fr_default_unload_file,
    NULL,
    NULL,
    NULL,
    NULL,
    fr_default_log
};

static void fr_platform_clear_file(FrPlatformFile *file) {
    if (file == NULL) {
        return;
    }
    file->data = NULL;
    file->size = 0u;
    file->handle = NULL;
}

static void fr_platform_apply_overrides(FrPlatform *dst, const FrPlatform *src) {
    if (dst == NULL || src == NULL) {
        return;
    }

    dst->user_data = src->user_data;
    if (src->alloc != NULL && src->realloc != NULL && src->free != NULL) {
        dst->alloc = src->alloc;
        dst->realloc = src->realloc;
        dst->free = src->free;
    }
    if (src->memcpy != NULL) {
        dst->memcpy = src->memcpy;
    }
    if (src->memmove != NULL) {
        dst->memmove = src->memmove;
    }
    if (src->memset != NULL) {
        dst->memset = src->memset;
    }
    if (src->memcmp != NULL) {
        dst->memcmp = src->memcmp;
    }
    if (src->load_file != NULL && src->unload_file != NULL) {
        dst->load_file = src->load_file;
        dst->unload_file = src->unload_file;
    }
    if (src->mutex_create != NULL && src->mutex_destroy != NULL &&
        src->mutex_lock != NULL && src->mutex_unlock != NULL) {
        dst->mutex_create = src->mutex_create;
        dst->mutex_destroy = src->mutex_destroy;
        dst->mutex_lock = src->mutex_lock;
        dst->mutex_unlock = src->mutex_unlock;
    }
    if (src->log != NULL) {
        dst->log = src->log;
    }
}

static int fr_platform_mutex_hooks_are_consistent(const FrPlatform *platform) {
    if (platform == NULL) {
        return 1;
    }
    return ((platform->mutex_create == NULL) == (platform->mutex_destroy == NULL)) &&
           ((platform->mutex_create == NULL) == (platform->mutex_lock == NULL)) &&
           ((platform->mutex_create == NULL) == (platform->mutex_unlock == NULL));
}

static int fr_platform_is_complete(const FrPlatform *platform) {
    if (platform == NULL) {
        return 0;
    }
    return platform->alloc != NULL &&
           platform->realloc != NULL &&
           platform->free != NULL &&
           platform->memcpy != NULL &&
           platform->memmove != NULL &&
           platform->memset != NULL &&
           platform->memcmp != NULL &&
           platform->load_file != NULL &&
           platform->unload_file != NULL &&
           fr_platform_mutex_hooks_are_consistent(platform) &&
           platform->log != NULL;
}

static const FrPlatform *fr_platform_get_effective(const FrPlatform *platform, FrPlatform *scratch) {
    if (platform == NULL) {
        return &fr_platform_default;
    }
    if (fr_platform_is_complete(platform)) {
        return platform;
    }
    fr_platform_get_default(scratch);
    fr_platform_apply_overrides(scratch, platform);
    return scratch;
}

static void *fr_default_alloc(void *user_data, size_t size) {
    (void)user_data;
    (void)size;
    return NULL;
}

static void *fr_default_realloc(void *user_data, void *ptr, size_t size) {
    (void)user_data;
    (void)ptr;
    (void)size;
    return NULL;
}

static void fr_default_free(void *user_data, void *ptr) {
    (void)user_data;
    (void)ptr;
}

static FR_PLATFORM_NO_OPT void *fr_default_memcpy(void *user_data, void *dst, const void *src, size_t size) {
    unsigned char *dst_bytes;
    const unsigned char *src_bytes;
    size_t i;

    (void)user_data;
    dst_bytes = (unsigned char *)dst;
    src_bytes = (const unsigned char *)src;
    for (i = 0u; i < size; ++i) {
        dst_bytes[i] = src_bytes[i];
    }
    return dst;
}

static FR_PLATFORM_NO_OPT void *fr_default_memmove(void *user_data, void *dst, const void *src, size_t size) {
    unsigned char *dst_bytes;
    const unsigned char *src_bytes;
    size_t i;

    (void)user_data;
    dst_bytes = (unsigned char *)dst;
    src_bytes = (const unsigned char *)src;
    if (dst_bytes == src_bytes || size == 0u) {
        return dst;
    }
    if (dst_bytes < src_bytes || dst_bytes >= src_bytes + size) {
        for (i = 0u; i < size; ++i) {
            dst_bytes[i] = src_bytes[i];
        }
        return dst;
    }
    for (i = size; i > 0u; --i) {
        dst_bytes[i - 1u] = src_bytes[i - 1u];
    }
    return dst;
}

static FR_PLATFORM_NO_OPT void *fr_default_memset(void *user_data, void *dst, int value, size_t size) {
    unsigned char *dst_bytes;
    unsigned char fill;
    size_t i;

    (void)user_data;
    dst_bytes = (unsigned char *)dst;
    fill = (unsigned char)value;
    for (i = 0u; i < size; ++i) {
        dst_bytes[i] = fill;
    }
    return dst;
}

static FR_PLATFORM_NO_OPT int fr_default_memcmp(void *user_data, const void *lhs, const void *rhs, size_t size) {
    const unsigned char *lhs_bytes;
    const unsigned char *rhs_bytes;
    size_t i;

    (void)user_data;
    lhs_bytes = (const unsigned char *)lhs;
    rhs_bytes = (const unsigned char *)rhs;
    for (i = 0u; i < size; ++i) {
        if (lhs_bytes[i] != rhs_bytes[i]) {
            return (lhs_bytes[i] < rhs_bytes[i]) ? -1 : 1;
        }
    }
    return 0;
}

static FrPlatformFileResult fr_default_load_file(void *user_data, const char *path, FrPlatformFile *out_file) {
    (void)user_data;
    (void)path;
    fr_platform_clear_file(out_file);
    return FR_PLATFORM_FILE_ERR_IO;
}

static void fr_default_unload_file(void *user_data, FrPlatformFile *file) {
    (void)user_data;
    fr_platform_clear_file(file);
}

static void fr_default_log(void *user_data, FrPlatformLogLevel level, const char *component,
                           const char *fmt, va_list args) {
    (void)user_data;
    (void)level;
    (void)component;
    (void)fmt;
    (void)args;
}

int fr_platform_validate(const FrPlatform *platform) {
    if (platform == NULL) {
        return 0;
    }
    if ((platform->alloc == NULL) != (platform->realloc == NULL) ||
        (platform->alloc == NULL) != (platform->free == NULL)) {
        return -1;
    }
    if ((platform->load_file == NULL) != (platform->unload_file == NULL)) {
        return -1;
    }
    if (!fr_platform_mutex_hooks_are_consistent(platform)) {
        return -1;
    }
    return 0;
}

void fr_platform_get_default(FrPlatform *out_platform) {
    if (out_platform == NULL) {
        return;
    }
    *out_platform = fr_platform_default;
}

void fr_platform_resolve(FrPlatform *out_platform) {
    if (out_platform == NULL) {
        return;
    }
    *out_platform = fr_platform_effective_global;
}

int fr_platform_set(const FrPlatform *platform) {
    if (fr_platform_validate(platform) != 0) {
        return -1;
    }
    if (platform == NULL) {
        fr_platform_current = (FrPlatform){0};
        fr_platform_effective_global = fr_platform_default;
        return 0;
    }
    fr_platform_current = *platform;
    fr_platform_effective_global = fr_platform_default;
    fr_platform_apply_overrides(&fr_platform_effective_global, platform);
    return 0;
}

void fr_platform_get(FrPlatform *out_platform) {
    if (out_platform == NULL) {
        return;
    }
    *out_platform = fr_platform_current;
}

void *fr_platform_alloc_with(const FrPlatform *platform, size_t size) {
    FrPlatform resolved;
    const FrPlatform *effective;

    effective = fr_platform_get_effective(platform, &resolved);
    return effective->alloc(effective->user_data, size);
}

void *fr_platform_calloc_with(const FrPlatform *platform, size_t count, size_t size) {
    FrPlatform resolved;
    const FrPlatform *effective;
    size_t total_size;
    void *ptr;

    if (count != 0u && size > SIZE_MAX / count) {
        return NULL;
    }
    effective = fr_platform_get_effective(platform, &resolved);
    total_size = count * size;
    ptr = effective->alloc(effective->user_data, total_size == 0u ? 1u : total_size);
    if (ptr == NULL) {
        return NULL;
    }
    effective->memset(effective->user_data, ptr, 0, total_size);
    return ptr;
}

void *fr_platform_realloc_with(const FrPlatform *platform, void *ptr, size_t size) {
    FrPlatform resolved;
    const FrPlatform *effective;

    effective = fr_platform_get_effective(platform, &resolved);
    return effective->realloc(effective->user_data, ptr, size);
}

void fr_platform_free_with(const FrPlatform *platform, void *ptr) {
    FrPlatform resolved;
    const FrPlatform *effective;

    if (ptr == NULL) {
        return;
    }
    effective = fr_platform_get_effective(platform, &resolved);
    effective->free(effective->user_data, ptr);
}

void *fr_platform_memcpy_with(const FrPlatform *platform, void *dst, const void *src, size_t size) {
    FrPlatform resolved;
    const FrPlatform *effective;

    effective = fr_platform_get_effective(platform, &resolved);
    return effective->memcpy(effective->user_data, dst, src, size);
}

void *fr_platform_memmove_with(const FrPlatform *platform, void *dst, const void *src, size_t size) {
    FrPlatform resolved;
    const FrPlatform *effective;

    effective = fr_platform_get_effective(platform, &resolved);
    return effective->memmove(effective->user_data, dst, src, size);
}

void *fr_platform_memset_with(const FrPlatform *platform, void *dst, int value, size_t size) {
    FrPlatform resolved;
    const FrPlatform *effective;

    effective = fr_platform_get_effective(platform, &resolved);
    return effective->memset(effective->user_data, dst, value, size);
}

int fr_platform_memcmp_with(const FrPlatform *platform, const void *lhs, const void *rhs, size_t size) {
    FrPlatform resolved;
    const FrPlatform *effective;

    effective = fr_platform_get_effective(platform, &resolved);
    return effective->memcmp(effective->user_data, lhs, rhs, size);
}

FrPlatformFileResult fr_platform_load_file_with(const FrPlatform *platform, const char *path, FrPlatformFile *out_file) {
    FrPlatform resolved;
    const FrPlatform *effective;

    effective = fr_platform_get_effective(platform, &resolved);
    return effective->load_file(effective->user_data, path, out_file);
}

void fr_platform_unload_file_with(const FrPlatform *platform, FrPlatformFile *file) {
    FrPlatform resolved;
    const FrPlatform *effective;

    if (file == NULL || file->data == NULL) {
        if (file != NULL) {
            fr_platform_clear_file(file);
        }
        return;
    }
    effective = fr_platform_get_effective(platform, &resolved);
    effective->unload_file(effective->user_data, file);
}

void fr_platform_log_with(const FrPlatform *platform, FrPlatformLogLevel level, const char *component,
                          const char *fmt, ...) {
    FrPlatform resolved;
    const FrPlatform *effective;
    va_list args;

    effective = fr_platform_get_effective(platform, &resolved);
    va_start(args, fmt);
    effective->log(effective->user_data, level, component, fmt, args);
    va_end(args);
}

void *fr_platform_alloc(size_t size) {
    return fr_platform_effective_global.alloc(fr_platform_effective_global.user_data, size);
}

void *fr_platform_calloc(size_t count, size_t size) {
    size_t total_size;
    void *ptr;

    if (count != 0u && size > SIZE_MAX / count) {
        return NULL;
    }
    total_size = count * size;
    ptr = fr_platform_effective_global.alloc(fr_platform_effective_global.user_data, total_size == 0u ? 1u : total_size);
    if (ptr == NULL) {
        return NULL;
    }
    fr_platform_effective_global.memset(fr_platform_effective_global.user_data, ptr, 0, total_size);
    return ptr;
}

void *fr_platform_realloc(void *ptr, size_t size) {
    return fr_platform_effective_global.realloc(fr_platform_effective_global.user_data, ptr, size);
}

void fr_platform_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    fr_platform_effective_global.free(fr_platform_effective_global.user_data, ptr);
}

void *fr_platform_memcpy(void *dst, const void *src, size_t size) {
    return fr_platform_effective_global.memcpy(fr_platform_effective_global.user_data, dst, src, size);
}

void *fr_platform_memmove(void *dst, const void *src, size_t size) {
    return fr_platform_effective_global.memmove(fr_platform_effective_global.user_data, dst, src, size);
}

void *fr_platform_memset(void *dst, int value, size_t size) {
    return fr_platform_effective_global.memset(fr_platform_effective_global.user_data, dst, value, size);
}

int fr_platform_memcmp(const void *lhs, const void *rhs, size_t size) {
    return fr_platform_effective_global.memcmp(fr_platform_effective_global.user_data, lhs, rhs, size);
}

void fr_platform_log_message(FrPlatformLogLevel level, const char *component, const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    fr_platform_effective_global.log(fr_platform_effective_global.user_data, level, component, fmt, args);
    va_end(args);
}
