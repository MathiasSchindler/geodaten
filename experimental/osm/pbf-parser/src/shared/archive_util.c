#include "archive_util.h"
#include "compression/crc32.h"
#include "platform.h"

unsigned int archive_crc32_update(unsigned int crc, const unsigned char *data, size_t length) {
    return compression_crc32_update(crc, data, length);
}

unsigned int archive_crc32_finish(unsigned int crc) {
    return compression_crc32_finish(crc);
}

int archive_read_exact(int fd, unsigned char *buffer, size_t count) {
    size_t offset = 0;

    while (offset < count) {
        long bytes = platform_read(fd, buffer + offset, count - offset);
        if (bytes <= 0) {
            return -1;
        }
        offset += (size_t)bytes;
    }

    return 0;
}

unsigned int archive_read_u32_le(const unsigned char *bytes) {
    return (unsigned int)bytes[0] |
           ((unsigned int)bytes[1] << 8) |
           ((unsigned int)bytes[2] << 16) |
           ((unsigned int)bytes[3] << 24);
}

unsigned long long archive_read_u64_le(const unsigned char *bytes) {
    unsigned long long value = 0;
    size_t i;

    for (i = 0; i < 8; ++i) {
        value |= ((unsigned long long)bytes[i]) << (i * 8);
    }

    return value;
}

void archive_store_u32_le(unsigned char *bytes, unsigned int value) {
    bytes[0] = (unsigned char)(value & 0xffU);
    bytes[1] = (unsigned char)((value >> 8) & 0xffU);
    bytes[2] = (unsigned char)((value >> 16) & 0xffU);
    bytes[3] = (unsigned char)((value >> 24) & 0xffU);
}

void archive_store_u64_le(unsigned char *bytes, unsigned long long value) {
    size_t i;

    for (i = 0; i < 8; ++i) {
        bytes[i] = (unsigned char)((value >> (i * 8)) & 0xffU);
    }
}

void archive_write_octal(char *field, size_t field_size, unsigned long long value) {
    size_t i;

    for (i = 0; i < field_size; ++i) {
        field[i] = '0';
    }

    if (field_size > 0) {
        field[field_size - 1] = ' ';
    }

    if (field_size > 1) {
        size_t pos = field_size - 2;
        field[pos] = '\0';

        do {
            field[pos] = (char)('0' + (value & 7ULL));
            value >>= 3;
            if (pos == 0) {
                break;
            }
            pos -= 1;
        } while (value != 0ULL);
    }
}

unsigned long long archive_parse_octal(const char *field, size_t field_size) {
    unsigned long long value = 0;
    size_t i = 0;

    while (i < field_size && (field[i] == ' ' || field[i] == '\0')) {
        i += 1;
    }

    while (i < field_size && field[i] >= '0' && field[i] <= '7') {
        value = (value * 8ULL) + (unsigned long long)(field[i] - '0');
        i += 1;
    }

    return value;
}
