#include "hash_util.h"
#include "platform.h"
#include "runtime.h"

#define HASH_STREAM_BUFFER_SIZE 4096

void hash_to_hex(const unsigned char *digest, size_t digest_size, char *hex_out) {
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < digest_size; ++i) {
        hex_out[i * 2U] = digits[(digest[i] >> 4) & 0x0fU];
        hex_out[i * 2U + 1U] = digits[digest[i] & 0x0fU];
    }

    hex_out[digest_size * 2U] = '\0';
}

int hash_md5_stream(int fd, unsigned char out[HASH_MD5_SIZE]) {
    CryptoMd5Context ctx;
    unsigned char buffer[HASH_STREAM_BUFFER_SIZE];

    crypto_md5_init(&ctx);
    for (;;) {
        long bytes = platform_read(fd, buffer, sizeof(buffer));
        if (bytes < 0) {
            return -1;
        }
        if (bytes == 0) {
            break;
        }
        crypto_md5_update(&ctx, buffer, (size_t)bytes);
    }
    crypto_md5_final(&ctx, out);
    return 0;
}

int hash_sha256_stream(int fd, unsigned char out[HASH_SHA256_SIZE]) {
    CryptoSha256Context ctx;
    unsigned char buffer[HASH_STREAM_BUFFER_SIZE];

    crypto_sha256_init(&ctx);
    for (;;) {
        long bytes = platform_read(fd, buffer, sizeof(buffer));
        if (bytes < 0) {
            return -1;
        }
        if (bytes == 0) {
            break;
        }
        crypto_sha256_update(&ctx, buffer, (size_t)bytes);
    }
    crypto_sha256_final(&ctx, out);
    return 0;
}

int hash_sha512_stream(int fd, unsigned char out[HASH_SHA512_SIZE]) {
    CryptoSha512Context ctx;
    unsigned char buffer[HASH_STREAM_BUFFER_SIZE];

    crypto_sha512_init(&ctx);
    for (;;) {
        long bytes = platform_read(fd, buffer, sizeof(buffer));
        if (bytes < 0) {
            return -1;
        }
        if (bytes == 0) {
            break;
        }
        crypto_sha512_update(&ctx, buffer, (size_t)bytes);
    }
    crypto_sha512_final(&ctx, out);
    return 0;
}
