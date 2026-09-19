#ifndef NEWOS_ARCHIVE_UTIL_H
#define NEWOS_ARCHIVE_UTIL_H

#include <stddef.h>

unsigned int archive_crc32_update(unsigned int crc, const unsigned char *data, size_t length);
unsigned int archive_crc32_finish(unsigned int crc);
int archive_read_exact(int fd, unsigned char *buffer, size_t count);
unsigned int archive_read_u32_le(const unsigned char *bytes);
unsigned long long archive_read_u64_le(const unsigned char *bytes);
void archive_store_u32_le(unsigned char *bytes, unsigned int value);
void archive_store_u64_le(unsigned char *bytes, unsigned long long value);
void archive_write_octal(char *field, size_t field_size, unsigned long long value);
unsigned long long archive_parse_octal(const char *field, size_t field_size);

#endif
