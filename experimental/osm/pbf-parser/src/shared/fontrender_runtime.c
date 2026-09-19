#include "fontrender_runtime.h"

#include "fontrender/fr_platform.h"
#include "platform.h"
#include "runtime.h"

static void *fontrender_alloc(void *user_data, size_t size) {
    (void)user_data;
    return rt_malloc(size == 0U ? 1U : size);
}

static void *fontrender_realloc(void *user_data, void *ptr, size_t size) {
    (void)user_data;
    return rt_realloc(ptr, size == 0U ? 1U : size);
}

static void fontrender_free(void *user_data, void *ptr) {
    (void)user_data;
    rt_free(ptr);
}

static void *fontrender_memcpy(void *user_data, void *dst, const void *src, size_t size) {
    (void)user_data;
    return memcpy(dst, src, size);
}

static void *fontrender_memmove(void *user_data, void *dst, const void *src, size_t size) {
    (void)user_data;
    return memmove(dst, src, size);
}

static void *fontrender_memset(void *user_data, void *dst, int value, size_t size) {
    (void)user_data;
    return memset(dst, value, size);
}

static int fontrender_memcmp(void *user_data, const void *lhs, const void *rhs, size_t size) {
    (void)user_data;
    return memcmp(lhs, rhs, size);
}

static FrPlatformFileResult fontrender_load_file(void *user_data, const char *path, FrPlatformFile *out_file) {
    int fd;
    long long file_size;
    unsigned char *data;
    size_t offset = 0U;

    (void)user_data;
    if (out_file == 0) return FR_PLATFORM_FILE_ERR_IO;
    out_file->data = 0;
    out_file->size = 0U;
    out_file->handle = 0;
    fd = platform_open_read(path);
    if (fd < 0) return FR_PLATFORM_FILE_ERR_IO;
    file_size = platform_seek(fd, 0, PLATFORM_SEEK_END);
    if (file_size < 0 || platform_seek(fd, 0, PLATFORM_SEEK_SET) != 0) {
        (void)platform_close(fd);
        return FR_PLATFORM_FILE_ERR_IO;
    }
    data = (unsigned char *)rt_malloc((size_t)file_size == 0U ? 1U : (size_t)file_size);
    if (data == 0) {
        (void)platform_close(fd);
        return FR_PLATFORM_FILE_ERR_NO_MEMORY;
    }
    while (offset < (size_t)file_size) {
        long n = platform_read(fd, data + offset, (size_t)file_size - offset);
        if (n <= 0) {
            rt_free(data);
            (void)platform_close(fd);
            return FR_PLATFORM_FILE_ERR_IO;
        }
        offset += (size_t)n;
    }
    (void)platform_close(fd);
    out_file->data = data;
    out_file->size = (size_t)file_size;
    out_file->handle = data;
    return FR_PLATFORM_FILE_OK;
}

static void fontrender_unload_file(void *user_data, FrPlatformFile *file) {
    (void)user_data;
    if (file == 0) return;
    rt_free(file->handle);
    file->data = 0;
    file->size = 0U;
    file->handle = 0;
}

static void fontrender_log(void *user_data, FrPlatformLogLevel level, const char *component, const char *fmt, va_list args) {
    (void)user_data;
    (void)level;
    (void)fmt;
    (void)args;
    if (component != 0) {
        rt_write_cstr(2, "fontrender: ");
        rt_write_cstr(2, component);
        rt_write_char(2, '\n');
    }
}

int fontrender_runtime_install(void) {
    FrPlatform platform;

    platform.user_data = 0;
    platform.alloc = fontrender_alloc;
    platform.realloc = fontrender_realloc;
    platform.free = fontrender_free;
    platform.memcpy = fontrender_memcpy;
    platform.memmove = fontrender_memmove;
    platform.memset = fontrender_memset;
    platform.memcmp = fontrender_memcmp;
    platform.load_file = fontrender_load_file;
    platform.unload_file = fontrender_unload_file;
    platform.mutex_create = 0;
    platform.mutex_destroy = 0;
    platform.mutex_lock = 0;
    platform.mutex_unlock = 0;
    platform.log = fontrender_log;
    return fr_platform_set(&platform);
}