#include "compression/crc32.h"

unsigned int compression_crc32_update(unsigned int crc, const unsigned char *data, size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        unsigned int value = (crc ^ (unsigned int)data[index]) & 0xffU;
        unsigned int bit;

        for (bit = 0U; bit < 8U; ++bit) {
            if ((value & 1U) != 0U) {
                value = (value >> 1U) ^ 0xedb88320U;
            } else {
                value >>= 1U;
            }
        }
        crc = (crc >> 8U) ^ value;
    }
    return crc;
}

unsigned int compression_crc32_finish(unsigned int crc) {
    return crc ^ 0xffffffffU;
}

unsigned int compression_crc32(const unsigned char *data, size_t length) {
    return compression_crc32_finish(compression_crc32_update(0xffffffffU, data, length));
}