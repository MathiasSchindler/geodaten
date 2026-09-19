#ifndef NEWOS_COMPRESSION_CRC32_H
#define NEWOS_COMPRESSION_CRC32_H

#include <stddef.h>

unsigned int compression_crc32_update(unsigned int crc, const unsigned char *data, size_t length);
unsigned int compression_crc32_finish(unsigned int crc);
unsigned int compression_crc32(const unsigned char *data, size_t length);

#endif