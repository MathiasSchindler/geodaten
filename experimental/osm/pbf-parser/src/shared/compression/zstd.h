#ifndef NEWOS_COMPRESSION_ZSTD_H
#define NEWOS_COMPRESSION_ZSTD_H

#include <stddef.h>

typedef enum {
    COMPRESSION_ZSTD_BACKEND_NONE = 0,
    COMPRESSION_ZSTD_BACKEND_CUSTOM
} CompressionZstdBackend;

typedef enum {
    COMPRESSION_ZSTD_OK = 0,
    COMPRESSION_ZSTD_ERR_TRUNCATED,
    COMPRESSION_ZSTD_ERR_CORRUPT,
    COMPRESSION_ZSTD_ERR_UNSUPPORTED,
    COMPRESSION_ZSTD_ERR_NOMEM
} CompressionZstdStatus;

typedef struct {
    CompressionZstdStatus status;
    CompressionZstdBackend backend;
    const char *message;
} CompressionZstdResult;

const char *compression_zstd_backend_name(CompressionZstdBackend backend);

CompressionZstdResult compression_zstd_frame_content_size(const void *src, size_t src_size, size_t *frame_content_size_out);
CompressionZstdResult compression_zstd_decompress_frame(void *dst, size_t dst_size, const void *src, size_t src_size, size_t *written_out);

#endif
