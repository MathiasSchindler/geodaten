#ifndef NEWOS_HASH_UTIL_H
#define NEWOS_HASH_UTIL_H

#include <stddef.h>
#include "crypto/md5.h"
#include "crypto/sha256.h"
#include "crypto/sha512.h"

#define HASH_MD5_SIZE CRYPTO_MD5_DIGEST_SIZE
#define HASH_SHA256_SIZE CRYPTO_SHA256_DIGEST_SIZE
#define HASH_SHA512_SIZE CRYPTO_SHA512_DIGEST_SIZE

int hash_md5_stream(int fd, unsigned char out[HASH_MD5_SIZE]);
int hash_sha256_stream(int fd, unsigned char out[HASH_SHA256_SIZE]);
int hash_sha512_stream(int fd, unsigned char out[HASH_SHA512_SIZE]);
void hash_to_hex(const unsigned char *digest, size_t digest_size, char *hex_out);

#endif
